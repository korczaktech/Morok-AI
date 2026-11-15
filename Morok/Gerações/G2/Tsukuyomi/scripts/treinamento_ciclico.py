#!/usr/bin/env python3
"""Treinamento cíclico com validação e teste independentes do Tsukuyomi G2 0.1."""

from collections import Counter
from pathlib import Path
import re
import subprocess
import sys
import time
import unicodedata

MAX_RESPONSE_BYTES = 160
MAX_VALIDATION_QUESTIONS = 40
MAX_CYCLES = 100000
INITIAL_LEARNING_RATE = 0.001
MIN_LEARNING_RATE = 0.00005
LR_REDUCE_FACTOR = 0.5
LR_PLATEAU_CYCLES = 5


def update_learning_rate(current: float, best_bpb: float, plateau: int,
                         observed_bpb: float) -> tuple[float, float, int, bool]:
    """Reduz o LR após cinco ciclos sem melhora mensurável de train_bpb."""
    if not (observed_bpb >= 0.0):
        raise ValueError("train_bpb observado precisa ser não negativo.")
    if observed_bpb + 1.0e-6 < best_bpb:
        return current, observed_bpb, 0, False
    plateau += 1
    if plateau >= LR_PLATEAU_CYCLES and current > MIN_LEARNING_RATE:
        reduced = max(MIN_LEARNING_RATE, current * LR_REDUCE_FACTOR)
        return reduced, best_bpb, 0, reduced < current
    return current, best_bpb, plateau, False


def should_stop(continuous: bool, correct_count: int, total: int) -> bool:
    """No modo contínuo, encerra assim que a questão selecionada estiver correta."""
    return continuous and total > 0 and correct_count == total


def select_validation_questions(train_dialogues, validation_pool, count):
    """Prioriza perguntas cuja resposta de referência existe nos 32 exemplos de treino."""
    known_answers = {tuple(normalize(answer)) for _, answer in train_dialogues}
    candidates = [
        dialogue for dialogue in validation_pool
        if tuple(normalize(dialogue[1])) in known_answers
    ]
    selected_pool = candidates if count == 1 and candidates else validation_pool
    return selected_pool[:count]


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
    for index, (dialogue, result) in enumerate(zip(dialogues, results), start=1):
        question, expected = dialogue
        answer, correct, score = result
        item = (index, question, expected, answer, score)
        (correct_items if correct else incorrect_items).append(item)

    report.write(f"## {title}\n\n")
    report.write(f"- Respostas corretas: {len(correct_items)}\n")
    report.write(f"- Respostas incorretas: {len(incorrect_items)}\n")
    report.write(f"- Total avaliado: {len(results)}\n\n")

    for heading, items, verdict in (
        ("Respostas corretas", correct_items, "CORRETA"),
        ("Respostas incorretas", incorrect_items, "INCORRETA"),
    ):
        report.write(f"### {heading} ({len(items)})\n\n")
        if not items:
            report.write("Nenhuma resposta nesta categoria.\n\n")
            continue
        for index, question, expected, answer, score in items:
            report.write(f"#### {index}. {question}\n\n")
            report.write(f"- Resposta gerada: {answer or '(vazia)'}\n")
            report.write(f"- Gabarito: {expected}\n")
            report.write(f"- Correspondência lexical: {score:.1%}\n")
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
    if epochs < 1 or attempts < 1:
        raise ValueError("Épocas e tentativas precisam ser maiores que zero.")
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
    output = Path("build/g2/respostas-tsukuyomi.md")
    output.parent.mkdir(parents=True, exist_ok=True)

    cycle = 0
    learning_rate = INITIAL_LEARNING_RATE
    best_train_bpb = float("inf")
    plateau_cycles = 0
    maximum_cycles = MAX_CYCLES if continuous else attempts
    with output.open("w", encoding="utf-8") as report:
        report.write("# Treinamento e avaliação — Tsukuyomi G2 0.1\n\n")
        report.write(f"- Diálogos de treinamento: {len(train_dialogues)}\n")
        report.write(f"- Banco de validação disponível: {len(validation_pool)}\n")
        report.write(f"- Questões selecionadas: {question_count}\n")
        report.write(f"- Perguntas de teste reservado: {len(test)}\n")
        report.write(f"- Épocas por tentativa: {epochs}\n")
        report.write(f"- Modo contínuo: {'ativado' if continuous else 'desativado'}\n")
        report.write(f"- Limite de geração: {MAX_RESPONSE_BYTES} bytes\n")
        report.write("- Critério contínuo: encerrar no primeiro ciclo em que a questão selecionada estiver correta\n")
        report.write("- Critério de aprovação: igualdade lexical normalizada; não mede semântica\n")
        report.write("- F1 lexical é apenas diagnóstico; similaridade não aprova uma resposta.\n")
        report.write(f"- Learning rate inicial: {learning_rate:.6f}; scheduler reduz após {LR_PLATEAU_CYCLES} ciclos sem melhora de train_bpb.\n")
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
            bpb_match = re.search(
                r"train_bpb final:\s*([0-9.eE+-]+)", training_process.stdout
            )
            if bpb_match:
                train_bpb = float(bpb_match.group(1))
                learning_rate, best_train_bpb, plateau_cycles, reduced = update_learning_rate(
                    learning_rate, best_train_bpb, plateau_cycles, train_bpb
                )
                if reduced:
                    print(
                        f"Scheduler: train_bpb sem melhora; learning rate reduzido para {learning_rate:.6f}.",
                        flush=True,
                    )
            else:
                raise RuntimeError("O treinador não informou train_bpb final; scheduler não pode continuar.")
            validation_results = evaluate_set(generator, checkpoint, validation)
            correct_count, total = write_results(
                report, f"Validação — tentativa {cycle}", validation, validation_results
            )
            elapsed = time.perf_counter() - started
            report.write(
                f"**Validação: {correct_count}/{total}. F1 é diagnóstico, não critério de aprovação. "
                f"LR: {learning_rate:.6f}. Duração: {elapsed:.2f}s.**\n\n"
            )
            report.flush()
            print(
                f"Validação do ciclo {cycle}: {correct_count}/{total}; "
                f"LR={learning_rate:.6f}; {elapsed:.2f}s.",
                flush=True,
            )

            if should_stop(continuous, correct_count, total):
                report.write("Questão selecionada respondida corretamente; treinamento encerrado.\n\n")
                break
            if not continuous and cycle >= attempts:
                break
        else:
            raise RuntimeError("Limite de segurança atingido sem completar o objetivo.")

        if continuous and not should_stop(continuous, correct_count, total):
            raise RuntimeError(
                f"A questão selecionada não foi respondida corretamente após {cycle} ciclos."
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
