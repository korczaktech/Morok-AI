#!/usr/bin/env bash
set -Eeuo pipefail

if [[ $# -lt 1 || $# -gt 3 ]]; then
  echo "usage: run-g1-first-training.sh <output-dir> [epochs=50] [seed=42]" >&2
  exit 2
fi

output_dir="$1"
epochs="${2:-50}"
seed="${3:-42}"

[[ "$epochs" =~ ^[1-9][0-9]{0,4}$ ]] || { echo "epochs must be 1..99999" >&2; exit 2; }
[[ "$seed" =~ ^[0-9]+$ ]] || { echo "seed must be an unsigned integer" >&2; exit 2; }
(( epochs <= 100000 )) || { echo "epochs exceeds supported limit" >&2; exit 2; }

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

train_bin="${MOROK_TRAIN_BIN:-./build/morok-train}"
generate_bin="${MOROK_GENERATE_BIN:-./build/morok_generate}"
corpus="tests/fixtures/benchmark"
checkpoint="$output_dir/g1-resume.morokckp"
best_model="$output_dir/g1-best.morokckp"
metrics="$output_dir/metrics.jsonl"
summary="$output_dir/training-summary.jsonl"
prompt="$corpus/doc-01.txt"
generated="$output_dir/generated.txt"

mkdir -p "$output_dir"
rm -f "$checkpoint" "$best_model" "$metrics" "$summary" "$generated"

echo "Running the first complete G1 training pass on the versioned synthetic fixture."
/usr/bin/time -v -o "$output_dir/resource-usage.txt" \
  "$train_bin" "$corpus" "$epochs" "$seed" \
  --checkpoint "$checkpoint" \
  --log "$metrics" \
  --export-best "$best_model" \
  --patience 5 \
  --truncation 64 \
  --optimizer adamw \
  --learning-rate 0.001 | tee "$summary"

test -s "$checkpoint"
test -s "$best_model"
test -s "$metrics"
grep -q '"event":"finished"' "$metrics"

# A second invocation must load the epoch-boundary checkpoint and preserve the
# corpus/config identity. max_epochs is intentionally extended by one.
resume_epochs=$((epochs + 1))
"$train_bin" "$corpus" "$resume_epochs" "$seed" \
  --resume "$checkpoint" \
  --checkpoint "$checkpoint" \
  --log "$metrics" \
  --export-best "$best_model" \
  --patience 5 \
  --truncation 64 \
  --optimizer adamw \
  --learning-rate 0.001 | tee -a "$summary"

grep -q '"resumed":true' "$summary"
"$generate_bin" "$best_model" "$prompt" "$generated" 128
test -s "$generated"
python3 - "$generated" <<'PY'
from pathlib import Path
import sys
data = Path(sys.argv[1]).read_bytes()
data.decode("utf-8", errors="strict")
if len(data) > 128:
    raise SystemExit("generated output exceeded the configured byte limit")
PY

sha256sum "$corpus"/*.txt > "$output_dir/corpus-files.sha256"
sha256sum "$metrics" "$checkpoint" "$best_model" "$generated" > "$output_dir/artifacts.sha256"

python3 - "$output_dir" "$epochs" "$seed" <<'PY'
import hashlib
import json
import os
import pathlib
import platform
import subprocess
import sys

out = pathlib.Path(sys.argv[1])
epochs = int(sys.argv[2])
seed = int(sys.argv[3])
root = pathlib.Path.cwd()
files = sorted((root / "tests/fixtures/benchmark").glob("*.txt"))
digest = hashlib.sha256()
for path in files:
    digest.update(path.name.encode("utf-8"))
    digest.update(path.read_bytes())
try:
    cpu = subprocess.check_output(
        "grep -m1 'model name' /proc/cpuinfo | cut -d: -f2-",
        shell=True, text=True).strip()
except Exception:
    cpu = platform.processor()
try:
    compiler = subprocess.check_output(["c++", "--version"], text=True).splitlines()[0]
except Exception:
    compiler = "unknown"
manifest = {
    "schema_version": 1,
    "experiment_id": os.environ.get("GITHUB_RUN_ID", "local-stage20"),
    "status": "completed",
    "purpose": "first complete G1 engineering training run; synthetic fixture only",
    "model": {"generation": "G1", "trainable_parameters": 1000, "language_standard": "C++20"},
    "data": {
        "name": "Morok versioned synthetic benchmark fixture",
        "path": "tests/fixtures/benchmark",
        "license": "CC0-1.0",
        "source": "repository-generated technical fixture",
        "documents": len(files),
        "bytes": sum(p.stat().st_size for p in files),
        "ordered_name_and_content_sha256": digest.hexdigest(),
        "split_policy": "deterministic document-level train/validation/test split, seed-bound"
    },
    "training": {
        "epochs_requested": epochs,
        "seed": seed,
        "optimizer": "AdamW",
        "learning_rate": 0.001,
        "bptt_truncation_bytes": 64,
        "early_stopping_patience": 5,
        "resume_verified": True
    },
    "code": {
        "commit": os.environ.get("GITHUB_SHA", "local"),
        "compiler": compiler,
        "build_type": "Release"
    },
    "environment": {
        "os": platform.platform(),
        "architecture": platform.machine(),
        "cpu": cpu,
        "logical_cpu_count": os.cpu_count()
    },
    "artifacts": {
        "metrics": "metrics.jsonl",
        "resume_checkpoint": "g1-resume.morokckp",
        "best_inference_checkpoint": "g1-best.morokckp",
        "generated_output": "generated.txt",
        "resource_usage": "resource-usage.txt"
    },
    "limitations": [
        "Synthetic fixture is not a natural-language corpus.",
        "Results do not establish linguistic quality or generalization.",
        "CI runner memory measurements do not validate operation on physical 4 GB hardware.",
        "A real, licensed corpus campaign remains necessary for scientific validation."
    ]
}
(out / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n")
PY

echo "G1 first-training run completed; manifest and reproducibility artifacts are in $output_dir."
