#!/usr/bin/env python3
"""Treinamento cíclico e relatório individual das 40 perguntas do Tsukuyomi G2 0.1."""

from collections import Counter, defaultdict
import difflib
import hashlib
import json
import math
import os
from pathlib import Path
import re
import subprocess
import shutil
import tempfile
import sys
import time
import unicodedata

MAX_RESPONSE_BYTES = 160
TRAINING_QUESTIONS = 40
MAX_CYCLES = 100
MAX_EPOCHS_PER_CYCLE = 1000
MIN_LEARNING_RATE = 1e-5
MAX_LEARNING_RATE = 1e-2
# LR-base fixo e interno; a configuração externa do workflow é ignorada.
INITIAL_LEARNING_RATE = 1e-3


def learning_rate_for_lexical_level(base_learning_rate: float, lexical_level: float) -> float:
    """Aplica a curva lexical fixa, independente do LR externo."""
    del base_learning_rate  # Mantido apenas para compatibilidade da assinatura.
    if not math.isfinite(lexical_level) or not 0.0 <= lexical_level <= 1.0:
        raise ValueError("Nível lexical precisa estar entre 0 e 1.")
    anchors = (
        (0.00, 1e-3),
        (0.25, 7.5e-4),
        (0.50, 5e-4),
        (0.75, 2.5e-4),
        (0.85, 1e-4),
        (0.90, 1e-5),
    )
    if lexical_level >= anchors[-1][0]:
        return MIN_LEARNING_RATE
    for (left_level, left_rate), (right_level, right_rate) in zip(anchors, anchors[1:]):
        if lexical_level <= right_level:
            fraction = (lexical_level - left_level) / (right_level - left_level)
            rate = left_rate + fraction * (right_rate - left_rate)
            return max(MIN_LEARNING_RATE, min(1e-3, rate))
    return MIN_LEARNING_RATE


def write_question_learning_rates(corpus: Path, rates: dict[str, float], destination: Path) -> int:
    """Associa cada byte-alvo IA: ao LR calculado para sua própria pergunta."""
    raw = corpus.read_bytes()
    offset = 0
    current_question = None
    found = set()
    ranges = []
    for line in raw.splitlines(keepends=True):
        content = line.rstrip(b"\r\n")
        leading = len(content) - len(content.lstrip(b" \t"))
        marker = content[leading:]
        if offset == 0 and marker.startswith(b"\xef\xbb\xbf"):
            marker = marker[3:]
            leading += 3
        if marker.startswith(b"U:"):
            current_question = marker[2:].decode("utf-8").strip()
        elif marker.startswith(b"IA:") and current_question is not None:
            if current_question not in rates:
                raise RuntimeError(f"Learning rate ausente para a pergunta: {current_question}")
            rate = rates[current_question]
            if not math.isfinite(rate) or not MIN_LEARNING_RATE <= rate <= MAX_LEARNING_RATE:
                raise ValueError(f"Learning rate inválido para a pergunta: {current_question}")
            begin = offset + leading + 3
            end = offset + len(line)
            if begin < end:
                ranges.append((begin, end, rate))
            found.add(current_question)
            current_question = None
        offset += len(line)
    if found != set(rates):
        missing = sorted(set(rates) - found)
        raise RuntimeError(f"Perguntas sem intervalo de treinamento: {missing}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_name(destination.name + ".tmp")
    temporary.write_text("".join(f"{begin} {end} {rate:.12g}\n" for begin, end, rate in ranges),
                         encoding="ascii")
    os.replace(temporary, destination)
    return len(ranges)


def save_best_state(checkpoint: Path, optimizer_state: Path,
                    best_checkpoint: Path, best_optimizer_state: Path) -> None:
    """Salva pesos e momentos Adam do melhor estado pela Loss de treinamento."""
    if not checkpoint.is_file() or not optimizer_state.is_file():
        raise RuntimeError("Checkpoint ou estado Adam ausente após treinamento.")
    checkpoint_tmp = best_checkpoint.with_name(best_checkpoint.name + ".tmp")
    optimizer_tmp = best_optimizer_state.with_name(best_optimizer_state.name + ".tmp")
    shutil.copy2(checkpoint, checkpoint_tmp)
    shutil.copy2(optimizer_state, optimizer_tmp)
    os.replace(checkpoint_tmp, best_checkpoint)
    os.replace(optimizer_tmp, best_optimizer_state)


def restore_best_state(best_checkpoint: Path, best_optimizer_state: Path,
                       checkpoint: Path, optimizer_state: Path) -> None:
    """Restaura pesos e momentos Adam do mesmo ciclo vencedor."""
    if not best_checkpoint.is_file() or not best_optimizer_state.is_file():
        raise RuntimeError("Nenhum estado de melhor validação foi salvo.")
    shutil.copy2(best_checkpoint, checkpoint)
    shutil.copy2(best_optimizer_state, optimizer_state)


def should_stop(continuous: bool, correct_count: int, total: int) -> bool:
    """No modo contínuo, encerra assim que a questão selecionada estiver correta."""
    return continuous and total > 0 and correct_count == total


def resumable_state_exists(checkpoint: Path, optimizer_state: Path) -> bool:
    """Exige pesos e estado Adam juntos para retomar sem perder o histórico do otimizador."""
    checkpoint_exists = checkpoint.is_file()
    optimizer_exists = optimizer_state.is_file()
    if checkpoint_exists != optimizer_exists:
        raise RuntimeError(
            "Estado persistente incompleto: checkpoint e arquivo .adam precisam existir juntos."
        )
    if checkpoint_exists and (
        checkpoint.stat().st_size == 0 or optimizer_state.stat().st_size == 0
    ):
        raise RuntimeError("Estado persistente vazio; recusando reinicialização silenciosa.")
    return checkpoint_exists


def normalize(text: str) -> list[str]:
    text = unicodedata.normalize("NFKD", text.casefold())
    text = "".join(ch for ch in text if not unicodedata.combining(ch))
    return re.findall(r"[a-z0-9]+", text)


def evaluate(answer: str, expected: str) -> tuple[bool, float]:
    """F1 lexical por frequência; não deve ser interpretado como avaliação semântica."""
    actual = normalize(answer)
    reference = normalize(expected)
    if not actual or not reference:
        return False, 0.0
    overlap = sum((Counter(actual) & Counter(reference)).values())
    precision = overlap / len(actual)
    recall = overlap / len(reference)
    score = 2 * precision * recall / (precision + recall) if precision + recall else 0.0
    return actual == reference, score



def character_similarity(answer: str, expected: str) -> float:
    """Similaridade de edição normalizada; diagnóstico, não critério de aprovação."""
    actual = " ".join(normalize(answer))
    reference = " ".join(normalize(expected))
    if not actual or not reference:
        return 0.0
    return difflib.SequenceMatcher(None, actual, reference, autojunk=False).ratio()


def benchmark_category(question: str) -> str:
    """Agrupa perguntas por habilidade para expor onde ocorre a falha."""
    tokens = normalize(question)
    if any(word in tokens for word in ("compare", "relacao", "diferenca")):
        return "composicao"
    if any(word in tokens for word in ("explique", "defina", "significa", "como")) or {"que", "e"}.issubset(tokens):
        return "conceito"
    if any(word in tokens for word in ("quantos", "quantas", "qual", "quais", "quem", "onde")):
        return "fato"
    return "conversa"


def generation_diagnostics(answer: str) -> tuple[float, float]:
    """Retorna proporção de repetição consecutiva e diversidade de caracteres."""
    if not answer:
        return 0.0, 0.0
    longest_run = 1
    current_run = 1
    for previous, current in zip(answer, answer[1:]):
        current_run = current_run + 1 if current == previous else 1
        longest_run = max(longest_run, current_run)
    repetition = longest_run / len(answer)
    diversity = len(set(answer)) / len(answer)
    return repetition, diversity


def read_dialogues(corpus: Path) -> list[tuple[str, str]]:
    dialogues = []
    question = None
    for raw in corpus.read_text(encoding="utf-8-sig").splitlines():
        line = raw.strip()
        if line.startswith("U:"):
            question = line[2:].strip()
        elif line.startswith("IA:") and question is not None:
            answer = line[3:].strip()
            if answer:
                dialogues.append((question, answer))
            question = None
    if not dialogues:
        raise RuntimeError(f"Nenhum par U:/IA: completo encontrado em {corpus}.")
    return dialogues


def load_feedback_memory(path: Path) -> dict[str, str]:
    """Carrega correções acumuladas; perguntas repetidas precisam ter o mesmo gabarito."""
    if not path.exists() or path.stat().st_size == 0:
        return {}
    entries = read_dialogues(path)
    memory: dict[str, str] = {}
    for question, expected in entries:
        previous = memory.get(question)
        if previous is not None and previous != expected:
            raise RuntimeError(f"Memória corretiva contém gabaritos conflitantes: {question}")
        memory[question] = expected
    return memory


def record_feedback_errors(memory: dict[str, str],
                             dialogues: list[tuple[str, str]],
                             results: list[tuple[str, bool, float]]) -> int:
    """Guarda respostas incorretas como exemplos supervisionados sem duplicar perguntas."""
    if len(dialogues) != len(results):
        raise ValueError("Quantidade de diálogos e resultados de feedback não corresponde.")
    added = 0
    for (question, expected), result in zip(dialogues, results):
        _, correct, _ = result[:3]
        if correct:
            continue
        previous = memory.get(question)
        if previous is not None and previous != expected:
            raise RuntimeError(f"Correção conflitante para a pergunta: {question}")
        if previous is None:
            memory[question] = expected
            added += 1
    return added


def load_question_history(path: Path) -> dict:
    """Carrega histórico auditável e contador global persistente de ciclos."""
    if not path.exists():
        return {"version": 1, "global_cycles": 0, "questions": {}}
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise RuntimeError("Histórico persistente de perguntas inválido.") from error
    if not isinstance(data, dict) or data.get("version") != 1 or not isinstance(data.get("questions"), dict):
        raise RuntimeError("Formato do histórico persistente de perguntas incompatível.")
    # Compatibilidade com históricos anteriores à introdução do contador global.
    data.setdefault("global_cycles", 0)
    if (not isinstance(data["global_cycles"], int) or
            isinstance(data["global_cycles"], bool) or data["global_cycles"] < 0):
        raise RuntimeError("Contador global de ciclos inválido.")
    return data


def next_global_cycle(history: dict) -> int:
    """Avança e devolve o número global do ciclo, persistido entre execuções."""
    current = history.get("global_cycles", 0)
    if not isinstance(current, int) or isinstance(current, bool) or current < 0:
        raise RuntimeError("Contador global de ciclos inválido.")
    history["global_cycles"] = current + 1
    return history["global_cycles"]


def checkpoint_sha256(path: Path) -> str | None:
    """Calcula SHA-256 de um arquivo de estado sem carregá-lo inteiro na memória."""
    if not path.is_file():
        return None
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def update_question_history(history: dict, dialogues, results, cycle: int,
                            execution_id: str, dataset: str = "training") -> tuple[int, int]:
    """Conta regressões e recuperações sem descartar respostas ou resultados anteriores."""
    if len(dialogues) != len(results):
        raise ValueError("Quantidade de diálogos e resultados do histórico não corresponde.")
    regressions = 0
    recoveries = 0
    questions = history.setdefault("questions", {})
    for (question, expected), result in zip(dialogues, results):
        answer, correct, lexical_score = result[:3]
        entry = questions.setdefault(question, {
            "expected": expected,
            "dataset": dataset,
            "observations": 0,
            "regressions": 0,
            "recoveries": 0,
            "ever_correct": False,
            "history": [],
        })
        if entry.get("expected") != expected or entry.get("dataset", dataset) != dataset:
            raise RuntimeError(f"Gabarito ou conjunto mudou para uma pergunta já monitorada: {question}")
        previous = entry.get("last_correct")
        if previous is True and not correct:
            regressions += 1
            entry["regressions"] = int(entry.get("regressions", 0)) + 1
        elif previous is False and correct:
            recoveries += 1
            entry["recoveries"] = int(entry.get("recoveries", 0)) + 1
        entry["observations"] = int(entry.get("observations", 0)) + 1
        observations = entry.setdefault("history", [])
        observations.append({
            "execution": execution_id,
            "cycle": int(cycle),
            "correct": bool(correct),
            "answer": answer,
            "lexical_f1": float(lexical_score),
            "dataset": dataset,
        })
        del observations[:-200]
        entry["ever_correct"] = bool(entry.get("ever_correct")) or bool(correct)
        entry["last_correct"] = bool(correct)
        entry["last_answer"] = answer
        entry["last_f1"] = float(lexical_score)
        entry["last_cycle"] = int(cycle)
        entry["last_execution"] = execution_id
    return regressions, recoveries


def write_question_history(path: Path, history: dict) -> None:
    """Grava o histórico de perguntas atomicamente."""
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(
        json.dumps(history, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    os.replace(temporary, path)


def write_feedback_memory(path: Path, memory: dict[str, str]) -> None:
    """Grava a memória corretiva atomicamente no formato U:/IA:."""
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    with temporary.open("w", encoding="utf-8", newline="\n") as output:
        for question, answer in sorted(memory.items()):
            q = " ".join(question.replace("\r", " ").replace("\n", " ").split())
            a = " ".join(answer.replace("\r", " ").replace("\n", " ").split())
            if not q or not a:
                raise RuntimeError("Memória corretiva contém pergunta ou resposta vazia.")
            output.write(f"U: {q}\nIA: {a}\n\n")
    os.replace(temporary, path)


def build_training_corpus(base: Path, memory: dict[str, str], destination: Path) -> None:
    """Mantém exatamente as 40 perguntas; a memória substitui gabaritos, não adiciona perguntas."""
    dialogues = read_dialogues(base)
    questions = {question for question, _ in dialogues}
    unknown = set(memory) - questions
    if unknown:
        raise RuntimeError(f"Memória contém perguntas fora do conjunto de treinamento: {sorted(unknown)}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_name(destination.name + ".tmp")
    with temporary.open("w", encoding="utf-8", newline="\n") as output:
        for question, original_answer in dialogues:
            answer = memory.get(question, original_answer)
            q = " ".join(question.replace("\r", " ").replace("\n", " ").split())
            a = " ".join(answer.replace("\r", " ").replace("\n", " ").split())
            if not q or not a:
                raise RuntimeError("Corpus contém pergunta ou resposta vazia.")
            output.write(f"U: {q}\nIA: {a}\n\n")
    os.replace(temporary, destination)

def extract_training_metric(output: str, name: str) -> float:
    match = re.search(rf"^{re.escape(name)}:\s*([0-9.eE+-]+)\s*$",
                      output, flags=re.MULTILINE)
    if not match:
        raise RuntimeError(f"O treinador não informou {name}.")
    value = float(match.group(1))
    if not math.isfinite(value):
        raise RuntimeError(f"A métrica {name} não é finita.")
    return value


def evaluate_set(generator: Path, trainer: Path, checkpoint: Path,
                 dialogues: list[tuple[str, str]]) -> list[tuple[str, bool, float, float, float, int]]:
    prompts = "".join(
        question.replace("\n", " ").replace("\r", " ") + "\n"
        for question, _ in dialogues
    )
    process = subprocess.run(
        [str(generator), "--batch", str(checkpoint), str(MAX_RESPONSE_BYTES)],
        input=prompts, check=True, capture_output=True, text=True, encoding="utf-8",
    )
    generated = process.stdout.splitlines()
    if len(generated) != len(dialogues):
        raise RuntimeError(
            f"Gerador retornou {len(generated)} respostas para {len(dialogues)} perguntas."
        )

    with tempfile.TemporaryDirectory(prefix="tsukuyomi-pergunta-") as directory:
        corpus = Path(directory) / "perguntas.txt"
        corpus.write_text(
            "".join(f"U: {question}\nIA: {expected}\n\n" for question, expected in dialogues),
            encoding="utf-8",
        )
        evaluated = subprocess.run(
            [str(trainer), "--evaluate-each", str(corpus), str(checkpoint)],
            check=True, capture_output=True, text=True, encoding="utf-8",
        )
    losses = []
    for line in evaluated.stdout.splitlines():
        fields = line.split("\t")
        if len(fields) != 4:
            raise RuntimeError(f"Saída de Loss individual inválida: {line!r}")
        index, loss, bpb, targets = int(fields[0]), float(fields[1]), float(fields[2]), int(fields[3])
        if index != len(losses) + 1 or not all(math.isfinite(value) for value in (loss, bpb)) or targets <= 0:
            raise RuntimeError("Métricas individuais fora de ordem ou inválidas.")
        losses.append((loss, bpb, targets))
    if len(losses) != len(dialogues):
        raise RuntimeError(f"Loss individual retornou {len(losses)} itens para {len(dialogues)} perguntas.")
    return [
        (answer.strip(), *evaluate(answer.strip(), expected), *losses[index])
        for index, ((_, expected), answer) in enumerate(zip(dialogues, generated))
    ]




def write_results(report, title: str, dialogues, results) -> tuple[int, int]:
    correct_items = []
    incorrect_items = []
    category_metrics = defaultdict(list)
    all_metrics = []
    for index, (dialogue, result) in enumerate(zip(dialogues, results), start=1):
        question, expected = dialogue
        answer, correct, score, loss, bpb, target_bytes = result
        char_score = character_similarity(answer, expected)
        repetition, diversity = generation_diagnostics(answer)
        rate = learning_rate_for_lexical_level(INITIAL_LEARNING_RATE, score)
        metrics = (score, char_score, repetition, diversity, loss, bpb)
        all_metrics.append(metrics)
        category_metrics[benchmark_category(question)].append((correct, *metrics))
        item = (index, question, expected, answer, score, char_score, repetition, diversity, loss, bpb, target_bytes, rate)
        (correct_items if correct else incorrect_items).append(item)

    total = max(1, len(results))
    mean = lambda column: sum(item[column] for item in all_metrics) / total
    report.write(f"## {title} — resultado individual das {len(results)} perguntas\n\n")
    report.write(f"- Success: {len(correct_items)}\n")
    report.write(f"- Error: {len(incorrect_items)}\n")
    report.write(f"- Total individual: {len(results)}\n")
    report.write(f"- F1 lexical médio: {mean(0):.1%}\n")
    report.write(f"- Loss média por pergunta: {mean(4):.6f}\n")
    report.write(f"- BPB médio por pergunta: {mean(5):.6f}\n\n")

    for item in sorted(correct_items + incorrect_items, key=lambda row: row[0]):
        (index, question, expected, answer, score, char_score, repetition,
         diversity, loss, bpb, target_bytes, rate) = item
        correct = item in correct_items
        status = "Success" if correct else "Error"
        color = "green" if correct else "red"
        ansi = "\033[32m" if correct else "\033[31m"
        reset = "\033[0m"
        print(f"{ansi}{status}: Pergunta {index:02d} — {question}{reset}", flush=True)
        report.write(f'<span style="color: {color}">**{status}: Pergunta {index:02d} — {question}**</span>\n\n')
        report.write(f"- Resposta gerada: {answer or '(vazia)'}\n")
        report.write(f"- Gabarito: {expected}\n")
        report.write(f"- Nível lexical (F1): {score:.2%}\n")
        report.write(f"- Learning rate individual: {rate:.8g}\n")
        report.write(f"- Loss individual: {loss:.8f}\n")
        report.write(f"- BPB individual: {bpb:.8f}\n")
        report.write(f"- Bytes-alvo da resposta: {target_bytes}\n")
        report.write(f"- Similaridade de caracteres: {char_score:.2%}\n")
        report.write(f"- Repetição consecutiva: {repetition:.2%}\n")
        report.write(f"- Diversidade de caracteres: {diversity:.2%}\n\n")

    report.write("### Métricas individuais por habilidade\n\n")
    report.write("| Habilidade | Perguntas | Success | Loss média | BPB médio | Nível lexical médio |\n")
    report.write("|---|---:|---:|---:|---:|---:|\n")
    for category, values in sorted(category_metrics.items()):
        n = len(values)
        report.write(
            f"| {category} | {n} | {sum(v[0] for v in values)} | "
            f"{sum(v[5] for v in values) / n:.6f} | "
            f"{sum(v[6] for v in values) / n:.6f} | "
            f"{sum(v[1] for v in values) / n:.2%} |\n"
        )
    report.write("\n")
    return len(correct_items), len(results)




def main() -> int:
    if len(sys.argv) != 7:
        print("Uso: treinamento_ciclico.py <treinador> <gerador> <corpus-40-perguntas> <epocas> <tentativas> <continuo:0|1>", file=sys.stderr)
        return 2
    trainer, generator, training_corpus = map(Path, sys.argv[1:4])
    epochs, attempts = int(sys.argv[4]), int(sys.argv[5])
    continuous = sys.argv[6] == "1"
    if not 1 <= epochs <= MAX_EPOCHS_PER_CYCLE or not 1 <= attempts <= MAX_CYCLES:
        raise ValueError(f"Épocas precisam estar entre 1 e {MAX_EPOCHS_PER_CYCLE}; tentativas entre 1 e {MAX_CYCLES}.")
    if sys.argv[6] not in ("0", "1"):
        raise ValueError("O modo contínuo precisa ser 0 ou 1.")

    training_dialogues = read_dialogues(training_corpus)
    if len(training_dialogues) != TRAINING_QUESTIONS:
        raise RuntimeError(f"O treinamento precisa conter exatamente {TRAINING_QUESTIONS} perguntas; foram encontradas {len(training_dialogues)}.")

    checkpoint = Path("build/g2/tsukuyomi-treinamento-ciclico.ckpt")
    optimizer_state = Path(str(checkpoint) + ".adam")
    best_checkpoint = Path(str(checkpoint) + ".best")
    best_optimizer_state = Path(str(optimizer_state) + ".best")
    output = Path("build/g2/respostas-tsukuyomi.md")
    output.parent.mkdir(parents=True, exist_ok=True)
    feedback_path = Path("build/g2/tsukuyomi-memoria-erros.txt")
    combined_training = Path("build/g2/treinamento-com-memoria.txt")
    rate_path = Path("build/g2/tsukuyomi-learning-rates-by-question.txt")
    history_path = Path("build/g2/tsukuyomi-historico-perguntas.json")
    memory = load_feedback_memory(feedback_path)
    history = load_question_history(history_path)
    # Migra o histórico antigo: preserva apenas as 40 perguntas que agora são treinadas.
    active_questions = {question for question, _ in training_dialogues}
    history_questions = history.setdefault("questions", {})
    history["questions"] = {
        question: entry for question, entry in history_questions.items()
        if question in active_questions
    }
    for entry in history["questions"].values():
        entry["dataset"] = "training"
    resumed = resumable_state_exists(checkpoint, optimizer_state)
    if not resumed:
        for stale in (best_checkpoint, best_optimizer_state):
            if stale.exists():
                stale.unlink()
        history = {"version": 1, "global_cycles": 0, "questions": {}}
    else:
        save_best_state(checkpoint, optimizer_state, best_checkpoint, best_optimizer_state)

    execution_id = os.environ.get("GITHUB_RUN_ID", "local")
    max_cycles = min(attempts, MAX_CYCLES)
    best_correct = 0
    with output.open("w", encoding="utf-8") as report:
        report.write("# Treinamento — Tsukuyomi G2 0.1\n\n")
        report.write("- Somente as 40 perguntas deste corpus são usadas.\n")
        report.write("- Não existe validação independente nem conjunto de perguntas reservadas.\n")
        report.write("- A seleção de checkpoint usa a Loss de treino: menor é melhor.\n")
        report.write("- Pesos e estado Adam são aceitos/restaurados juntos.\n")
        report.write(f"- Ciclos globais anteriores: {history['global_cycles']}\n")
        report.write(f"- Épocas por ciclo: {epochs}; máximo de ciclos: {max_cycles}.\n\n")

        if resumed:
            baseline = evaluate_set(generator, trainer, checkpoint, training_dialogues)
            best_correct, _ = write_results(report, "Estado retomado — 40 perguntas", training_dialogues, baseline)
            update_question_history(history, training_dialogues, baseline, 0, execution_id, dataset="training")
            write_question_history(history_path, history)

        cycle = 0
        while cycle < max_cycles:
            cycle += 1
            global_cycle = next_global_cycle(history)
            write_question_history(history_path, history)
            seed = 42 + global_cycle
            if best_checkpoint.is_file() and best_optimizer_state.is_file():
                restore_best_state(best_checkpoint, best_optimizer_state, checkpoint, optimizer_state)

            build_training_corpus(training_corpus, memory, combined_training)
            current_dialogues = read_dialogues(combined_training)
            if len(current_dialogues) != TRAINING_QUESTIONS:
                raise RuntimeError("A memória alterou a quantidade de perguntas de treinamento.")
            probe = evaluate_set(generator, trainer, checkpoint, current_dialogues) if checkpoint.is_file() else [("", False, 0.0, 0.0, 0.0, 0)] * TRAINING_QUESTIONS
            rates = {
                question: learning_rate_for_lexical_level(MAX_LEARNING_RATE, result[2])
                for (question, _), result in zip(current_dialogues, probe)
            }
            write_question_learning_rates(combined_training, rates, rate_path)
            individual_rates = list(rates.values())
            report.write(f"### LR individual — ciclo {cycle}\n\n")
            report.write("| Pergunta | Nível lexical | LR |\n|---|---:|---:|\n")
            for (question, _), result in zip(current_dialogues, probe):
                report.write(f"| {question.replace('|', '&#124;')} | {result[2]:.1%} | {rates[question]:.8g} |\n")
            report.write("\n")

            weights_before = checkpoint_sha256(checkpoint)
            adam_before = checkpoint_sha256(optimizer_state)
            command = [str(trainer), str(combined_training), str(checkpoint), str(epochs), str(seed), f"{INITIAL_LEARNING_RATE:.12g}", "--lr-map", str(rate_path)]
            if checkpoint.is_file():
                command.append("--resume")
            print(f"=== Ciclo {cycle}/{max_cycles}: treinando as 40 perguntas ===", flush=True)
            started = time.perf_counter()
            proc = subprocess.run(command, check=True, capture_output=True, text=True, encoding="utf-8")
            print(proc.stdout, end="", flush=True)

            initial_bpb = extract_training_metric(proc.stdout, "train_bpb inicial")
            final_bpb = extract_training_metric(proc.stdout, "train_bpb final")
            initial_loss = extract_training_metric(proc.stdout, "Loss inicial")
            final_loss = extract_training_metric(proc.stdout, "Loss final")
            target_bytes = extract_training_metric(proc.stdout, "Bytes-alvo")
            completed_epochs = extract_training_metric(proc.stdout, "Épocas")
            if target_bytes <= 0 or completed_epochs != epochs:
                raise RuntimeError("O treinador não concluiu todas as épocas ou não processou alvos.")
            if not checkpoint.is_file() or checkpoint.stat().st_size == 0 or not optimizer_state.is_file() or optimizer_state.stat().st_size == 0:
                raise RuntimeError("Checkpoint ou estado Adam inválido.")

            weights_after = checkpoint_sha256(checkpoint)
            adam_after = checkpoint_sha256(optimizer_state)
            candidate_results = evaluate_set(generator, trainer, checkpoint, training_dialogues)
            new_errors = record_feedback_errors(memory, training_dialogues, candidate_results)
            write_feedback_memory(feedback_path, memory)
            regressions, recoveries = update_question_history(history, training_dialogues, candidate_results, global_cycle, execution_id, dataset="training")
            write_question_history(history_path, history)
            correct, total = write_results(report, f"Treinamento — ciclo {cycle}", training_dialogues, candidate_results)
            best_correct = correct

            has_best = best_checkpoint.is_file() and best_optimizer_state.is_file()
            accepted = not has_best or final_loss < initial_loss - 1e-12
            if accepted:
                save_best_state(checkpoint, optimizer_state, best_checkpoint, best_optimizer_state)
                best_correct = correct
            else:
                restore_best_state(best_checkpoint, best_optimizer_state, checkpoint, optimizer_state)
                restored_results = evaluate_set(generator, checkpoint, training_dialogues)
                best_correct = sum(1 for result in restored_results if result[1])

            print(f"Loss de treino: {initial_loss:.6f} → {final_loss:.6f}; checkpoint {'ACEITO' if accepted else 'REJEITADO'}.", flush=True)
            report.write(f"- Loss de treino: {initial_loss:.6f} → {final_loss:.6f}.\n")
            report.write(f"- Checkpoint {'ACEITO' if accepted else 'REJEITADO'} pela redução da Loss de treino.\n")
            report.write(f"- Respostas exatas nas 40 perguntas: {correct}/{total}.\n")
            report.write(f"- Novas correções armazenadas: {new_errors}.\n")
            report.write(f"- Regressões/recuperações históricas: {regressions}/{recoveries}.\n")

            if os.environ.get("TSUKUYOMI_PERSIST_STATE") == "1":
                persistence = Path(__file__).with_name("persistir_estado.sh")
                if not persistence.is_file():
                    raise RuntimeError(f"Script de persistência não encontrado: {persistence}")
                subprocess.run(["bash", str(persistence)], check=True)

            elapsed = time.perf_counter() - started
            report.write(f"- SHA-256 pesos: {weights_before or 'inexistente'} → {weights_after}.\n")
            report.write(f"- SHA-256 Adam: {adam_before or 'inexistente'} → {adam_after}.\n")
            report.write(f"- Épocas: {completed_epochs:.0f}; bytes-alvo: {target_bytes:.0f}.\n")
            report.write(f"- train_bpb: {initial_bpb:.4f} → {final_bpb:.4f}.\n")
            report.write(f"- LR individual observado: {min(individual_rates):.8g}–{max(individual_rates):.8g}.\n")
            report.write(f"- Duração: {elapsed:.2f}s.\n\n")
            report.flush()

            if should_stop(continuous, best_correct, TRAINING_QUESTIONS):
                report.write("Todas as 40 perguntas estão corretas; treinamento contínuo encerrado.\n")
                break

        if best_checkpoint.is_file() and best_optimizer_state.is_file():
            restore_best_state(best_checkpoint, best_optimizer_state, checkpoint, optimizer_state)
        if continuous and best_correct < TRAINING_QUESTIONS:
            raise RuntimeError(
                f"Objetivo contínuo não atingido: {best_correct}/{TRAINING_QUESTIONS} "
                f"após {cycle} ciclos."
            )
        final_results = evaluate_set(generator, checkpoint, training_dialogues)
        final_correct, final_total = write_results(report, "Resultado final — 40 perguntas treinadas", training_dialogues, final_results)
        report.write(f"**Resultado final: {final_correct}/{final_total} respostas exatas nas 40 perguntas treinadas.**\n")
        print(f"Resultado final nas 40 perguntas: {final_correct}/{final_total}.", flush=True)

    print(f"Relatório salvo em: {output}")
    print(f"Checkpoint final: {checkpoint}")
    print("LR individual por nível lexical; sem scheduler global.")
    print(f"Histórico de perguntas: {history_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
