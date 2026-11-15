#!/usr/bin/env python3
"""Train each selected G1 profile on the curated Portuguese QA corpus and evaluate it."""
import csv
import os
import subprocess
import sys
from pathlib import Path

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
profiles = [name for name in ("Tsukuyomi", "Apollo", "Mokosh")
            if os.environ.get("G1_MODEL_" + name.upper(), "false").lower() == "true"]
question_map = {
    "SKY": "sky_is_blue",
    "GRASS": "grass_color",
    "MOUNTAINS": "mountains_high_or_low",
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
for profile in profiles:
    model_dir = out / profile.lower()
    model_dir.mkdir(parents=True, exist_ok=True)
    checkpoint = model_dir / "model.morokckp"
    best = model_dir / "best.morokckp"
    metrics = model_dir / "metrics.jsonl"
    print(f"Training {profile}: {epochs} epochs requested", flush=True)
    subprocess.run([
        str(train), str(corpus), str(epochs), str(seed),
        "--checkpoint", str(checkpoint),
        "--log", str(metrics),
        "--export-best", str(best),
        "--patience", "1000000",
        "--truncation", "64",
        "--optimizer", "adamw",
        "--learning-rate", "0.001",
    ], check=True)
    subprocess.run([
        str(evaluate), str(best), str(results), profile,
        ",".join(questions), str(attempts),
    ], check=True)

with results.open(encoding="utf-8", newline="") as stream:
    rows = list(csv.DictReader(stream, delimiter="\t"))
expected = len(profiles) * len(questions) * attempts
if len(rows) != expected:
    raise SystemExit(f"Expected {expected} evaluation rows, got {len(rows)}")
print(f"Completed {len(profiles)} model(s), {len(questions)} question(s), "
      f"{attempts} attempt(s) each: {len(rows)} evaluations.")
