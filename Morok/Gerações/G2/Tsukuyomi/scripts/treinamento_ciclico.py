#!/usr/bin/env python3
"""Treinamento cíclico com validação e teste independentes do Tsukuyomi G2 0.1."""

from collections import Counter, defaultdict
import difflib
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
MAX_VALIDATION_QUESTIONS = 40
MAX_CYCLES = 100
MAX_EPOCHS_PER_CYCLE = 1000
try:
    INITIAL_LEARNING_RATE = float(os.environ.get("TSUKUYOMI_LEARNING_RATE", "0.001"))
except ValueError as error:
    raise SystemExit("TSUKUYOMI_LEARNING_RATE precisa ser um número válido.") from error
if not math.isfinite(INITIAL_LEARNING_RATE) or not 0.0 < INITIAL_LEARNING_RATE <= 0.1:
    raise SystemExit("TSUKUYOMI_LEARNING_RATE precisa estar entre 0 e 0.1.")
MIN_LEARNING_RATE = 0.00005
LR_REDUCE_FACTOR = 0.5
LR_PLATEAU_CYCLES = 3
LR_MIN_DELTA = 0.005


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


def update_learning_rate(current: float, best_rank: tuple[int, float], plateau: int,
                         observed_rank: tuple[int, float]) -> tuple[float, tuple[int, float], int, bool]:
    """Reduz LR após três ciclos sem melhora real; exatidão sempre domina o desempate."""
    correct, quality = observed_rank
    if correct < 0 or not 0.0 <= quality <= 1.0 or not math.isfinite(quality):
        raise ValueError("Ranking de validação inválido.")
    if correct > best_rank[0] or (
        correct == best_rank[0] and quality > best_rank[1] + LR_MIN_DELTA
    ):
        return current, observed_rank, 0, False
    plateau += 1
    if plateau >= LR_PLATEAU_CYCLES and current > MIN_LEARNING_RATE:
        reduced = max(MIN_LEARNING_RATE, current * LR_REDUCE_FACTOR)
        return reduced, best_rank, 0, reduced < current
    return current, best_rank, plateau, False


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
    """Seleciona perguntas em ordem fixa, sem favorecer respostas memorizadas."""
    del train_dialogues
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


def record_validation_errors(memory: dict[str, str],
                             dialogues: list[tuple[str, str]],
                             results: list[tuple[str, bool, float]]) -> int:
    """Guarda respostas incorretas como exemplos supervisionados sem duplicar perguntas."""
    if len(dialogues) != len(results):
        raise ValueError("Quantidade de diálogos e resultados de validação não corresponde.")
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
    if len(sys.argv) != 10:
        print(
            "Uso: treinamento_ciclico.py <treinador> <gerador> <treino> "
            "<validacao> <teste> <epocas> <tentativas> <continuo:0|1> <questoes:1..40>",
            file=sys.stderr,
        )
        return 2

    trainer, generator, train_corpus, validation_corpus, test_corpus = map(
        Path, sys.argv[1:6]
    )
    epochs, attempts = int(sys.argv[6]), int(sys.argv[7])
    continuous = sys.argv[8] == "1"
    question_count = int(sys.argv[9])
    if not 1 <= epochs <= MAX_EPOCHS_PER_CYCLE or not 1 <= attempts <= MAX_CYCLES:
        raise ValueError(f"Épocas precisam estar entre 1 e {MAX_EPOCHS_PER_CYCLE}; tentativas entre 1 e {MAX_CYCLES}.")
    if sys.argv[8] not in ("0", "1"):
        raise ValueError("O modo contínuo precisa ser 0 ou 1.")
    if not 1 <= question_count <= MAX_VALIDATION_QUESTIONS:
        raise ValueError("A quantidade de questões precisa estar entre 1 e 40.")

    train_dialogues = read_dialogues(train_corpus)
    validation_pool = read_dialogues(validation_corpus)
    if len(validation_pool) != MAX_VALIDATION_QUESTIONS:
        raise RuntimeError(
            f"O banco de validação precisa conter exatamente 40 questões; "
            f"foram encontradas {len(validation_pool)}."
        )
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
    feedback_memory = load_feedback_memory(feedback_path)
    resume_previous_run = resumable_state_exists(checkpoint, optimizer_state)
    if resume_previous_run:
        print(
            "Estado persistente encontrado: retomando pesos e momentos Adam da execução anterior.",
            flush=True,
        )
    else:
        print("Nenhum estado persistente encontrado: inicialização do modelo.", flush=True)

    cycle = 0
    learning_rate = INITIAL_LEARNING_RATE
    best_validation_score = -1.0
    best_validation_correct = 0
    best_validation_cycle = 0
    best_validation_rank = (0, -1.0)
    plateau_cycles = 0
    maximum_cycles = min(attempts, MAX_CYCLES)
    with output.open("w", encoding="utf-8") as report:
        report.write("# Treinamento e avaliação — Tsukuyomi G2 0.1\n\n")
        report.write(f"- Diálogos de treinamento originais: {len(train_dialogues)}\n")
        report.write(f"- Correções persistidas antes desta execução: {len(feedback_memory)}\n")
        report.write("- Respostas incorretas da validação são guardadas como exemplos supervisionados para ciclos e execuções futuras; após o primeiro erro, a validação também funciona como feedback adaptativo. O teste reservado permanece separado.\\n")
        report.write(f"- Banco de validação disponível: {len(validation_pool)}\n")
        report.write(f"- Questões selecionadas: {question_count}\n")
        report.write(f"- Perguntas de teste reservado: {len(test)}\n")
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
        report.write(f"- Learning rate inicial: {learning_rate:.6f}; scheduler reduz após {LR_PLATEAU_CYCLES} ciclos sem melhora da validação.\n")
        report.write("- O conjunto de teste é consultado somente após o treinamento.\n\n")

        while cycle < maximum_cycles:
            cycle += 1
            build_training_corpus(train_corpus, feedback_memory, combined_training)
            command = [
                str(trainer), str(combined_training), str(checkpoint),
                str(epochs), "42", f"{learning_rate:.8f}",
            ]
            if cycle > 1 or resume_previous_run:
                command.append("--resume")

            print(
                f"\n=== Ciclo {cycle}: {epochs} época(s), validação "
                f"{len(validation)} pergunta(s), LR={learning_rate:.6f} ===",
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
            validation_results = evaluate_set(generator, checkpoint, validation)
            new_corrections = record_validation_errors(
                feedback_memory, validation, validation_results
            )
            write_feedback_memory(feedback_path, feedback_memory)
            print(
                f"Memória corretiva: {new_corrections} nova(s) correção(ões); "
                f"{len(feedback_memory)} pergunta(s) acumulada(s).",
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
            mean_f1 = sum(result[2] for result in validation_results) / max(1, total)
            mean_char_similarity = sum(
                character_similarity(result[0], expected)
                for (_, expected), result in zip(validation, validation_results)
            ) / max(1, total)
            validation_score = (
                0.70 * (correct_count / max(1, total))
                + 0.15 * mean_f1
                + 0.15 * mean_char_similarity
            )
            if not math.isfinite(validation_score):
                raise RuntimeError("A pontuação de validação não é finita.")
            observed_rank = validation_rank(
                correct_count, total, mean_f1, mean_char_similarity
            )
            previous_best_rank = best_validation_rank
            learning_rate, best_validation_rank, plateau_cycles, reduced = update_learning_rate(
                learning_rate, best_validation_rank, plateau_cycles, observed_rank
            )
            if best_validation_rank != previous_best_rank:
                save_best_state(checkpoint, optimizer_state,
                                best_checkpoint, best_optimizer_state)
                best_validation_correct = correct_count
                best_validation_cycle = cycle
                best_validation_score = validation_score
            if reduced:
                print(
                    f"Scheduler: validação estagnada; learning rate reduzido para {learning_rate:.6f}.",
                    flush=True,
                )
            elapsed = time.perf_counter() - started
            report.write(
                f"**Treino: {completed_epochs:.0f} época(s), {target_bytes:.0f} bytes-alvo, "
                f"train_bpb {initial_bpb:.4f} → {final_bpb:.4f}.**\n"
                f"**Validação: {correct_count}/{total}; F1 médio: {mean_f1:.3f}; "
                f"similaridade char: {mean_char_similarity:.3f}; "
                f"pontuação: {validation_score:.3f}; LR: {learning_rate:.6f}; "
                f"duração: {elapsed:.2f}s.**\n\n"
            )
            report.flush()
            print(
                f"Validação do ciclo {cycle}: {correct_count}/{total}; F1={mean_f1:.3f}; "
                f"char={mean_char_similarity:.3f}; score={validation_score:.3f}; LR={learning_rate:.6f}; {elapsed:.2f}s.",
                flush=True,
            )

            if should_stop(continuous, correct_count, total):
                report.write("Todas as questões selecionadas foram respondidas corretamente; treinamento encerrado.\n\n")
                break
            if not continuous and cycle >= attempts:
                break
        else:
            raise RuntimeError("Limite de segurança atingido sem completar o objetivo.")

        if continuous and not should_stop(continuous, correct_count, total):
            raise RuntimeError(
                f"Objetivo contínuo não atingido: {correct_count}/{total} após {cycle} ciclos."
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
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
