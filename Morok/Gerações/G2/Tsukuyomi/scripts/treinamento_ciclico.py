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
MAX_CYCLES = 10
MAX_EPOCHS_PER_CYCLE = 100
MAX_TOTAL_EPOCHS = 200
INITIAL_LEARNING_RATE = 0.001
MIN_LEARNING_RATE = 0.00005
LR_REDUCE_FACTOR = 0.5
LR_PLATEAU_CYCLES = 5


def update_learning_rate(current: float, best_score: float, plateau: int,
                         observed_score: float) -> tuple[float, float, int, bool]:
    """Reduz o LR após cinco ciclos sem melhora na qualidade das respostas."""
    if not 0.0 <= observed_score <= 1.0:
        raise ValueError("A pontuação de validação precisa estar entre 0 e 1.")
    if observed_score > best_score + 1.0e-6:
        return current, observed_score, 0, False
    plateau += 1
    if plateau >= LR_PLATEAU_CYCLES and current > MIN_LEARNING_RATE:
        reduced = max(MIN_LEARNING_RATE, current * LR_REDUCE_FACTOR)
        return reduced, best_score, 0, reduced < current
    return current, best_score, plateau, False


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
    if any(word in tokens for word in ("explique", "defina", "significa", "como")):
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
    if epochs * attempts > MAX_TOTAL_EPOCHS:
        raise ValueError(f"O treinamento está limitado a {MAX_TOTAL_EPOCHS} épocas totais por execução.")
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

    cycle = 0
    learning_rate = INITIAL_LEARNING_RATE
    best_validation_score = -1.0
    best_validation_correct = 0
    best_validation_cycle = 0
    plateau_cycles = 0
    maximum_cycles = min(attempts, MAX_CYCLES)
    with output.open("w", encoding="utf-8") as report:
        report.write("# Treinamento e avaliação — Tsukuyomi G2 0.1\n\n")
        report.write(f"- Diálogos de treinamento: {len(train_dialogues)}\n")
        report.write(f"- Banco de validação disponível: {len(validation_pool)}\n")
        report.write(f"- Questões selecionadas: {question_count}\n")
        report.write(f"- Perguntas de teste reservado: {len(test)}\n")
        report.write(f"- Épocas por tentativa: {epochs}\n")
        report.write(f"- Épocas totais máximas: {epochs * maximum_cycles}\n")
        report.write(f"- Modo contínuo: {'ativado' if continuous else 'desativado'}\n")
        report.write(f"- Limite de geração: {MAX_RESPONSE_BYTES} bytes\n")
        report.write("- Critério contínuo: encerrar quando todas as questões selecionadas estiverem corretas\n")
        report.write("- Critério de aprovação: igualdade lexical normalizada; não mede semântica\n")
        report.write("- F1 lexical é diagnóstico; similaridade não aprova uma resposta.\n")
        report.write("- Scheduler: 80% exatidão + 20% F1 lexical médio da validação.\n")
        report.write(f"- Learning rate inicial: {learning_rate:.6f}; scheduler reduz após {LR_PLATEAU_CYCLES} ciclos sem melhora da validação.\n")
        report.write("- O conjunto de teste é consultado somente após o treinamento.\n\n")

        while cycle < maximum_cycles:
            cycle += 1
            command = [
                str(trainer), str(train_corpus), str(checkpoint),
                str(epochs), "42", f"{learning_rate:.8f}",
            ]
            if cycle > 1:
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
            correct_count, total = write_results(
                report, f"Validação — ciclo {cycle}", validation, validation_results
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
            previous_best_score = best_validation_score
            learning_rate, best_validation_score, plateau_cycles, reduced = update_learning_rate(
                learning_rate, best_validation_score, plateau_cycles, validation_score
            )
            if validation_score > previous_best_score + 1.0e-6:
                save_best_state(checkpoint, optimizer_state,
                                best_checkpoint, best_optimizer_state)
                best_validation_correct = correct_count
                best_validation_cycle = cycle
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
