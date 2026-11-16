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



def benchmark_category(question: str) -> str:
    tokens = set(normalize(question))
    if tokens & {"compare", "relacao", "diferenca", "diferença"}:
        return "composicao"
    if tokens & {"explique", "defina", "significa", "como", "conceito"} or {"que", "e"}.issubset(tokens):
        return "conceito"
    if tokens & {"quantos", "quantas", "qual", "quais", "quem", "onde", "quando"}:
        return "fato"
    return "conversa"


def summarize_rows(dialogues: list[tuple[str, str]], answers: list[str]) -> dict:
    if len(dialogues) != len(answers) or not dialogues:
        raise ValueError("A avaliação precisa de uma resposta para cada pergunta.")
    groups: dict[str, list[dict]] = {}
    for (question, expected), answer in zip(dialogues, answers):
        groups.setdefault(benchmark_category(question), []).append(metrics(answer, expected))
    summary = {}
    for category, rows in sorted(groups.items()):
        count = len(rows)
        summary[category] = {
            "questions": count,
            "exact_matches": sum(row["exact"] for row in rows),
            "exact_match_rate": sum(row["exact"] for row in rows) / count,
            **{f"mean_{key}": sum(row[key] for row in rows) / count for key in
               ("lexical_f1", "character_similarity", "consecutive_repetition", "character_diversity")},
        }
    return summary



def retrieval_baseline_answers(training: list[tuple[str, str]],
                               questions: list[tuple[str, str]]) -> list[str]:
    if not training:
        raise ValueError("O baseline de recuperação precisa de exemplos de treinamento.")
    document_frequency = Counter()
    training_tokens = []
    for question, _ in training:
        tokens = normalize(question)
        training_tokens.append(Counter(tokens))
        document_frequency.update(set(tokens))
    document_count = len(training)
    inverse_frequency = {
        token: math.log((1.0 + document_count) / (1.0 + count)) + 1.0
        for token, count in document_frequency.items()
    }

    def vector(tokens: Counter) -> dict[str, float]:
        return {token: frequency * inverse_frequency.get(token, 1.0)
                for token, frequency in tokens.items()}

    training_vectors = [vector(tokens) for tokens in training_tokens]
    training_norms = [
        math.sqrt(sum(value * value for value in values.values()))
        for values in training_vectors
    ]
    answers = []
    for question, _ in questions:
        query = vector(Counter(normalize(question)))
        query_norm = math.sqrt(sum(value * value for value in query.values()))
        best_index = 0
        best_score = -1.0
        for index, candidate in enumerate(training_vectors):
            denominator = query_norm * training_norms[index]
            score = (sum(value * candidate.get(token, 0.0)
                         for token, value in query.items()) / denominator
                     if denominator > 0.0 else 0.0)
            if score > best_score:
                best_score = score
                best_index = index
        answers.append(training[best_index][1])
    return answers


def evaluate_retrieval_baseline(training: list[tuple[str, str]],
                                dialogues: list[tuple[str, str]]) -> dict:
    answers = retrieval_baseline_answers(training, dialogues)
    rows = [metrics(answer, expected)
            for answer, (_, expected) in zip(answers, dialogues)]
    count = len(rows)
    if count == 0:
        raise ValueError("O conjunto de avaliação não pode estar vazio.")
    return {
        "questions": count,
        "exact_matches": sum(row["exact"] for row in rows),
        "exact_match_rate": sum(row["exact"] for row in rows) / count,
        "mean_lexical_f1": sum(row["lexical_f1"] for row in rows) / count,
        "mean_character_similarity": sum(row["character_similarity"] for row in rows) / count,
        "answers": [{"question": question, "expected": expected, "generated": answer, **row}
                    for (question, expected), answer, row in zip(dialogues, answers, rows)],
    }


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
        "by_skill": summarize_rows(dialogues, answers),
    }


def main() -> int:
    if len(sys.argv) not in (6, 8):
        print("Uso: avaliacao_final.py <gerador> <checkpoint> <validacao.txt> <teste.txt> <saida.json> [<gerador_baseline> <checkpoint_baseline>]", file=sys.stderr)
        return 2
    generator, checkpoint, validation_path, test_path, output = map(Path, sys.argv[1:6])
    if not generator.is_file() or not checkpoint.is_file():
        raise FileNotFoundError("Gerador ou checkpoint final não encontrado.")
    combined_validation = read_dialogues(validation_path)
    test = read_dialogues(test_path)
    if len(combined_validation) != 40 or len(test) != 12:
        raise RuntimeError("O banco combinado deve conter 20 perguntas de feedback, 20 de validação independente e 12 de teste.")
    feedback_questions = combined_validation[:20]
    validation = combined_validation[20:]
    validation_result = evaluate_split(generator, checkpoint, validation)
    test_result = evaluate_split(generator, checkpoint, test)
    training_path = validation_path.with_name("treinamento.txt")
    training = read_dialogues(training_path)
    retrieval_validation = evaluate_retrieval_baseline(training, validation)
    retrieval_test = evaluate_retrieval_baseline(training, test)
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
        "feedback_questions_excluded_from_validation": len(feedback_questions),
        "validation_is_independent_of_feedback": True,
        "pretrained_weights": False,
        "validation": validation_result,
        "reserved_test": test_result,
        "retrieval_baseline": {
            "method": "tfidf_cosine_nearest_training_question",
            "validation": retrieval_validation,
            "reserved_test": retrieval_test,
            "candidate_minus_baseline_test_exact_matches": test_result["exact_matches"] - retrieval_test["exact_matches"],
            "candidate_minus_baseline_test_lexical_f1": test_result["mean_lexical_f1"] - retrieval_test["mean_lexical_f1"],
            "candidate_minus_baseline_test_character_similarity": test_result["mean_character_similarity"] - retrieval_test["mean_character_similarity"],
        },
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
        f"- Validação independente (20 perguntas; feedback excluído): {validation_result['exact_matches']}/{validation_result['questions']} exatas; "
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
    print(f"Validação independente: {validation_result['exact_matches']}/20; teste: {test_result['exact_matches']}/12; determinístico: sim")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
