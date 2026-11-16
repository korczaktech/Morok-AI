#!/usr/bin/env python3
"""Treinamento cíclico com validação e teste independentes do Tsukuyomi G2 0.1."""

from collections import Counter, defaultdict
import difflib
import json
import math
import os
from pathlib import Path
import re
import subprocess
import shutil
import sys
import time
import unicodedata

MAX_RESPONSE_BYTES = 160
MAX_VALIDATION_QUESTIONS = 20
MAX_FEEDBACK_QUESTIONS = 40
MAX_CYCLES = 100
MAX_EPOCHS_PER_CYCLE = 1000
MIN_LEARNING_RATE = 1e-5
MAX_LEARNING_RATE = 1e-2
# LR-base fixo e interno; a configuração externa do workflow é ignorada.
INITIAL_LEARNING_RATE = 1e-3
LR_REDUCE_FACTOR = 0.9


def validation_rank(correct_count: int, total: int, mean_f1: float,
                    mean_char_similarity: float) -> tuple[int, float]:
    """Ordena primeiro por respostas exatas; métricas aproximadas só desempatem."""
    if total <= 0 or not 0 <= correct_count <= total:
        raise ValueError("Contagem de validação inválida.")
    if not 0.0 <= mean_f1 <= 1.0 or not 0.0 <= mean_char_similarity <= 1.0:
        raise ValueError("Métricas de validação precisam estar entre 0 e 1.")
    if not math.isfinite(mean_f1) or not math.isfinite(mean_char_similarity):
        raise ValueError("Métricas de validação precisam ser finitas.")
    return correct_count, 0.5 * mean_f1 + 0.5 * mean_char_similarity


def decay_learning_rate(current: float) -> float:
    """Reduz 10% a cada ciclo, respeitando o piso absoluto de 1e-5."""
    if (not math.isfinite(current) or current < MIN_LEARNING_RATE
            or current > MAX_LEARNING_RATE):
        raise ValueError("Learning rate atual inválido.")
    return max(MIN_LEARNING_RATE, current * LR_REDUCE_FACTOR)


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


def load_learning_rate(path: Path, fallback: float) -> float:
    """Retoma o scheduler persistido; estado presente e inválido nunca é ignorado."""
    if not path.exists():
        return fallback
    try:
        value = float(path.read_text(encoding="utf-8").strip())
    except (OSError, ValueError) as error:
        raise RuntimeError("Estado persistente do learning rate inválido.") from error
    if not math.isfinite(value) or value <= 0.0:
        raise RuntimeError("Learning rate persistido inválido.")
    # Normaliza schedulers antigos para os novos limites sem perder a retomada.
    return min(MAX_LEARNING_RATE, max(MIN_LEARNING_RATE, value))


def write_learning_rate(path: Path, value: float) -> None:
    """Persiste atomicamente o próximo learning rate."""
    if not math.isfinite(value) or not MIN_LEARNING_RATE <= value <= MAX_LEARNING_RATE:
        raise ValueError("Learning rate a persistir inválido.")
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(f"{value:.12g}\n", encoding="utf-8")
    os.replace(temporary, path)


def validation_metrics(dialogues, results):
    """Calcula métricas de seleção exclusivamente sobre a validação independente."""
    if not dialogues or len(dialogues) != len(results):
        raise ValueError("Conjunto de validação vazio ou com resultados incompatíveis.")
    correct = sum(1 for result in results if result[1])
    total = len(results)
    mean_f1 = sum(result[2] for result in results) / total
    mean_char = sum(
        character_similarity(result[0], expected)
        for (_, expected), result in zip(dialogues, results)
    ) / total
    score = 0.70 * (correct / total) + 0.15 * mean_f1 + 0.15 * mean_char
    if not math.isfinite(score):
        raise RuntimeError("A pontuação de validação não é finita.")
    return correct, total, mean_f1, mean_char, score, validation_rank(
        correct, total, mean_f1, mean_char
    )


def save_best_state(checkpoint: Path, optimizer_state: Path,
                    best_checkpoint: Path, best_optimizer_state: Path) -> None:
    """Salva pesos e momentos Adam como um único estado lógico de melhor validação."""
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


def select_validation_questions(train_dialogues, validation_pool, count):
    """Seleciona apenas perguntas do conjunto independente de validação."""
    del train_dialogues
    if not 1 <= count <= MAX_VALIDATION_QUESTIONS:
        raise ValueError("A quantidade de questões de validação precisa estar entre 1 e 20.")
    return validation_pool[:count]


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
    for (question, expected), (_, correct, _) in zip(dialogues, results):
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
    """Carrega histórico auditável de acertos, regressões e recuperações por pergunta."""
    if not path.exists():
        return {"version": 1, "questions": {}}
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise RuntimeError("Histórico persistente de perguntas inválido.") from error
    if not isinstance(data, dict) or data.get("version") != 1 or not isinstance(data.get("questions"), dict):
        raise RuntimeError("Formato do histórico persistente de perguntas incompatível.")
    return data


def find_retention_regressions(previous_results, current_dialogues, current_results) -> list[str]:
    """Identifica perguntas corretas no melhor estado que regrediram no candidato."""
    if previous_results is None:
        return []
    if len(current_dialogues) != len(current_results) or len(previous_results) != len(current_results):
        raise ValueError("Resultados candidatos incompatíveis com o holdout.")
    previous_correct = {
        question for (question, _), result in zip(current_dialogues, previous_results)
        if result[1]
    }
    current_correct = {
        question for (question, _), result in zip(current_dialogues, current_results)
        if result[1]
    }
    return sorted(previous_correct - current_correct)


def update_question_history(history: dict, dialogues, results, cycle: int,
                            execution_id: str, dataset: str = "validation") -> tuple[int, int]:
    """Conta regressões e recuperações sem descartar respostas ou resultados anteriores."""
    if len(dialogues) != len(results):
        raise ValueError("Quantidade de diálogos e resultados do histórico não corresponde.")
    regressions = 0
    recoveries = 0
    questions = history.setdefault("questions", {})
    for (question, expected), (answer, correct, lexical_score) in zip(dialogues, results):
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
    """Combina o corpus original imutável com as correções acumuladas."""
    original = base.read_text(encoding="utf-8-sig").rstrip()
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_name(destination.name + ".tmp")
    with temporary.open("w", encoding="utf-8", newline="\n") as output:
        output.write(original)
        if memory:
            output.write("\n\n")
            for question, answer in sorted(memory.items()):
                q = " ".join(question.replace("\r", " ").replace("\n", " ").split())
                a = " ".join(answer.replace("\r", " ").replace("\n", " ").split())
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


def evaluate_set(generator: Path, checkpoint: Path,
                 dialogues: list[tuple[str, str]]) -> list[tuple[str, bool, float]]:
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
    return [
        (answer.strip(), *evaluate(answer.strip(), expected))
        for (_, expected), answer in zip(dialogues, generated)
    ]


def write_results(report, title: str, dialogues, results) -> tuple[int, int]:
    correct_items = []
    incorrect_items = []
    category_metrics = defaultdict(list)
    all_metrics = []
    for index, (dialogue, result) in enumerate(zip(dialogues, results), start=1):
        question, expected = dialogue
        answer, correct, score = result
        char_score = character_similarity(answer, expected)
        repetition, diversity = generation_diagnostics(answer)
        metrics = (score, char_score, repetition, diversity)
        all_metrics.append(metrics)
        category_metrics[benchmark_category(question)].append((correct, *metrics))
        item = (index, question, expected, answer, score, char_score, repetition, diversity)
        (correct_items if correct else incorrect_items).append(item)

    total = max(1, len(results))
    mean = lambda column: sum(item[column] for item in all_metrics) / total
    report.write(f"## {title}\n\n")
    report.write(f"- Respostas corretas: {len(correct_items)}\n")
    report.write(f"- Respostas incorretas: {len(incorrect_items)}\n")
    report.write(f"- Total avaliado: {len(results)}\n")
    report.write(f"- F1 lexical médio: {mean(0):.1%}\n")
    report.write(f"- Similaridade de caracteres média: {mean(1):.1%}\n")
    report.write(f"- Repetição consecutiva média: {mean(2):.1%}\n")
    report.write(f"- Diversidade média de caracteres: {mean(3):.1%}\n")
    report.write("- Aprovação exige igualdade lexical normalizada; as demais métricas são diagnósticas.\n\n")
    report.write("### Resultado por habilidade\n\n")
    report.write("| Habilidade | Exatidão | F1 lexical | Similaridade | Repetição | Diversidade |\n")
    report.write("|---|---:|---:|---:|---:|---:|\n")
    for category, values in sorted(category_metrics.items()):
        n = len(values)
        report.write(
            f"| {category} | {sum(v[0] for v in values) / n:.1%} | "
            f"{sum(v[1] for v in values) / n:.1%} | "
            f"{sum(v[2] for v in values) / n:.1%} | "
            f"{sum(v[3] for v in values) / n:.1%} | "
            f"{sum(v[4] for v in values) / n:.1%} |\n"
        )
    report.write("\n")

    for heading, items, verdict in (
        ("Respostas corretas", correct_items, "CORRETA"),
        ("Respostas incorretas", incorrect_items, "INCORRETA"),
    ):
        report.write(f"### {heading} ({len(items)})\n\n")
        if not items:
            report.write("Nenhuma resposta nesta categoria.\n\n")
            continue
        for index, question, expected, answer, score, char_score, repetition, diversity in items:
            report.write(f"#### {index}. {question}\n\n")
            report.write(f"- Resposta gerada: {answer or '(vazia)'}\n")
            report.write(f"- Gabarito: {expected}\n")
            report.write(f"- F1 lexical: {score:.1%}\n")
            report.write(f"- Similaridade de caracteres: {char_score:.1%}\n")
            report.write(f"- Maior sequência repetida: {repetition:.1%} da resposta\n")
            report.write(f"- Diversidade de caracteres: {diversity:.1%}\n")
            report.write(f"- Resultado: **{verdict}**\n\n")
    return len(correct_items), len(results)


def main() -> int:
    if len(sys.argv) != 11:
        print(
            "Uso: treinamento_ciclico.py <treinador> <gerador> <treino> <feedback> "
            "<validacao> <teste> <epocas> <tentativas> <continuo:0|1> <questoes:1..20>",
            file=sys.stderr,
        )
        return 2

    trainer, generator, train_corpus, feedback_corpus, validation_corpus, test_corpus = map(
        Path, sys.argv[1:7]
    )
    epochs, attempts = int(sys.argv[7]), int(sys.argv[8])
    continuous = sys.argv[9] == "1"
    question_count = int(sys.argv[10])
    if not 1 <= epochs <= MAX_EPOCHS_PER_CYCLE or not 1 <= attempts <= MAX_CYCLES:
        raise ValueError(f"Épocas precisam estar entre 1 e {MAX_EPOCHS_PER_CYCLE}; tentativas entre 1 e {MAX_CYCLES}.")
    if sys.argv[9] not in ("0", "1"):
        raise ValueError("O modo contínuo precisa ser 0 ou 1.")
    if not 1 <= question_count <= MAX_VALIDATION_QUESTIONS:
        raise ValueError("A quantidade de questões de validação precisa estar entre 1 e 20.")

    train_dialogues = read_dialogues(train_corpus)
    feedback_questions = read_dialogues(feedback_corpus)
    if len(feedback_questions) != MAX_FEEDBACK_QUESTIONS:
        raise RuntimeError(f"O conjunto de feedback precisa conter exatamente 40 questões; foram encontradas {len(feedback_questions)}.")
    validation_pool = read_dialogues(validation_corpus)
    if len(validation_pool) != MAX_VALIDATION_QUESTIONS:
        raise RuntimeError(f"O holdout independente precisa conter exatamente 20 questões; foram encontradas {len(validation_pool)}.")
    validation = select_validation_questions(train_dialogues, validation_pool, question_count)
    test = read_dialogues(test_corpus)
    checkpoint = Path("build/g2/tsukuyomi-treinamento-ciclico.ckpt")
    optimizer_state = Path(str(checkpoint) + ".adam")
    best_checkpoint = Path(str(checkpoint) + ".best")
    best_optimizer_state = Path(str(optimizer_state) + ".best")
    output = Path("build/g2/respostas-tsukuyomi.md")
    output.parent.mkdir(parents=True, exist_ok=True)
    feedback_path = Path("build/g2/tsukuyomi-memoria-erros.txt")
    combined_training = Path("build/g2/treinamento-com-memoria.txt")
    learning_rate_path = Path("build/g2/tsukuyomi-learning-rate.txt")
    question_learning_rate_path = Path("build/g2/tsukuyomi-learning-rates-by-question.txt")
    question_history_path = Path("build/g2/tsukuyomi-historico-perguntas.json")
    feedback_memory = load_feedback_memory(feedback_path)
    question_history = load_question_history(question_history_path)
    resume_previous_run = resumable_state_exists(checkpoint, optimizer_state)
    if not resume_previous_run:
        for stale_best in (best_checkpoint, best_optimizer_state):
            if stale_best.exists():
                stale_best.unlink()
        if learning_rate_path.exists():
            learning_rate_path.unlink()
        question_history = {"version": 1, "questions": {}}
    learning_rate = load_learning_rate(learning_rate_path, INITIAL_LEARNING_RATE)
    execution_id = os.environ.get("GITHUB_RUN_ID", "local")
    if resume_previous_run:
        print(
            "Estado persistente encontrado: retomando pesos e momentos Adam da execução anterior.",
            flush=True,
        )
    else:
        print("Nenhum estado persistente encontrado: inicialização do modelo.", flush=True)

    cycle = 0
    best_validation_score = -1.0
    best_validation_correct = 0
    best_validation_cycle = 0
    best_validation_rank = (-1, -1.0)
    maximum_cycles = min(attempts, MAX_CYCLES)
    with output.open("w", encoding="utf-8") as report:
        report.write("# Treinamento e avaliação — Tsukuyomi G2 0.1\n\n")
        report.write(f"- Diálogos de treinamento originais: {len(train_dialogues)}\n")
        report.write(f"- Correções persistidas antes desta execução: {len(feedback_memory)}\n")
        report.write("- Os conjuntos são arquivos separados: 40 perguntas de feedback supervisionado, 20 perguntas novas de validação independente e 12 perguntas de teste reservado.\n")
        report.write(f"- Perguntas de feedback: {len(feedback_questions)}\n")
        report.write(f"- Holdout independente de validação: {len(validation_pool)}\n")
        report.write(f"- Questões selecionadas para validação: {question_count}\n")
        report.write(f"- Épocas por tentativa: {epochs}\n")
        report.write(f"- Limite de épocas por ciclo: {MAX_EPOCHS_PER_CYCLE}\n")
        report.write(f"- Limite de ciclos por execução: {MAX_CYCLES}\n")
        report.write(f"- Máximo teórico nesta configuração: {epochs * maximum_cycles} épocas\n")
        report.write(f"- Modo contínuo: {'ativado' if continuous else 'desativado'}\n")
        report.write(f"- Limite de geração: {MAX_RESPONSE_BYTES} bytes\n")
        report.write("- Critério contínuo: encerrar quando todas as questões selecionadas estiverem corretas\n")
        report.write("- Critério de aprovação: igualdade lexical normalizada; não mede semântica\n")
        report.write("- F1 lexical, similaridade de caracteres, repetição e diversidade são diagnósticos; não aprovam respostas.\n")
        report.write("- Scheduler: ranking lexicográfico; exatidão primeiro, F1 e similaridade apenas em empate (delta 0,005).\n")
        report.write(f"- Learning rate retomado: {learning_rate:.8g}; redução fixa de 10% após cada ciclo, com piso de {MIN_LEARNING_RATE:.1e}.\n")
        report.write("- Cada ciclo começa pelo melhor par checkpoint + momentos Adam; o estado persistido anterior é avaliado antes de qualquer atualização.\n")
        report.write("- O conjunto de teste é consultado somente após o treinamento.\n\n")

        best_validation_results = None
        if resume_previous_run:
            best_validation_results = evaluate_set(generator, checkpoint, validation_pool)
            baseline_selected = best_validation_results[:question_count]
            (best_validation_correct, _, baseline_f1, baseline_char,
             best_validation_score, best_validation_rank) = validation_metrics(
                validation, baseline_selected
            )
            best_validation_cycle = 0
            save_best_state(
                checkpoint, optimizer_state, best_checkpoint, best_optimizer_state
            )
            baseline_feedback_results = evaluate_set(generator, checkpoint, feedback_questions)
            feedback_regressions, feedback_recoveries = update_question_history(
                question_history, feedback_questions, baseline_feedback_results, 0,
                execution_id, dataset="feedback"
            )
            regressions, recoveries = update_question_history(
                question_history, validation_pool, best_validation_results, 0, execution_id
            )
            write_question_history(question_history_path, question_history)
            print(
                f"Checkpoint anterior avaliado: {best_validation_correct}/{question_count}; "
                f"regressões históricas={regressions}; recuperações={recoveries}.",
                flush=True,
            )
            report.write(
                f"**Checkpoint anterior: {best_validation_correct}/{question_count}; "
                f"F1={baseline_f1:.3f}; similaridade={baseline_char:.3f}.**\n\n"
            )

        while cycle < maximum_cycles:
            cycle += 1
            if best_checkpoint.is_file() and best_optimizer_state.is_file():
                restore_best_state(
                    best_checkpoint, best_optimizer_state, checkpoint, optimizer_state
                )
            build_training_corpus(train_corpus, feedback_memory, combined_training)
            current_training_dialogues = read_dialogues(combined_training)
            if checkpoint.is_file():
                lexical_probe_results = evaluate_set(
                    generator, checkpoint, current_training_dialogues
                )
            else:
                lexical_probe_results = [("", False, 0.0)] * len(current_training_dialogues)
            question_rates = {
                question: learning_rate_for_lexical_level(MAX_LEARNING_RATE, result[2])
                for (question, _), result in zip(current_training_dialogues, lexical_probe_results)
            }
            rate_ranges = write_question_learning_rates(
                combined_training, question_rates, question_learning_rate_path
            )
            lexical_levels = [result[2] for result in lexical_probe_results]
            individual_rates = list(question_rates.values())
            print(
                f"LR adaptativo por questão: {rate_ranges} intervalos; "
                f"nível lexical médio={sum(lexical_levels) / max(1, len(lexical_levels)):.1%}; "
                f"LR calculado observado mínimo={min(individual_rates):.8g}; "
                f"máximo observado={max(individual_rates):.8g}; "
                f"faixa definida pela curva: mínimo={MIN_LEARNING_RATE:.8g}, "
                f"máximo={1e-3:.8g}.", flush=True
            )
            report.write(f"### Learning rate adaptativo — ciclo {cycle}\n\n")
            report.write(
                f"- Faixa da curva individual: mínimo={MIN_LEARNING_RATE:.8g}; "
                f"máximo={1e-3:.8g}.\n"
            )
            report.write(
                f"- Faixa observada neste ciclo: mínimo={min(individual_rates):.8g}; "
                f"máximo={max(individual_rates):.8g}.\n\n"
            )
            report.write("| Pergunta | Nível lexical atual | Learning rate individual |\n")
            report.write("|---|---:|---:|\n")
            for (question, _), result in zip(current_training_dialogues, lexical_probe_results):
                safe_question = question.replace("|", "&#124;")
                report.write(
                    f"| {safe_question} | {result[2]:.1%} | "
                    f"{question_rates[question]:.8g} |\n"
                )
            report.write("\n")
            command = [
                str(trainer), str(combined_training), str(checkpoint),
                str(epochs), "42", f"{INITIAL_LEARNING_RATE:.12g}", "--lr-map",
                str(question_learning_rate_path),
            ]
            if cycle > 1 or resume_previous_run or best_checkpoint.is_file():
                command.append("--resume")

            print(
                f"\n=== Ciclo {cycle}: {epochs} época(s), validação "
                f"{len(validation)} pergunta(s), LR máximo interno={MAX_LEARNING_RATE:.8g} ===",
                flush=True,
            )
            started = time.perf_counter()
            training_process = subprocess.run(
                command, check=True, capture_output=True, text=True, encoding="utf-8"
            )
            print(training_process.stdout, end="", flush=True)
            initial_bpb = extract_training_metric(training_process.stdout, "train_bpb inicial")
            final_bpb = extract_training_metric(training_process.stdout, "train_bpb final")
            target_bytes = extract_training_metric(training_process.stdout, "Bytes-alvo")
            completed_epochs = extract_training_metric(training_process.stdout, "Épocas")
            if target_bytes <= 0 or completed_epochs != epochs:
                raise RuntimeError("O treinador não concluiu todas as épocas ou não processou alvos.")
            if (not checkpoint.is_file() or checkpoint.stat().st_size == 0 or
                    not optimizer_state.is_file() or optimizer_state.stat().st_size == 0):
                raise RuntimeError("O treinador não publicou checkpoint e estado Adam válidos.")
            candidate_all_results = evaluate_set(generator, checkpoint, validation_pool)
            validation_results = candidate_all_results[:question_count]
            retention_regressions = find_retention_regressions(
                best_validation_results, validation_pool, candidate_all_results
            )
            feedback_results = evaluate_set(generator, checkpoint, feedback_questions)
            new_corrections = record_feedback_errors(
                feedback_memory, feedback_questions, feedback_results
            )
            write_feedback_memory(feedback_path, feedback_memory)
            feedback_history_regressions, feedback_history_recoveries = update_question_history(
                question_history, feedback_questions, feedback_results, cycle,
                execution_id, dataset="feedback"
            )
            regressions, recoveries = update_question_history(
                question_history, validation_pool, candidate_all_results, cycle, execution_id
            )
            write_question_history(question_history_path, question_history)
            print(
                f"Memória corretiva: {new_corrections} nova(s) correção(ões); "
                f"{len(feedback_memory)} pergunta(s) acumulada(s).",
                flush=True,
            )
            print(
                f"Histórico da validação: {regressions} regressão(ões), "
                f"{recoveries} recuperação(ões) neste ciclo; "
                f"regressões contra o melhor checkpoint: {len(retention_regressions)}; "
                f"feedback regressões={feedback_history_regressions}, "
                f"recuperações={feedback_history_recoveries}.",
                flush=True,
            )
            total_questions = len(validation_results)
            for question_index, ((_, expected_answer), (generated_answer, is_correct, lexical_score)) in enumerate(
                zip(validation, validation_results), start=1
            ):
                status = "Success" if is_correct else "Error"
                verdict = "CORRETA" if is_correct else "INCORRETA"
                print(
                    f"{status}: Questão {question_index}/{total_questions} | "
                    f"Resposta {verdict} | Nivel lexical: {lexical_score * 100:.1f}%",
                    flush=True,
                )
            correct_count, total = write_results(
                report, f"Validação — ciclo {cycle}", validation, validation_results
            )
            report.write(
                f"- Memória corretiva acumulada após o ciclo: {len(feedback_memory)} pergunta(s).\n"
            )
            (correct_count, total, mean_f1, mean_char_similarity,
             validation_score, observed_rank) = validation_metrics(
                validation, validation_results
            )
            if not retention_regressions and (
                observed_rank[0] > best_validation_rank[0] or (
                    observed_rank[0] == best_validation_rank[0]
                    and observed_rank[1] > best_validation_rank[1] + 0.005
                )
            ):
                best_validation_rank = observed_rank
                save_best_state(checkpoint, optimizer_state,
                                best_checkpoint, best_optimizer_state)
                best_validation_correct = correct_count
                best_validation_cycle = cycle
                best_validation_score = validation_score
                best_validation_results = candidate_all_results
            # O próximo ciclo sempre parte do melhor estado, nunca do último estado pior.
            restore_best_state(
                best_checkpoint, best_optimizer_state, checkpoint, optimizer_state
            )
            previous_learning_rate = learning_rate
            learning_rate = decay_learning_rate(learning_rate)
            write_learning_rate(learning_rate_path, learning_rate)
            print(
                f"Scheduler: LR {previous_learning_rate:.8g} -> {learning_rate:.8g} "
                "(redução de 10% após este ciclo; piso 1e-5).",
                flush=True,
            )
            elapsed = time.perf_counter() - started
            report.write(
                f"**Treino: {completed_epochs:.0f} época(s), {target_bytes:.0f} bytes-alvo, "
                f"train_bpb {initial_bpb:.4f} → {final_bpb:.4f}.**\n"
                f"**Validação independente: {correct_count}/{total}; F1 médio: {mean_f1:.3f}; "
                f"similaridade char: {mean_char_similarity:.3f}; regressões históricas: {regressions}; "
                f"regressões contra melhor estado: {len(retention_regressions)}; "
                f"recuperações: {recoveries}; pontuação: {validation_score:.3f}; "
                f"próximo LR: {learning_rate:.8g}; "
                f"duração: {elapsed:.2f}s.**\n\n"
            )
            report.flush()
            print(
                f"Validação independente do ciclo {cycle}: {correct_count}/{total}; "
                f"F1={mean_f1:.3f}; char={mean_char_similarity:.3f}; "
                f"regressões históricas={regressions}; "
                f"regressões contra melhor={len(retention_regressions)}; recuperações={recoveries}; "
                f"score={validation_score:.3f}; próximo LR={learning_rate:.8g}; {elapsed:.2f}s.",
                flush=True,
            )

            if should_stop(continuous, best_validation_correct, question_count):
                report.write("O melhor checkpoint mantém todas as questões selecionadas corretas; treinamento encerrado.\n\n")
                break
            if not continuous and cycle >= attempts:
                break
        else:
            raise RuntimeError("Limite de segurança atingido sem completar o objetivo.")

        if continuous and not should_stop(continuous, best_validation_correct, question_count):
            raise RuntimeError(
                f"Objetivo contínuo não atingido no melhor checkpoint: "
                f"{best_validation_correct}/{question_count} após {cycle} ciclos."
            )

        # A avaliação final usa o melhor estado de validação, não necessariamente
        # o último ciclo, e restaura também os momentos Adam correspondentes.
        restore_best_state(best_checkpoint, best_optimizer_state,
                           checkpoint, optimizer_state)
        report.write(
            f"**Melhor estado: ciclo {best_validation_cycle}; "
            f"validação {best_validation_correct}/{len(validation)}; "
            f"pontuação {best_validation_score:.3f}.**\n\n"
        )
        print(
            f"Melhor checkpoint: ciclo {best_validation_cycle}; "
            f"validação {best_validation_correct}/{len(validation)}; "
            f"score={best_validation_score:.3f}.",
            flush=True,
        )

        # No modo de uma questão, não consultar o teste reservado.
        if question_count == 1:
            report.write("Teste reservado não executado: modo de uma questão.\n")
            print("Teste reservado preservado e não executado (modo de uma questão).", flush=True)
        else:
            test_results = evaluate_set(generator, checkpoint, test)
            test_correct, test_total = write_results(
                report, "Teste final reservado", test, test_results
            )
            report.write(
                f"**Resultado final no teste reservado: {test_correct}/{test_total}.**\n"
            )
            print(f"Teste final reservado: {test_correct}/{test_total}.", flush=True)

    print(f"\nRelatório salvo em: {output}")
    print(f"Checkpoint final: {checkpoint}")
    print(f"Learning rate persistido: {learning_rate:.8g}")
    print(f"Histórico de perguntas: {question_history_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
