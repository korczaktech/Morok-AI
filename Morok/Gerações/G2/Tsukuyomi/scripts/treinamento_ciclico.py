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
    return actual == reference or score >= 0.90, score


def read_dialogues(corpus: Path) -> list[tuple[str, str]]:
    dialogues = []
    question = None
    for raw in corpus.read_text(encoding="utf-8").splitlines():
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
                 dialogues: list[tuple[str, str]]) -> list[tuple[str, str, bool, float]]:
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
    results = []
    for (question, expected), answer in zip(dialogues, generated):
        answer = answer.strip()
        correct, score = evaluate(answer, expected)
        results.append((question, answer, correct, score))
    return results


def write_results(report, title: str, results) -> tuple[int, int]:
    report.write(f"## {title}\n\n")
    for index, (question, answer, correct, score) in enumerate(results, start=1):
        verdict = "CORRETA" if correct else "INCORRETA"
        report.write(f"### {index}. {question}\n\n")
        report.write(f"- Resposta gerada: {answer or '(vazia)'}\n")
        report.write(f"- Gabarito: {results_expected_placeholder(question)}\n")
        report.write(f"- Correspondência lexical: {score:.1%}\n")
        report.write(f"- Resultado: **{verdict}**\n\n")
    correct_count = sum(item[2] for item in results)
    return correct_count, len(results)


def results_expected_placeholder(question: str) -> str:
    # O gabarito é acrescentado por write_dialogue_results para evitar estado global.
    return "(ver gabarito no arquivo de origem)"


def write_dialogue_results(report, title: str, dialogues, results) -> tuple[int, int]:
    report.write(f"## {title}\n\n")
    for index, ((question, expected), (_, answer, correct, score)) in enumerate(
        zip(dialogues, results), start=1
    ):
        verdict = "CORRETA" if correct else "INCORRETA"
        report.write(f"### {index}. {question}\n\n")
        report.write(f"- Resposta gerada: {answer or '(vazia)'}\n")
        report.write(f"- Gabarito: {expected}\n")
        report.write(f"- Correspondência lexical: {score:.1%}\n")
        report.write(f"- Resultado: **{verdict}**\n\n")
    return sum(item[2] for item in results), len(results)


def main() -> int:
    if len(sys.argv) != 9:
        print(
            "Uso: treinamento_ciclico.py <treinador> <gerador> <treino> "
            "<validacao> <teste> <epocas> <tentativas> <continuo:0|1>",
            file=sys.stderr,
        )
        return 2

    trainer, generator, train_corpus, validation_corpus, test_corpus = map(
        Path, sys.argv[1:6]
    )
    epochs, attempts = int(sys.argv[6]), int(sys.argv[7])
    continuous = sys.argv[8] == "1"
    if epochs < 1 or attempts < 1:
        raise ValueError("Épocas e tentativas precisam ser maiores que zero.")

    validation = read_dialogues(validation_corpus)
    test = read_dialogues(test_corpus)
    train_dialogues = read_dialogues(train_corpus)
    checkpoint = Path("build/g2/tsukuyomi-treinamento-ciclico.ckpt")
    output = Path("build/g2/respostas-tsukuyomi.md")
    output.parent.mkdir(parents=True, exist_ok=True)

    streak = 0
    cycle = 0
    maximum_cycles = 100000 if continuous else attempts
    with output.open("w", encoding="utf-8") as report:
        report.write("# Treinamento e avaliação — Tsukuyomi G2 0.1\n\n")
        report.write(f"- Diálogos de treinamento: {len(train_dialogues)}\n")
        report.write(f"- Perguntas de validação: {len(validation)}\n")
        report.write(f"- Perguntas de teste reservado: {len(test)}\n")
        report.write(f"- Épocas por ciclo: {epochs}\n")
        report.write(f"- Modo contínuo: {'ativado' if continuous else 'desativado'}\n")
        report.write(f"- Limite de geração: {MAX_RESPONSE_BYTES} bytes\n")
        report.write("- Critério contínuo: 10 ciclos consecutivos com 100% na validação\n")
        report.write("- Métrica: F1 lexical por frequência; não mede semântica\n")
        report.write("- O conjunto de teste é consultado somente após o treinamento.\n\n")

        while cycle < maximum_cycles:
            cycle += 1
            command = [str(trainer), str(train_corpus), str(checkpoint), str(epochs), "42"]
            if cycle > 1:
                command.append("--resume")

            print(
                f"\n=== Ciclo {cycle}: {epochs} época(s), validação "
                f"{len(validation)} perguntas; sequência {streak}/10 ===",
                flush=True,
            )
            started = time.perf_counter()
            subprocess.run(command, check=True)
            validation_results = evaluate_set(generator, checkpoint, validation)
            correct_count, total = write_dialogue_results(
                report, f"Validação — tentativa {cycle}", validation, validation_results
            )
            all_correct = total > 0 and correct_count == total
            streak = streak + 1 if all_correct else 0
            elapsed = time.perf_counter() - started
            report.write(
                f"**Validação: {correct_count}/{total}. Sequência perfeita: "
                f"{streak}/10. Duração: {elapsed:.2f}s.**\n\n"
            )
            report.flush()
            print(
                f"Validação do ciclo {cycle}: {correct_count}/{total}; "
                f"sequência {streak}/10; {elapsed:.2f}s.",
                flush=True,
            )

            if continuous and streak >= 10:
                report.write("Dez ciclos consecutivos aprovados na validação.\n\n")
                break
            if not continuous and cycle >= attempts:
                break
        else:
            raise RuntimeError("Limite de segurança atingido sem completar o objetivo.")

        if continuous and streak < 10:
            raise RuntimeError("Treinamento contínuo terminou sem 10 ciclos perfeitos.")

        # O teste reservado não participa da escolha de ciclos nem do critério de parada.
        test_results = evaluate_set(generator, checkpoint, test)
        test_correct, test_total = write_dialogue_results(
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
