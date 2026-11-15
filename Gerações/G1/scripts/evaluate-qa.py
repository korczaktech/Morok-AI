#!/usr/bin/env python3
"""Run a small held-out QA diagnostic; token F1 is not semantic evaluation."""
from __future__ import annotations

import json
import re
import subprocess
import sys
import tempfile
import unicodedata
from pathlib import Path


def tokens(text: str) -> list[str]:
    normalized = unicodedata.normalize("NFKD", text.casefold())
    normalized = "".join(ch for ch in normalized if not unicodedata.combining(ch))
    return re.findall(r"[a-z0-9]+", normalized)


def token_f1(prediction: str, reference: str) -> float:
    predicted = tokens(prediction)
    expected = tokens(reference)
    if not predicted or not expected:
        return 0.0
    remaining = expected.copy()
    overlap = 0
    for token in predicted:
        if token in remaining:
            remaining.remove(token)
            overlap += 1
    if overlap == 0:
        return 0.0
    precision = overlap / len(predicted)
    recall = overlap / len(expected)
    return 2.0 * precision * recall / (precision + recall)


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: evaluate-g1-qa.py <checkpoint> <report.json>", file=sys.stderr)
        return 2
    checkpoint = Path(sys.argv[1])
    report_path = Path(sys.argv[2])
    generator = Path(__file__).resolve().parents[1] / "build" / "morok_generate"
    cases_path = Path(__file__).resolve().parents[1] / "shared" / "tests" / "fixtures" / "continual-eval" / "cases.json"
    if not checkpoint.is_file() or not generator.is_file():
        raise SystemExit("checkpoint or morok_generate executable not found")
    cases = json.loads(cases_path.read_text(encoding="utf-8"))
    results = []
    with tempfile.TemporaryDirectory(prefix="morok-g1-qa-") as temporary:
        temp = Path(temporary)
        for case in cases["cases"]:
            prompt = temp / f"{case['id']}.prompt.txt"
            output = temp / f"{case['id']}.generated.txt"
            prompt.write_text(case["prompt"], encoding="utf-8")
            process = subprocess.run(
                [str(generator), str(checkpoint), str(prompt), str(output), "160",
                 "--repetition-penalty", "0.5", "--repetition-window", "64",
                 "--eos-threshold", "0.75", "--min-new-bytes", "1"],
                check=False, capture_output=True, text=True, encoding="utf-8"
            )
            if process.returncode != 0:
                raise RuntimeError(f"generation failed for {case['id']}: {process.stderr.strip()}")
            generated = output.read_text(encoding="utf-8")
            results.append({
                "id": case["id"],
                "prompt": case["prompt"],
                "reference": case["reference"],
                "generated": generated,
                "exact_match": " ".join(tokens(generated)) == " ".join(tokens(case["reference"])),
                "token_f1": token_f1(generated, case["reference"]),
                "generated_bytes": len(generated.encode("utf-8"))
            })
    report = {
        "evaluation": "held_out_narrow_task_diagnostic",
        "metric_interpretation": "exact match and token F1 measure lexical overlap only; they do not establish semantic understanding",
        "case_count": len(results),
        "exact_match_rate": sum(item["exact_match"] for item in results) / len(results) if results else 0.0,
        "mean_token_f1": sum(item["token_f1"] for item in results) / len(results) if results else 0.0,
        "semantic_acceptance": "not_established",
        "results": results
    }
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: report[key] for key in (
        "case_count", "exact_match_rate", "mean_token_f1", "semantic_acceptance"
    )}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f"evaluate-g1-qa: {error}", file=sys.stderr)
        raise SystemExit(1)
