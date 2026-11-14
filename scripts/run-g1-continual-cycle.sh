#!/usr/bin/env bash
set -Eeuo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: run-g1-continual-cycle.sh <state-dir>" >&2
  exit 2
fi

state_dir="$1"
root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

train_bin="${MOROK_TRAIN_BIN:-./build/morok-train}"
generate_bin="${MOROK_GENERATE_BIN:-./build/morok_generate}"
corpus="tests/fixtures/continual"
checkpoint="$state_dir/g1-continual-resume.morokckp"
best_model="$state_dir/g1-continual-best.morokckp"
metrics="$state_dir/metrics.jsonl"
summary="$state_dir/cycle-summary.jsonl"
generated="$state_dir/generated.txt"
epoch_file="$state_dir/completed-epochs.txt"
status_file="$state_dir/status.json"
goal_file="$state_dir/goal-reached.txt"

mkdir -p "$state_dir"

if [[ -f "$goal_file" ]]; then
  echo "The target response was already reached; no additional training is needed."
  exit 0
fi

completed_epochs=0
if [[ -f "$epoch_file" ]]; then
  read -r completed_epochs < "$epoch_file"
fi
[[ "$completed_epochs" =~ ^[0-9]+$ ]] || { echo "invalid persisted epoch counter" >&2; exit 1; }

max_total_epochs="${MOROK_MAX_TOTAL_EPOCHS:-500}"
epochs_per_cycle="${MOROK_EPOCHS_PER_CYCLE:-10}"
[[ "$max_total_epochs" =~ ^[1-9][0-9]*$ ]] || exit 2
[[ "$epochs_per_cycle" =~ ^[1-9][0-9]*$ ]] || exit 2

if (( completed_epochs >= max_total_epochs )); then
  printf '{"status":"budget_exhausted","completed_epochs":%d,"max_total_epochs":%d}\n' \
    "$completed_epochs" "$max_total_epochs" > "$status_file"
  echo "Training budget exhausted without reaching the target."
  exit 0
fi

target_epochs=$((completed_epochs + epochs_per_cycle))
if (( target_epochs > max_total_epochs )); then
  target_epochs="$max_total_epochs"
fi

args=("$corpus" "$target_epochs" "42"
  --checkpoint "$checkpoint"
  --log "$metrics"
  --export-best "$best_model"
  --patience 10000
  --truncation 64
  --optimizer adamw
  --learning-rate 0.001)

if [[ -s "$checkpoint" ]]; then
  args+=(--resume "$checkpoint")
fi

"$train_bin" "${args[@]}" | tee "$summary"
test -s "$checkpoint"
test -s "$best_model"
test -s "$metrics"
printf '%s\n' "$target_epochs" > "$epoch_file"

"$generate_bin" "$best_model" "$corpus/prompt.txt" "$generated" 128
test -s "$generated"

if python3 - "$generated" <<'PY'
from pathlib import Path
import re
import sys

text = Path(sys.argv[1]).read_bytes().decode("utf-8", errors="strict")
normalized = " ".join(text.casefold().split())
# Narrow lexical gate only; this does not establish semantic correctness.
positive = re.search(r"(?<![a-zà-ÿ])azul(?![a-zà-ÿ])", normalized) is not None
negated = re.search(r"\b(?:nao|não)\b.{0,24}\bazul\b", normalized) is not None
raise SystemExit(0 if positive and not negated else 1)
PY
then
  printf 'accepted_at_epoch=%s\ncriterion=generated continuation contains the expected token "azul"\n' \
    "$target_epochs" > "$goal_file"
  state="goal_reached"
else
  state="training"
fi

if [[ "$state" != "goal_reached" ]] && (( target_epochs >= max_total_epochs )); then
  state="budget_exhausted"
fi

export MOROK_STATE="$state" MOROK_EPOCHS="$target_epochs" MOROK_MAX_EPOCHS="$max_total_epochs"
python3 - "$state_dir" <<'PY'
import hashlib
import json
import os
from pathlib import Path
import sys

out = Path(sys.argv[1])
files = sorted(Path("tests/fixtures/continual").glob("doc-*.txt"))
digest = hashlib.sha256()
for path in files:
    digest.update(path.name.encode("utf-8"))
    digest.update(path.read_bytes())
status = {
    "status": os.environ["MOROK_STATE"],
    "completed_epochs": int(os.environ["MOROK_EPOCHS"]),
    "maximum_epochs": int(os.environ["MOROK_MAX_EPOCHS"]),
    "trainable_parameters": 1000,
    "corpus_documents": len(files),
    "corpus_sha256": digest.hexdigest(),
    "acceptance": {
        "criterion": "generated continuation contains the token azul and no nearby negation",
        "scope": "lexical smoke check only; not a semantic evaluation"
    },
    "limitations": [
        "The curated fixture contains a small set of authored Portuguese paraphrases.",
        "A lexical match does not prove that the model understands the question.",
        "The workflow cannot enumerate all possible synonyms or paraphrases.",
        "The model may memorize the fixed prompt and corpus."
    ]
}
(out / "status.json").write_text(json.dumps(status, ensure_ascii=False, indent=2) + "\n")
PY

if [[ "$state" == "goal_reached" ]]; then
  echo "The narrow lexical acceptance criterion was reached."
elif [[ "$state" == "budget_exhausted" ]]; then
  echo "Maximum epoch budget reached without satisfying the target."
else
  echo "Cycle completed; the next scheduled run will resume from the saved checkpoint."
fi
