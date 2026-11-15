#!/usr/bin/env python3
"""Treinamento cíclico e avaliação em lote do Tsukuyomi G2."""

from collections import Counter
from pathlib import Path
import re
import subprocess
import sys
import time
import unicodedata


MAX_RESPONSE_BYTES = 96


def normalize(text: str) -> list[str]:
    text = unicodedata.normalize("NFKD", text.casefold())
    text = "".join(ch for ch in text if not unicodedata.combining(ch))
    return re.findall(r"[a-z0-9]+", text)


def evaluate(answer: str, expected: str) -> tuple[bool, float]:
    """F1 por frequência de palavras; não representa avaliação semântica."""
    actual = normalize(answer)
    reference = normalize(expected)
    if not actual or not reference:
        return False, 0.0

    actual_counts = Counter(actual)
    reference_counts = Counter(reference)
    overlap = sum((actual_counts & reference_counts).values())
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
        raise RuntimeError("Nenhum par U:/IA: completo foi encontrado no corpus.")
    return dialogues


def main() -> int:
    if len(sys.argv) != 7:
        print(
            "Uso: treinamento_ciclico.py <treinador> <gerador> <corpus> "
            "<epocas_por_ciclo> <tentativas> <continuo:0|1>",
            file=sys.stderr,
        )
        return 2

    trainer, generator, corpus = map(Path, sys.argv[1:4])
    epochs, attempts = int(sys.argv[4]), int(sys.argv[5])
    continuous = sys.argv[6] == "1"
    if epochs < 1 or attempts < 1:
        raise ValueError("Épocas e tentativas precisam ser maiores que zero.")

    dialogues = read_dialogues(corpus)
    checkpoint = Path("build/g2/tsukuyomi-treinamento-ciclico.ckpt")
    output = Path("build/g2/respostas-tsukuyomi.md")
    output.parent.mkdir(parents=True, exist_ok=True)

    streak = 0
    cycle = 0
    maximum_cycles = 100000 if continuous else attempts
    with output.open("w", encoding="utf-8") as report:
        report.write("# Avaliação do treinamento — Tsukuyomi G2\n\n")
        report.write(f"- Perguntas: {len(dialogues)}\n")
        report.write(f"- Épocas por ciclo: {epochs}\n")
        report.write(f"- Modo contínuo: {'ativado' if continuous else 'desativado'}\n")
        report.write("- Geração: um único processo por ciclo, checkpoint carregado uma vez\n")
        report.write(f"- Limite por resposta: {MAX_RESPONSE_BYTES} bytes\n")
        report.write("- Critério contínuo: 10 ciclos consecutivos com 100% das questões corretas\n")
        report.write("- Avaliador: F1 lexical por frequência de palavras (não semântico)\n\n")

        while cycle < maximum_cycles:
            cycle += 1
            command = [str(trainer), str(corpus), str(checkpoint), str(epochs), "42"]
            if cycle > 1:
                command.append("--resume")

            print(
                f"\n=== Ciclo {cycle}: {epochs} época(s), "
                f"{len(dialogues)} pergunta(s); sequência correta: {streak}/10 ===",
                flush=True,
            )
            started = time.perf_counter()
            subprocess.run(command, check=True)

            # O gerador mantém o modelo carregado durante todas as perguntas.
            prompts = "".join(question.replace("\n", " ").replace("\r", " ") + "\n"
                              for question, _ in dialogues)
            generated = subprocess.run(
                [str(generator), "--batch", str(checkpoint), str(MAX_RESPONSE_BYTES)],
                input=prompts,
                check=True,
                capture_output=True,
                text=True,
                encoding="utf-8",
            ).stdout.splitlines()

            if len(generated) != len(dialogues):
                raise RuntimeError(
                    f"Gerador retornou {len(generated)} respostas para "
                    f"{len(dialogues)} perguntas."
                )

            cycle_results = []
            report.write(f"## Tentativa {cycle}\n\n")
            for index, ((question, expected), answer) in enumerate(
                zip(dialogues, generated), start=1
            ):
                answer = answer.strip()
                correct, score = evaluate(answer, expected)
                cycle_results.append(correct)
                verdict = "CORRETA" if correct else "INCORRETA"
                report.write(f"### {index}. {question}\n\n")
                report.write(f"- Resposta gerada: {answer or '(vazia)'}\n")
                report.write(f"- Gabarito: {expected}\n")
                report.write(f"- Correspondência lexical: {score:.1%}\n")
                report.write(f"- Resultado: **{verdict}**\n\n")
                print(
                    f"::{'notice' if correct else 'error'} title=Tsukuyomi G2::{verdict} "
                    f"— tentativa {cycle}, questão {index}/{len(dialogues)}: {question}; "
                    f"correspondência lexical {score:.1%}",
                    flush=True,
                )

            elapsed = time.perf_counter() - started
            correct_count = sum(cycle_results)
            all_correct = len(cycle_results) == len(dialogues) and all(cycle_results)
            streak = streak + 1 if all_correct else 0
            report.write(
                f"**Resultado da tentativa {cycle}: {correct_count}/{len(dialogues)} "
                f"corretas. Sequência de ciclos perfeitos: {streak}/10. "
                f"Tempo do ciclo: {elapsed:.2f} s.**\n\n"
            )
            report.flush()
            print(
                f"Ciclo {cycle}: {correct_count}/{len(dialogues)} corretas; "
                f"sequência {streak}/10; duração {elapsed:.2f}s.",
                flush=True,
            )

            if continuous and streak >= 10:
                report.write("## Objetivo atingido\n\n")
                report.write("Dez ciclos consecutivos concluídos com todas as questões corretas.\n")
                print("Objetivo atingido: 10 ciclos consecutivos com 100% de acerto.", flush=True)
                break
            if not continuous and cycle >= attempts:
                break
        else:
            raise RuntimeError("Limite de segurança de ciclos atingido sem completar o objetivo.")

    if continuous and streak < 10:
        raise RuntimeError("Treinamento contínuo terminou sem atingir 10 ciclos perfeitos.")
    print(f"\nRelatório salvo em: {output}")
    print(f"Checkpoint final: {checkpoint}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
