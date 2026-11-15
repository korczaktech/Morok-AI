#!/usr/bin/env python3
"""Executa ciclos de treinamento e geração para todas as perguntas do corpus."""

from pathlib import Path
import subprocess
import sys


def main() -> int:
    if len(sys.argv) != 6:
        print(
            "Uso: treinamento_ciclico.py <treinador> <gerador> <corpus> "
            "<epocas_por_ciclo> <tentativas>",
            file=sys.stderr,
        )
        return 2

    trainer = Path(sys.argv[1])
    generator = Path(sys.argv[2])
    corpus = Path(sys.argv[3])
    epochs = int(sys.argv[4])
    attempts = int(sys.argv[5])

    if epochs < 1 or attempts < 1:
        raise ValueError("Épocas e tentativas precisam ser maiores que zero.")

    questions = []
    for line in corpus.read_text(encoding="utf-8").splitlines():
        if line.startswith("U:"):
            question = line[2:].strip()
            if question:
                questions.append(question)

    if not questions:
        raise RuntimeError("Nenhuma pergunta no formato 'U: ...' foi encontrada.")

    checkpoint = Path("build/g2/tsukuyomi-treinamento-ciclico.ckpt")
    output = Path("build/g2/respostas-tsukuyomi.md")
    output.parent.mkdir(parents=True, exist_ok=True)

    with output.open("w", encoding="utf-8") as report:
        report.write("# Respostas do treinamento cíclico — Tsukuyomi G2\n\n")
        report.write(f"- Perguntas encontradas: {len(questions)}\n")
        report.write(f"- Épocas por ciclo: {epochs}\n")
        report.write(f"- Ciclos/tentativas: {attempts}\n\n")

        for attempt in range(1, attempts + 1):
            command = [
                str(trainer), str(corpus), str(checkpoint), str(epochs), "42"
            ]
            if attempt > 1:
                command.append("--resume")

            print(
                f"\n=== Ciclo {attempt}/{attempts}: "
                f"{epochs} época(s), {len(questions)} pergunta(s) ===",
                flush=True,
            )
            subprocess.run(command, check=True)

            report.write(f"## Tentativa {attempt}\n\n")
            for index, question in enumerate(questions, start=1):
                prompt = f"U: {question}\nIA:"
                result = subprocess.run(
                    [str(generator), str(checkpoint), prompt, "256"],
                    check=True,
                    capture_output=True,
                    text=True,
                    encoding="utf-8",
                )
                answer = result.stdout.strip()
                report.write(f"### {index}. {question}\n\n")
                report.write((answer or "_(nenhuma saída gerada)_") + "\n\n")
            report.flush()

    print(f"\nRelatório salvo em: {output}")
    print(f"Checkpoint final: {checkpoint}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
