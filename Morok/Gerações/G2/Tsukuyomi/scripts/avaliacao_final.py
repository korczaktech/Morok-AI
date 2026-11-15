#!/usr/bin/env python3
"""Avaliação final reproduzível do Tsukuyomi G2 0.1; métricas lexicais não provam semântica."""
from __future__ import annotations

from collections import Counter
import difflib
import json
import math
from pathlib import Path
import re
import subprocess
import sys
import time
import unicodedata


def normalize(text: str) -> list[str]:
    text = unicodedata.normalize("NFKD", text.casefold())
    text = "".join(ch for ch in text if not unicodedata.combining(ch))
    return re.findall(r"[a-z0-9]+", text)


def metrics(answer: str, expected: str) -> dict:
    actual = normalize(answer)
    reference = normalize(expected)
    overlap = sum((Counter(actual) & Counter(reference)).values())
    precision = overlap / len(actual) if actual else 0.0
    recall = overlap / len(reference) if reference else 0.0
    f1 = 2 * precision * recall / (precision + recall) if precision + recall else 0.0
    exact = bool(actual and reference and actual == reference)
    a, b = " ".join(actual), " ".join(reference)
    char = difflib.SequenceMatcher(None, a, b, autojunk=False).ratio() if a and b else 0.0
    longest = current = 1
    for left, right in zip(answer, answer[1:]):
        current = current + 1 if left == right else 1
        longest = max(longest, current)
    repetition = longest / len(answer) if answer else 0.0
    diversity = len(set(answer)) / len(answer) if answer else 0.0
    return {"exact": exact, "lexical_f1": f1, "character_similarity": char,
            "consecutive_repetition": repetition, "character_diversity": diversity}


def read_dialogues(path: Path) -> list[tuple[str, str]]:
    result = []
    question = None
    for raw in path.read_text(encoding="utf-8-sig").splitlines():
        line = raw.strip()
        if line.startswith("U:"):
            question = line[2:].strip()
        elif line.startswith("IA:") and question is not None:
            answer = line[3:].strip()
            if answer:
                result.append((question, answer))
            question = None
    if not result:
        raise ValueError(f"Corpus sem pares U:/IA: completos: {path}")
    return result


def generate(generator: Path, checkpoint: Path, dialogues: list[tuple[str, str]]) -> tuple[list[str], float]:
    prompts = "".join(q.replace("\n", " ").replace("\r", " ") + "\n" for q, _ in dialogues)
    started = time.perf_counter()
    proc = subprocess.run([str(generator), "--batch", str(checkpoint), "160"],
                          input=prompts, check=True, capture_output=True,
                          text=True, encoding="utf-8")
    elapsed = time.perf_counter() - started
    answers = [line.strip() for line in proc.stdout.splitlines()]
    if len(answers) != len(dialogues):
        raise RuntimeError(f"Gerador retornou {len(answers)} respostas para {len(dialogues)} perguntas.")
    return answers, elapsed


def evaluate_split(generator: Path, checkpoint: Path, dialogues: list[tuple[str, str]]) -> dict:
    answers, elapsed = generate(generator, checkpoint, dialogues)
    rows = [metrics(answer, expected) for answer, (_, expected) in zip(answers, dialogues)]
    count = len(rows)
    means = {key: sum(row[key] for row in rows) / count for key in
             ("lexical_f1", "character_similarity", "consecutive_repetition", "character_diversity")}
    return {
        "questions": count,
        "exact_matches": sum(row["exact"] for row in rows),
        "exact_match_rate": sum(row["exact"] for row in rows) / count,
        **{f"mean_{key}": value for key, value in means.items()},
        "generation_seconds_including_checkpoint_load": elapsed,
        "questions_per_second_including_checkpoint_load": count / elapsed if elapsed > 0 else 0.0,
        "answers": [{"question": q, "expected": expected, "generated": answer, **row}
                    for (q, expected), answer, row in zip(dialogues, answers, rows)],
    }


def main() -> int:
    if len(sys.argv) not in (6, 8):
        print("Uso: avaliacao_final.py <gerador> <checkpoint> <validacao.txt> <teste.txt> <saida.json> [<gerador_baseline> <checkpoint_baseline>]", file=sys.stderr)
        return 2
    generator, checkpoint, validation_path, test_path, output = map(Path, sys.argv[1:])
    if not generator.is_file() or not checkpoint.is_file():
        raise FileNotFoundError("Gerador ou checkpoint final não encontrado.")
    validation = read_dialogues(validation_path)
    test = read_dialogues(test_path)
    if len(validation) != 40 or len(test) != 12:
        raise RuntimeError("Os conjuntos independentes devem conter 40 questões de validação e 12 de teste.")
    validation_result = evaluate_split(generator, checkpoint, validation)
    test_result = evaluate_split(generator, checkpoint, test)
    repeat_answers, repeat_seconds = generate(generator, checkpoint, test)
    deterministic = repeat_answers == [item["generated"] for item in test_result["answers"]]
    comparison = {
        "previous_architecture_checkpoint_available": False,
        "status": "not_comparable_without_a_compatible_baseline_checkpoint",
        "note": "Pass a baseline generator and checkpoint to compare both architectures on identical validation and test splits."
    }
    if len(sys.argv) == 8:
        baseline_generator, baseline_checkpoint = map(Path, sys.argv[6:8])
        if not baseline_generator.is_file() or not baseline_checkpoint.is_file():
            raise FileNotFoundError("Gerador ou checkpoint baseline não encontrado.")
        baseline_validation = evaluate_split(baseline_generator, baseline_checkpoint, validation)
        baseline_test = evaluate_split(baseline_generator, baseline_checkpoint, test)
        comparison = {
            "previous_architecture_checkpoint_available": True,
            "status": "compared_on_identical_splits",
            "baseline_validation": baseline_validation,
            "baseline_reserved_test": baseline_test,
            "candidate_minus_baseline_test_exact_matches": test_result["exact_matches"] - baseline_test["exact_matches"],
            "candidate_minus_baseline_test_lexical_f1": test_result["mean_lexical_f1"] - baseline_test["mean_lexical_f1"],
            "candidate_minus_baseline_test_character_similarity": test_result["mean_character_similarity"] - baseline_test["mean_character_similarity"]
        }
    values = [validation_result["exact_match_rate"], test_result["exact_match_rate"],
              test_result["mean_lexical_f1"], test_result["mean_character_similarity"],
              test_result["mean_consecutive_repetition"], test_result["mean_character_diversity"],
              validation_result["generation_seconds_including_checkpoint_load"],
              test_result["generation_seconds_including_checkpoint_load"], repeat_seconds]
    if not all(math.isfinite(value) for value in values):
        raise RuntimeError("A avaliação produziu métrica não finita.")
    report = {
        "evaluation": "final_independent_g2",
        "model": "Tsukuyomi",
        "generation": "G2",
        "public_version": "0.1.0",
        "architecture_parameter_budget": 10000,
        "training_examples_preserved": 32,
        "pretrained_weights": False,
        "validation": validation_result,
        "reserved_test": test_result,
        "deterministic_greedy_generation": deterministic,
        "repeat_test_generation_seconds": repeat_seconds,
        "comparison": comparison,
        "interpretation": {
            "semantic_understanding_proven": False,
            "lexical_metrics_are_diagnostics_only": True,
            "note": "Exact normalized lexical match, token F1 and character similarity do not establish semantic understanding."
        }
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    markdown = output.with_suffix(".md")
    markdown.write_text(
        "# Avaliação final independente — Tsukuyomi G2 0.1\n\n"
        f"- Parâmetros: 10.000 (limite absoluto).\n"
        f"- Exemplos originais preservados: 32.\n"
        f"- Validação: {validation_result['exact_matches']}/{validation_result['questions']} exatas; "
        f"F1 {validation_result['mean_lexical_f1']:.1%}; similaridade {validation_result['mean_character_similarity']:.1%}.\n"
        f"- Teste reservado: {test_result['exact_matches']}/{test_result['questions']} exatas; "
        f"F1 {test_result['mean_lexical_f1']:.1%}; similaridade {test_result['mean_character_similarity']:.1%}.\n"
        f"- Tempo teste (inclui carga do checkpoint): {test_result['generation_seconds_including_checkpoint_load']:.3f}s; "
        f"{test_result['questions_per_second_including_checkpoint_load']:.2f} perguntas/s.\n"
        f"- Geração gulosa determinística: {'sim' if deterministic else 'não'}.\n"
        f"- Comparação histórica: {comparison['status']}.\n"
        "- As métricas lexicais são diagnósticas e não comprovam compreensão semântica.\n",
        encoding="utf-8")
    if not deterministic:
        raise RuntimeError("A geração gulosa variou entre execuções idênticas.")
    print(f"Avaliação final salva: {output} e {markdown}")
    print(f"Validação: {validation_result['exact_matches']}/40; teste: {test_result['exact_matches']}/12; determinístico: sim")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
