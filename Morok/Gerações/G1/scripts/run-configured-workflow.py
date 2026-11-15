#!/usr/bin/env python3
"""Train each selected G1 profile on the curated Portuguese QA corpus and evaluate it."""
import csv
import os
import subprocess
import sys
from pathlib import Path
import json
import shutil

root = Path(__file__).resolve().parents[3]
build = root / "build" / "g1"
train = Path(os.environ.get("MOROK_TRAIN_BIN", build / "morok-train"))
evaluate = build / "morok-g1-profile-evaluate"
corpus = root / "Gerações/G1/shared/tests/fixtures/continual"
out = root / "artifacts/g1-first-training"
out.mkdir(parents=True, exist_ok=True)

attempts = int(os.environ["G1_ATTEMPTS"])
epochs = int(os.environ["G1_EPOCHS"])
seed = int(os.environ.get("G1_SEED", "42"))
profile_inputs = (
    ("Tsukuyomi", "TSUKUYOMI"),
    ("Apollo", "APOLLO"),
    ("Mokosh", "MOKOSH"),
    ("TsukuyomiMax", "TSUKUYOMI_MAX"),
)
profiles = [name for name, key in profile_inputs
            if os.environ.get("G1_MODEL_" + key, "false").lower() == "true"]
question_map = {
    "SKY": "sky_is_blue",
    "GRASS": "grass_color",
    "MOUNTAINS": "mountains_high_or_low",
    "GREETING": "greeting",
    "ENGLISH_SKY": "english_sky",
    "SPANISH_SKY": "spanish_sky",
    "MULTI_TURN": "multi_turn",
}
questions = [qid for key, qid in question_map.items()
             if os.environ.get("G1_QUESTION_" + key, "false").lower() == "true"]
if not 1 <= attempts <= 1000:
    raise SystemExit("G1_ATTEMPTS must be in 1..1000")
if not 1 <= epochs <= 1000000:
    raise SystemExit("G1_EPOCHS must be in 1..1000000")
if not profiles:
    raise SystemExit("Select at least one G1 model.")
if not questions:
    raise SystemExit("Select at least one question.")

results = out / "profile-evaluation.tsv"
results.write_text("", encoding="utf-8")
for profile_index, profile in enumerate(profiles):
    model_dir = out / profile.lower()
    model_dir.mkdir(parents=True, exist_ok=True)
    checkpoint = model_dir / "model.morokckp"
    candidate = model_dir / "candidate.morokckp"
    approved = model_dir / "approved.morokckp"
    gate_report = model_dir / "quality-gate.json"
    candidate.unlink(missing_ok=True)
    approved.unlink(missing_ok=True)
    gate_report.unlink(missing_ok=True)
    metrics = model_dir / "metrics.jsonl"
    profile_seed = (seed + profile_index) % (1 << 64)
    print(f"Training {profile}: {epochs} epochs requested (seed={profile_seed})", flush=True)
    subprocess.run([
        str(train), str(corpus), str(epochs), str(profile_seed),
        "--checkpoint", str(checkpoint),
        "--log", str(metrics),
        "--export-best", str(candidate),
        "--patience", "50",
        "--truncation", "256",
        "--optimizer", "adamw",
        "--learning-rate", "0.001",
    ], check=True)
    subprocess.run([
        str(evaluate), str(candidate), str(results), profile,
        ",".join(questions), str(attempts),
    ], check=True)

    # Validation BPB selects a candidate; it does not establish answer quality.
    with results.open(encoding="utf-8", newline="") as stream:
        profile_rows = [row for row in csv.DictReader(stream, delimiter="\t")
                        if row["profile"] == profile]
    required_questions = [qid for qid in ("sky_is_blue", "grass_color", "mountains_high_or_low") if qid in questions]
    selected_rows = [row for row in profile_rows if row["question_id"] in required_questions]
    expected_rows = len(required_questions) * attempts
    checks = []
    for row in selected_rows:
        answer = row["answer"]
        letters = sum(ch.isalpha() for ch in answer)
        printable = all(ch.isprintable() or ch in "\t\n\r" for ch in answer)
        no_long_repeat = not any(len(answer[i:i + 8]) == 8 and len(set(answer[i:i + 8])) == 1
                                 for i in range(max(0, len(answer) - 7)))
        readable = bool(answer.strip()) and printable and letters / max(1, len(answer)) >= 0.30
        checks.append({
            "question_id": row["question_id"],
            "attempt": int(row["attempt"]),
            "correct": row["verdict"] == "correct",
            "readable": readable,
            "no_long_repeat": no_long_repeat,
        })
    passed = (expected_rows > 0 and len(selected_rows) == expected_rows
              and all(x["correct"] and x["readable"] and x["no_long_repeat"] for x in checks))
    status = "approved" if passed else ("candidate_rejected" if expected_rows else "not_evaluated")
    if passed:
        shutil.copy2(candidate, approved)
    report = {
        "profile": profile,
        "status": status,
        "approval_rule": "100% factual accuracy on selected questions and attempts; "
                         "non-empty readable output; no repeated run of 8 characters",
        "selected_questions": questions,
        "quality_gate_questions": required_questions,
        "attempts_per_question": attempts,
        "evaluations": len(selected_rows),
        "expected_evaluations": expected_rows,
        "correct": sum(x["correct"] for x in checks),
        "readable": sum(x["readable"] for x in checks),
        "no_long_repeat": sum(x["no_long_repeat"] for x in checks),
        "approved_checkpoint": approved.name if passed else None,
        "candidate_checkpoint": candidate.name if candidate.is_file() else None,
        "checks": checks,
    }
    gate_report.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n",
                           encoding="utf-8")
    print(f"Quality gate {profile}: {status} "
          f"({report['correct']}/{expected_rows} correct; "
          f"{report['readable']}/{expected_rows} readable)", flush=True)

with results.open(encoding="utf-8", newline="") as stream:
    rows = list(csv.DictReader(stream, delimiter="\t"))
expected = len(profiles) * len(questions) * attempts
if len(rows) != expected:
    raise SystemExit(f"Expected {expected} evaluation rows, got {len(rows)}")
print(f"Completed {len(profiles)} model(s), {len(questions)} question(s), "
      f"{attempts} attempt(s) each: {len(rows)} evaluations.")
