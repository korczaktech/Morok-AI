#!/usr/bin/env python3
"""Treina um epoch por ciclo e testa uma resposta por modelo até acertar."""
import csv
import json
import os
import subprocess
import sys
import time
from pathlib import Path

root = Path(__file__).resolve().parents[3]
build = root / "build" / "g1"
train = Path(os.environ.get("MOROK_TRAIN_BIN", build / "morok-train"))
evaluate = build / "morok-g1-profile-evaluate"
corpus = root / "Gerações/G1/shared/tests/fixtures/continual"
state = root / "test-state"
state.mkdir(parents=True, exist_ok=True)

models = [
    ("Tsukuyomi", "TSUKUYOMI"),
    ("Apollo", "APOLLO"),
    ("Mokosh", "MOKOSH"),
    ("TsukuyomiMax", "TSUKUYOMI_MAX"),
]
selected = [name for name, key in models
            if os.environ.get("G1_MODEL_" + key, "false").lower() == "true"]
question = os.environ["G1_TEST_QUESTION"]
if not selected:
    raise SystemExit("Selecione pelo menos um modelo.")
valid_questions = {
    "sky_is_blue", "grass_color", "mountains_high_or_low",
    "greeting", "english_sky", "spanish_sky", "multi_turn",
}
if question not in valid_questions:
    raise SystemExit("Pergunta de teste inválida.")

completed_path = state / "completed.json"
completed = json.loads(completed_path.read_text()) if completed_path.exists() else {}
epochs_path = state / "epochs.json"
epochs = json.loads(epochs_path.read_text()) if epochs_path.exists() else {}
results_path = state / "history.tsv"
deadline = time.monotonic() + 340 * 60
seed_base = 42

print("Modelos selecionados: " + ", ".join(selected), flush=True)
print("Pergunta selecionada: " + question, flush=True)

while not all(completed.get(model, False) for model in selected):
    if time.monotonic() >= deadline:
        break
    for model_index, model in enumerate(selected):
        if completed.get(model, False):
            continue
        if time.monotonic() >= deadline:
            break

        model_dir = state / model.lower()
        model_dir.mkdir(parents=True, exist_ok=True)
        checkpoint = model_dir / "resume.morokckp"
        candidate = model_dir / "candidate.morokckp"
        metrics = model_dir / "metrics.jsonl"
        epoch = int(epochs.get(model, 0)) + 1
        args = [
            str(train), str(corpus), str(epoch), str(seed_base + model_index),
            "--checkpoint", str(checkpoint),
            "--log", str(metrics),
            "--export-best", str(candidate),
            "--patience", "10000",
            "--truncation", "256",
            "--optimizer", "adamw",
            "--learning-rate", "0.001",
        ]
        if checkpoint.is_file():
            args.extend(["--resume", str(checkpoint)])

        print(f"[{model}] Treinando época {epoch}.", flush=True)
        subprocess.run(args, check=True)
        if not candidate.is_file():
            raise RuntimeError(f"{model}: checkpoint candidato não foi produzido.")

        temporary_results = model_dir / "attempt.tsv"
        temporary_results.unlink(missing_ok=True)
        subprocess.run([
            str(evaluate), str(candidate), str(temporary_results),
            model, question, "1",
        ], check=True)
        with temporary_results.open(encoding="utf-8", newline="") as stream:
            rows = list(csv.DictReader(stream, delimiter="\\t"))
        if len(rows) != 1:
            raise RuntimeError(f"{model}: esperado exatamente um resultado; recebidos {len(rows)}.")
        row = rows[0]
        is_correct = row["verdict"] == "correct"
        epochs[model] = epoch
        completed[model] = is_correct
        epochs_path.write_text(json.dumps(epochs, ensure_ascii=False, indent=2) + "\\n")
        completed_path.write_text(json.dumps(completed, ensure_ascii=False, indent=2) + "\\n")

        header_needed = not results_path.exists()
        with results_path.open("a", encoding="utf-8", newline="") as stream:
            writer = csv.writer(stream, delimiter="\\t")
            if header_needed:
                writer.writerow(["model", "epoch", "question_id", "verdict", "answer"])
            writer.writerow([model, epoch, question, row["verdict"], row["answer"]])
        print(f"[{model}] época={epoch}; resposta={'CORRETA' if is_correct else 'incorreta'}; saída={row['answer']!r}", flush=True)

        if all(completed.get(name, False) for name in selected):
            break

success = all(completed.get(model, False) for model in selected)
with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as output:
    output.write("continue=" + ("false" if success else "true") + "\\n")
    output.write("success=" + ("true" if success else "false") + "\\n")

if success:
    print("Todos os modelos selecionados produziram uma resposta considerada correta.", flush=True)
else:
    print("Janela desta execução encerrada; os checkpoints serão preservados e outro ciclo será iniciado.", flush=True)
