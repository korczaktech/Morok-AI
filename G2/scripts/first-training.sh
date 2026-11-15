#!/usr/bin/env bash
set -Eeuo pipefail
if [[ $# -lt 1 || $# -gt 3 ]]; then
  echo "usage: run-g2-first-training.sh <output-dir> [epochs=10] [seed=42]" >&2
  exit 2
fi
output_dir="$1"
epochs="${2:-10}"
seed="${3:-42}"
[[ "$epochs" =~ ^[1-9][0-9]{0,4}$ ]] || { echo "epochs must be 1..99999" >&2; exit 2; }
[[ "$seed" =~ ^[0-9]+$ ]] || { echo "seed must be an unsigned integer" >&2; exit 2; }
(( epochs < 100000 )) || { echo "epochs must leave room for the resume pass" >&2; exit 2; }
root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$root"
train_bin="${MOROK_TRAIN_G2_BIN:-./build/morok-train-g2}"
generate_bin="${MOROK_GENERATE_G2_BIN:-./build/morok-generate-g2}"
corpus="G2/tests/fixtures/g2-single-document"
prefix="$output_dir/g2-model"
prompt="$output_dir/prompt.txt"
generated="$output_dir/generated.txt"
mkdir -p "$output_dir"
printf 'Usuario: O ceu e azul?\nIA: ' > "$prompt"
rm -f "$prefix".* "$output_dir"/metrics* "$output_dir"/training-summary.txt "$generated"
echo "Training both G2 experts from independent deterministic initializations."
/usr/bin/time -v -o "$output_dir/resource-usage.txt" \
  "$train_bin" "$corpus" "$prefix" "$epochs" "$seed" | tee "$output_dir/training-summary.txt"
test -s "$prefix.expert-a" && test -s "$prefix.expert-b"
resume_epochs=$((epochs + 1))
"$train_bin" "$corpus" "$prefix" "$resume_epochs" "$seed" --resume | tee -a "$output_dir/training-summary.txt"
grep -q 'expert_a_resumed=true' "$output_dir/training-summary.txt"
grep -q 'expert_b_resumed=true' "$output_dir/training-summary.txt"
"$generate_bin" "$prefix" "$prompt" "$generated" 128
test -s "$generated"
python3 - "$generated" <<'PY'
from pathlib import Path
import sys
data = Path(sys.argv[1]).read_bytes()
data.decode("utf-8", errors="strict")
if len(data) > 128:
    raise SystemExit("G2 output exceeded the configured byte limit")
PY
sha256sum "$corpus"/*.txt > "$output_dir/corpus-files.sha256"
sha256sum "$prefix.expert-a" "$prefix.expert-b" "$generated" > "$output_dir/artifacts.sha256"
python3 - "$output_dir" "$epochs" "$seed" <<'PY'
import hashlib, json, os, pathlib, platform, subprocess, sys
out=pathlib.Path(sys.argv[1]); epochs=int(sys.argv[2]); seed=int(sys.argv[3])
root=pathlib.Path.cwd(); corpus=sorted((root/"G2/tests/fixtures/g2-single-document").glob("*.txt"))
digest=hashlib.sha256()
for p in corpus:
    digest.update(p.name.encode()); digest.update(p.read_bytes())
def metric(name):
    lines=(out/"training-summary.txt").read_text().splitlines()
    for line in reversed(lines):
        if line.startswith(name+"="):
            try: return float(line.split("=",1)[1])
            except ValueError: pass
    return None
try: cpu=subprocess.check_output("grep -m1 'model name' /proc/cpuinfo | cut -d: -f2-",shell=True,text=True).strip()
except Exception: cpu=platform.processor()
manifest={
 "schema_version":1,"experiment_id":os.environ.get("GITHUB_RUN_ID","local-g2"),
 "status":"completed","purpose":"three-document G2 EOS-boundary engineering smoke run; not a representative language corpus",
 "model":{"generation":"G2","trainable_parameters":2000,"architecture":"two independent 1000-parameter G1 experts","language_standard":"C++20"},
 "data":{"path":"G2/tests/fixtures/g2-single-document","license":"CC0-1.0","documents":len(corpus),
         "bytes":sum(p.stat().st_size for p in corpus),"ordered_name_and_content_sha256":digest.hexdigest()},
 "training":{"epochs_requested":epochs,"resume_epoch":epochs+1,"seed":seed,"optimizer":"AdamW",
             "single_document_evaluation":False,"training_data_evaluation":True,"independent_qa_documents":True,"experts_trained_sequentially":True,
             "expert_a_validation_bpb":metric("expert_a_validation_bpb"),
             "expert_b_validation_bpb":metric("expert_b_validation_bpb"),
             "expert_a_test_bpb":metric("expert_a_test_bpb"),
             "expert_b_test_bpb":metric("expert_b_test_bpb"),
             "resume_verified":True},
 "code":{"commit":os.environ.get("GITHUB_SHA","local"),"compiler":subprocess.check_output(["c++","--version"],text=True).splitlines()[0]},
 "environment":{"os":platform.platform(),"architecture":platform.machine(),"cpu":cpu,"logical_cpu_count":os.cpu_count()},
 "artifacts":{"checkpoint_a":"g2-model.expert-a","checkpoint_b":"g2-model.expert-b","generated_output":"generated.txt","resource_usage":"resource-usage.txt"},
 "limitations":["The three-document corpus is a smoke fixture, not a representative natural-language corpus.","Training-data validation and test metrics reuse the three training documents and do not measure generalization.","Metrics do not establish conversational quality or generalization.","CI runner memory does not validate operation on physical 4 GB hardware."]
}
(out/"manifest.json").write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+"\n")
PY
echo "G2 training, resume, checkpoint loading and inference completed."
