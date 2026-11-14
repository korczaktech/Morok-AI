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

mkdir -p "$state_dir"

# EOS changed the meaning of the final 29 model parameters. Never resume an
# older checkpoint whose architecture identifier predates the EOS head.
expected_arch="RLAH-G1-fastslow-d11-bit8-eos"
expected_corpus_manifest_version="2"
current_corpus_sha="$(python3 - <<'PY'
import hashlib
from pathlib import Path
digest = hashlib.sha256()
for path in sorted(Path("tests/fixtures/continual").glob("doc-*.txt")):
    digest.update(path.name.encode("utf-8"))
    digest.update(path.read_bytes())
print(digest.hexdigest())
PY
)"
current_corpus_documents="$(find "$corpus" -maxdepth 1 -type f -name 'doc-*.txt' | wc -l | tr -d ' ')"
state_exists=false
if [[ -e "$checkpoint" || -e "$epoch_file" || -e "$status_file" ]]; then
  state_exists=true
fi
checkpoint_compatible=false
if [[ "$state_exists" == "true" && -s "$checkpoint" ]]; then
  if python3 - "$status_file" "$expected_arch" "$current_corpus_sha" "$current_corpus_documents" "$expected_corpus_manifest_version" <<'PY'
import json
import sys
from pathlib import Path
try:
    status = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
except (OSError, ValueError):
    raise SystemExit(1)
compatible = (
    status.get("architecture_id") == sys.argv[2]
    and status.get("corpus_sha256") == sys.argv[3]
    and str(status.get("corpus_documents", 0)) == sys.argv[4]
    and str(status.get("corpus_manifest_version", 0)) == sys.argv[5]
)
raise SystemExit(0 if compatible else 1)
PY
  then
    checkpoint_compatible=true
  fi
fi

if [[ ( -s "$checkpoint" || -e "$epoch_file" || -e "$status_file" ) && "$checkpoint_compatible" != "true" ]]; then
  echo "Discarding incompatible checkpoint and its dependent training state."
  rm -f "$checkpoint" "$best_model" "$metrics" "$summary" "$generated" "$epoch_file" "$status_file"
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
  echo "Training budget exhausted; no semantic success was established."
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

"$generate_bin" "$best_model" "tests/fixtures/continual-prompt.txt" "$generated" 128 --repetition-penalty 0.5 --repetition-window 64 --eos-threshold 0.75 --min-new-bytes 8
test -s "$generated"

qa_report="$state_dir/qa-evaluation.json"
python3 scripts/evaluate-g1-qa.py "$best_model" "$qa_report"
test -s "$qa_report"

# This is a diagnostic only. A lexical match is not semantic understanding
# and must never stop training or mark the model as having learned the task.
lexical_match=false
if python3 - "$generated" <<'PY'
from pathlib import Path
import re
import sys

text = Path(sys.argv[1]).read_bytes().decode("utf-8", errors="strict")
normalized = " ".join(text.casefold().split())
positive = re.search(r"(?<![a-zà-ÿ])azul(?![a-zà-ÿ])", normalized) is not None
negated = re.search(r"\b(?:nao|não)\b.{0,24}\bazul\b", normalized) is not None
raise SystemExit(0 if positive and not negated else 1)
PY
then
  lexical_match=true
fi

if (( target_epochs >= max_total_epochs )); then
  state="budget_exhausted"
else
  state="training"
fi

export MOROK_STATE="$state" MOROK_EPOCHS="$target_epochs" MOROK_MAX_EPOCHS="$max_total_epochs"
export MOROK_LEXICAL_MATCH="$lexical_match"
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
    "architecture_id": "RLAH-G1-fastslow-d11-bit8-eos",
    "corpus_manifest_version": 2,
    "qa_evaluation": "token-overlap diagnostic only; semantic acceptance not established",
    "architecture_parameters": 1000,
    "corpus_documents": len(files),
    "corpus_sha256": digest.hexdigest(),
    "diagnostics": {
        "contains_expected_lexical_answer": os.environ["MOROK_LEXICAL_MATCH"] == "true",
        "interpretation": "lexical smoke check only; not semantic understanding"
    },
    "acceptance": {
        "status": "not_established",
        "reason": "No held-out semantic evaluation is implemented; lexical matching cannot establish understanding."
    },
    "limitations": [
        "The curated fixture contains a small set of authored Portuguese paraphrases.",
        "The 29 EOS-head parameters are updated by continuation and document-boundary objectives.",
        "The model may memorize the fixed prompt and corpus.",
        "A valid UTF-8 continuation and a lexical match do not prove semantic correctness."
    ]
}
(out / "status.json").write_text(json.dumps(status, ensure_ascii=False, indent=2) + "\n")
PY

if [[ "$state" == "budget_exhausted" ]]; then
  echo "Maximum epoch budget reached. Semantic success was not established."
else
  echo "Cycle completed; the next scheduled run will resume from the saved checkpoint."
fi
