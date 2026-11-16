#!/usr/bin/env python3
"""Validate the curated Morok G3 JSONL corpus without modifying it."""
from __future__ import annotations

import json
import sys
import unicodedata
from pathlib import Path

REQUIRED = {
    "example_id", "group_id", "category", "question", "answer",
    "source", "language", "dataset_version",
}
EXPECTED_LANGUAGE = "pt-BR"
EXPECTED_VERSION = "g3-ptbr-v1.0"


def fail(message: str) -> None:
    raise SystemExit(f"ERRO: {message}")


def main() -> int:
    if len(sys.argv) != 2:
        fail("uso: validate_corpus.py <arquivo.jsonl>")
    path = Path(sys.argv[1])
    if not path.is_file():
        fail(f"arquivo não encontrado: {path}")

    raw = path.read_bytes()
    try:
        content = raw.decode("utf-8", errors="strict")
    except UnicodeDecodeError as exc:
        fail(f"arquivo não é UTF-8 válido: {exc}")

    records = []
    for line_number, line in enumerate(content.splitlines(), start=1):
        if not line.strip():
            fail(f"linha vazia na linha {line_number}")
        try:
            record = json.loads(line)
        except json.JSONDecodeError as exc:
            fail(f"JSON inválido na linha {line_number}: {exc}")
        if not isinstance(record, dict):
            fail(f"registro da linha {line_number} não é objeto JSON")
        missing = REQUIRED - record.keys()
        if missing:
            fail(f"campos ausentes na linha {line_number}: {sorted(missing)}")
        for field in REQUIRED:
            if not isinstance(record[field], str) or not record[field].strip():
                fail(f"campo {field} vazio ou inválido na linha {line_number}")
        if record["language"] != EXPECTED_LANGUAGE:
            fail(f"idioma inesperado na linha {line_number}")
        if record["dataset_version"] != EXPECTED_VERSION:
            fail(f"versão inesperada na linha {line_number}")
        if not (0.0 <= float(record.get("quality", 1.0)) <= 1.0):
            fail(f"quality fora de [0,1] na linha {line_number}")
        if record.get("sensitive", False):
            fail(f"registro sensível bloqueado na linha {line_number}")
        variants = record.get("question_variants", [])
        if not isinstance(variants, list) or not variants:
            fail(f"question_variants ausente/vazio na linha {line_number}")
        if any(not isinstance(value, str) or not value.strip() for value in variants):
            fail(f"variante vazia ou inválida na linha {line_number}")
        questions = [record["question"], *variants]
        normalized = [unicodedata.normalize("NFC", q).strip() for q in questions]
        if len(set(normalized)) != len(normalized):
            fail(f"perguntas duplicadas dentro do grupo na linha {line_number}")
        records.append(record)

    if len(records) < 3:
        fail("são necessários pelo menos três registros/grupos")
    ids = [record["example_id"] for record in records]
    if len(ids) != len(set(ids)):
        fail("example_id duplicado")
    groups = [record["group_id"] for record in records]
    if len(groups) != len(set(groups)):
        fail("group_id duplicado neste corpus: cada registro deve representar uma intenção")
    all_questions = []
    for record in records:
        all_questions.extend(
            unicodedata.normalize("NFC", q).casefold().strip()
            for q in [record["question"], *record["question_variants"]]
        )
    if len(all_questions) != len(set(all_questions)):
        fail("pergunta idêntica repetida entre intenções diferentes")

    variant_count = sum(len(record["question_variants"]) for record in records)
    categories = sorted({record["category"] for record in records})
    print("Corpus válido.")
    print(f"Registros/intencoes: {len(records)}")
    print(f"Paráfrases curadas: {variant_count}")
    print(f"Perguntas antes da perturbação: {len(records) + variant_count}")
    print(f"Categorias: {len(categories)}")
    print(f"UTF-8: válido; IDs e grupos: únicos; campos: completos.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
