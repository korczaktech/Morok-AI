#!/usr/bin/env python3
"""Treina Tsukuyomi G2, avalia cada resposta contra o gabarito e controla ciclos."""

from pathlib import Path
import re
import subprocess
import sys
import unicodedata


def normalize(text: str) -> list[str]:
    text = unicodedata.normalize("NFKD", text.casefold())
    text = "".join(ch for ch in text if not unicodedata.combining(ch))
    return re.findall(r"[a-z0-9]+", text)


def evaluate(answer: str, expected: str) -> tuple[bool, float]:
    """Comparação lexical determinística; não afirma compreensão semântica."""
    actual = normalize(answer)
    reference = normalize(expected)
    if not actual or not reference:
        return False, 0.0
    actual_set = set(actual)
    reference_set = set(reference)
    precision = len(actual_set & reference_set) / len(actual_set)
    recall = len(actual_set & reference_set) / len(reference_set)
    score = 2 * precision * recall / (precision + recall) if precision + recall else 0.0
    exact = actual == reference
    return exact or score >= 0.90, score


def read_dialogues(corpus: Path) -> list[tuple[str, str]]:
    dialogues = []
    question = None
    for raw in corpus.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if line.startswith("U:"):
            question = line[2:].strip()
        elif line.startswith("IA:") and question:
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
        report.write(f"- Critério contínuo: 10 ciclos consecutivos com 100% das questões corretas\n")
        report.write("- Avaliador: correspondência lexical determinística (não é compreensão semântica)\n\n")

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
            subprocess.run(command, check=True)

            cycle_results = []
            report.write(f"## Tentativa {cycle}\n\n")
            for index, (question, expected) in enumerate(dialogues, start=1):
                prompt = f"U: {question}\nIA:"
                result = subprocess.run(
                    [str(generator), str(checkpoint), prompt, "256"],
                    check=True, capture_output=True, text=True, encoding="utf-8",
                )
                answer = result.stdout.strip()
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

            all_correct = len(cycle_results) == len(dialogues) and all(cycle_results)
            streak = streak + 1 if all_correct else 0
            report.write(
                f"**Resultado da tentativa {cycle}: "
                f"{sum(cycle_results)}/{len(dialogues)} corretas. "
                f"Sequência de ciclos perfeitos: {streak}/10.**\n\n"
            )
            report.flush()
            print(
                f"Ciclo {cycle}: {sum(cycle_results)}/{len(dialogues)} corretas; "
                f"sequência {streak}/10.",
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
