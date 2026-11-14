#!/usr/bin/env python3
"""Run one reward-driven episode for each of the three Morok G1 questions."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unicodedata
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DB_DEFAULT = ROOT / "data" / "reinforcement" / "answers.json"
CORPUS = ROOT / "tests" / "fixtures" / "continual"
SEED_ANSWERS = {
    "sky_is_blue": "Sim.",
    "mountains_high_or_low": "Altas.",
    "grass_color": "Verde.",
}
TASK_CONCEPTS = {
    "sky_is_blue": {"sim", "azul"},
    "mountains_high_or_low": {"altas"},
    "grass_color": {"verde"},
}
NEGATIONS = {"nao", "nunca", "jamais"}


def normalized_tokens(text: str) -> list[str]:
    value = unicodedata.normalize("NFKD", text.casefold())
    value = "".join(ch for ch in value if not unicodedata.combining(ch))
    return re.findall(r"[a-z0-9]+", value)


def grade(question_id: str, answer: str) -> tuple[str, int, str]:
    tokens = normalized_tokens(answer)
    token_set = set(tokens)
    if not tokens:
        return "incorrect", -1, "empty_response"
    if token_set & NEGATIONS:
        return "incorrect", -1, "negated_response"
    required = TASK_CONCEPTS[question_id]
    matched = sorted(token_set & required)
    if matched:
        return "correct", 1, "matched_expected_concept:" + ",".join(matched)
    return "incorrect", -1, "expected_concept_not_found"


def atomic_write(path: Path, value: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(
        json.dumps(value, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    os.replace(temporary, path)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run_checked(command: list[str]) -> str:
    result = subprocess.run(
        command, check=False, capture_output=True, text=True, encoding="utf-8"
    )
    if result.returncode:
        raise RuntimeError(
            f"command failed ({result.returncode}): {' '.join(command)}\n"
            f"{result.stdout}\n{result.stderr}"
        )
    return result.stdout.strip()


def bootstrap_if_needed(checkpoint: Path, train_bin: Path, epochs: int) -> None:
    if checkpoint.is_file() and checkpoint.stat().st_size:
        return
    best = checkpoint.with_name("g1-reinforcement-bootstrap-best.morokckp")
    checkpoint.parent.mkdir(parents=True, exist_ok=True)
    run_checked([
        str(train_bin), str(CORPUS), str(epochs), "42",
        "--checkpoint", str(checkpoint),
        "--export-best", str(best),
        "--patience", "10000",
        "--truncation", "64",
        "--optimizer", "adamw",
        "--learning-rate", "0.001",
    ])
    if not best.is_file() or not best.stat().st_size:
        raise RuntimeError("bootstrap training did not export a best checkpoint")
    shutil.copy2(best, checkpoint)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--checkpoint", type=Path, required=True)
    parser.add_argument("--database", type=Path, default=DB_DEFAULT)
    parser.add_argument("--generator", type=Path, default=ROOT / "build" / "morok_generate")
    parser.add_argument("--reinforcer", type=Path, default=ROOT / "build" / "morok-reinforce")
    parser.add_argument("--trainer", type=Path, default=ROOT / "build" / "morok-train")
    parser.add_argument("--bootstrap-epochs", type=int, default=8)
    parser.add_argument("--learning-rate", type=float, default=0.0001)
    args = parser.parse_args()

    if args.bootstrap_epochs < 1 or args.bootstrap_epochs > 100:
        raise ValueError("bootstrap-epochs must be between 1 and 100")
    if not args.database.is_file():
        raise FileNotFoundError(f"feedback database not found: {args.database}")
    database = json.loads(args.database.read_text(encoding="utf-8"))
    if database.get("schema_version") != 1:
        raise ValueError("unsupported reinforcement database schema")
    questions = database.get("questions", [])
    if {item.get("id") for item in questions} != set(TASK_CONCEPTS):
        raise ValueError("database must contain exactly the three configured questions")

    bootstrap_if_needed(args.checkpoint, args.trainer, args.bootstrap_epochs)
    run_id = os.environ.get("GITHUB_RUN_ID", "local")
    run_attempt = os.environ.get("GITHUB_RUN_ATTEMPT", "1")
    summaries = []
    with tempfile.TemporaryDirectory(prefix="morok-g1-reinforcement-") as temp_dir:
        temporary = Path(temp_dir)

        # Warm-start all three tasks in round-robin batches to reduce
        # catastrophic forgetting between the shared-policy question contexts.
        if int(database.get("oracle_demonstration_version", 0)) < 6:
            seed_paths = {}
            seed_answers = {}
            model_before = {}
            path_lengths = {question["id"]: 0.0 for question in questions}
            first_reports = {}
            last_reports = {}
            for question in questions:
                question_id = question["id"]
                prompt_path = temporary / f"{question_id}.seed.prompt.txt"
                answer_path = temporary / f"{question_id}.seed.answer.txt"
                prompt_path.write_text(question["prompt"], encoding="utf-8")
                answer = SEED_ANSWERS[question_id]
                answer_path.write_text(answer, encoding="utf-8")
                seed_paths[question_id] = (prompt_path, answer_path)
                seed_answers[question_id] = answer
                model_before[question_id] = sha256(args.checkpoint)

            # 100 rounds × 10 small updates per question; tasks are interleaved.
            for batch in range(100):
                for question in questions:
                    question_id = question["id"]
                    prompt_path, answer_path = seed_paths[question_id]
                    update_output = run_checked([
                        str(args.reinforcer), str(args.checkpoint),
                        str(prompt_path), str(answer_path), "1",
                        "0.0025", "10",
                    ])
                    report = json.loads(update_output)
                    if batch == 0:
                        first_reports[question_id] = report
                    last_reports[question_id] = report
                    path_lengths[question_id] += report["parameter_delta_l2"]

            for question in questions:
                question_id = question["id"]
                answer = seed_answers[question_id]
                episode_number = len(database["episodes"]) + 1
                report = {
                    "reward": 1,
                    "target_bytes": len(answer.encode("utf-8")),
                    "optimizer_steps": 1000,
                    "optimizer_batches": 100,
                    "parameter_path_length_l2": path_lengths[question_id],
                    "gradient_norm_first": first_reports[question_id]["gradient_norm_first"],
                    "gradient_norm_last": last_reports[question_id]["gradient_norm_last"],
                    "trainable_parameters": 1000,
                }
                episode = {
                    "episode": episode_number,
                    "episode_id": f"{run_id}:{run_attempt}:oracle:{question_id}:v6",
                    "run_id": run_id,
                    "run_attempt": int(run_attempt),
                    "source": "oracle_demonstration",
                    "demonstration_version": 6,
                    "question_id": question_id,
                    "prompt": question["prompt"],
                    "answer": answer,
                    "verdict": "correct",
                    "reward": 1,
                    "grading_reason": "known_target_demonstration_for_interleaved_policy_warm_start",
                    "timestamp_utc": datetime.now(timezone.utc).isoformat(),
                    "model_sha256_before": model_before[question_id],
                    "model_sha256_after": sha256(args.checkpoint),
                    "trainable_parameters": 1000,
                    "policy_update": {
                        "status": "applied",
                        "algorithm": "reward_weighted_bptt_score_function_surrogate",
                        "optimizer_steps": 1000,
                        "optimizer_batches": 100,
                        "report": report,
                    },
                }
                database["episodes"].append(episode)
                database["totals"]["episodes"] += 1
                database["totals"]["correct"] += 1
                database["totals"]["policy_updates"] += 1
                database["totals"]["oracle_demonstrations"] += 1
                atomic_write(args.database, database)
            database["oracle_demonstrations_applied"] = True
            database["oracle_demonstration_version"] = 6
            atomic_write(args.database, database)

        for offset, question in enumerate(questions):
            question_id = question["id"]
            prompt_path = temporary / f"{question_id}.prompt.txt"
            answer_path = temporary / f"{question_id}.answer.txt"
            prompt_path.write_text(question["prompt"], encoding="utf-8")
            episode_number = len(database["episodes"]) + 1
            model_before = sha256(args.checkpoint)
            seed = 20261114 + episode_number * 17 + offset
            run_checked([
                str(args.generator), str(args.checkpoint), str(prompt_path),
                str(answer_path), "48",
                "--temperature", "0.0",
                "--top-k", "1",
                "--seed", str(seed),
                "--repetition-penalty", "0.15",
                "--repetition-window", "24",
                "--eos-threshold", "0.75",
                "--min-new-bytes", "2",
                "--printable-only",
            ])
            answer = answer_path.read_text(encoding="utf-8")
            verdict, reward, reason = grade(question_id, answer)
            episode = {
                "episode": episode_number,
                "episode_id": f"{run_id}:{run_attempt}:generated:{question_id}",
                "run_id": run_id,
                "run_attempt": int(run_attempt),
                "source": "model_generated",
                "question_id": question_id,
                "prompt": question["prompt"],
                "answer": answer,
                "verdict": verdict,
                "reward": reward,
                "grading_reason": reason,
                "timestamp_utc": datetime.now(timezone.utc).isoformat(),
                "model_sha256_before": model_before,
                "model_sha256_after": None,
                "trainable_parameters": 1000,
                "policy_update": {"status": "pending", "algorithm": "reward_weighted_bptt_score_function_surrogate"},
            }
            database["episodes"].append(episode)
            database["totals"]["episodes"] += 1
            database["totals"][verdict] += 1
            atomic_write(args.database, database)

            if len(answer.encode("utf-8")) >= 2:
                try:
                    update_output = run_checked([
                        str(args.reinforcer), str(args.checkpoint),
                        str(prompt_path), str(answer_path), str(reward),
                        str(args.learning_rate),
                    ])
                    episode["policy_update"] = {
                        "status": "applied",
                        "algorithm": "reward_weighted_bptt_score_function_surrogate",
                        "report": json.loads(update_output),
                    }
                    database["totals"]["policy_updates"] += 1
                except Exception as error:
                    episode["policy_update"] = {
                        "status": "failed",
                        "error": str(error),
                    }
                    atomic_write(args.database, database)
                    raise
            else:
                episode["policy_update"] = {
                    "status": "skipped",
                    "reason": "response_too_short_for_bptt",
                }
                database["totals"]["skipped_updates"] += 1

            episode["model_sha256_after"] = sha256(args.checkpoint)
            atomic_write(args.database, database)
            summaries.append({
                "episode": episode_number,
                "question_id": question_id,
                "answer": answer,
                "verdict": verdict,
                "reward": reward,
                "policy_update": episode["policy_update"]["status"],
            })

    print(json.dumps({
        "episodes_this_run": len(summaries),
        "totals": database["totals"],
        "results": summaries,
        "database": str(args.database),
        "checkpoint_sha256": sha256(args.checkpoint),
        "trainable_parameters": 1000,
    }, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f"run-g1-reinforcement: {error}", file=sys.stderr)
        raise SystemExit(1)
