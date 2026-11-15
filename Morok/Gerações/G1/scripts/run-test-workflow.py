#!/usr/bin/env python3
"""Treina uma época e testa uma resposta por modelo em cada ciclo."""
import csv
import json
import os
import subprocess
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
valid_questions = {
    "sky_is_blue", "grass_color", "mountains_high_or_low",
    "greeting", "english_sky", "spanish_sky", "multi_turn",
}
if not selected:
    raise SystemExit("Selecione pelo menos um modelo.")
if question not in valid_questions:
    raise SystemExit("Pergunta de teste inválida.")

completed_path = state / "completed.json"
epochs_path = state / "epochs.json"
results_path = state / "history.tsv"
completed = json.loads(completed_path.read_text(encoding="utf-8")) if completed_path.exists() else {}
epochs = json.loads(epochs_path.read_text(encoding="utf-8")) if epochs_path.exists() else {}
if not isinstance(completed, dict) or not isinstance(epochs, dict):
    raise RuntimeError("Estado de treinamento inválido: completed.json/epochs.json devem conter objetos JSON.")
for model in selected:
    value = epochs.get(model, 0)
    if isinstance(value, bool) or not isinstance(value, int) or value < 0:
        raise RuntimeError(f"Contador de épocas inválido para {model}.")

def save_json_atomic(path, value):
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n",
                         encoding="utf-8")
    temporary.replace(path)

deadline = time.monotonic() + 300 * 60
seed_base = 42
timed_out = False

print("Modelos selecionados: " + ", ".join(selected), flush=True)
print("Pergunta selecionada: " + question, flush=True)

while not all(completed.get(model, False) for model in selected):
    if time.monotonic() >= deadline:
        break
    for model_index, model in enumerate(selected):
        if completed.get(model, False):
            continue
        key = next(key for name, key in models if name == model)
        remaining = int(deadline - time.monotonic())
        if remaining <= 0:
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

        # Nunca aceite um artefato antigo como se fosse a saída deste treinamento.
        candidate.unlink(missing_ok=True)
        print(f"[{model}] Treinando época {epoch}.", flush=True)
        try:
            subprocess.run(args, check=True, timeout=remaining)
        except subprocess.TimeoutExpired:
            print(f"[{model}] Limite de tempo atingido durante o treinamento; o último checkpoint íntegro será preservado.", flush=True)
            timed_out = True
            break
        if not candidate.is_file() or candidate.stat().st_size == 0:
            raise RuntimeError(f"{model}: checkpoint candidato não foi produzido ou está vazio.")

        temporary_results = model_dir / "attempt.tsv"
        temporary_results.unlink(missing_ok=True)
        remaining = int(deadline - time.monotonic())
        if remaining <= 0:
            timed_out = True
            break
        try:
            subprocess.run([
                str(evaluate), str(candidate), str(temporary_results),
                model, question, "1",
            ], check=True, timeout=remaining)
        except subprocess.TimeoutExpired:
            print(f"[{model}] Limite de tempo atingido durante a avaliação.", flush=True)
            timed_out = True
            break

        with temporary_results.open(encoding="utf-8", newline="") as stream:
            rows = list(csv.DictReader(stream, delimiter="\t"))
        if len(rows) != 1:
            raise RuntimeError(f"{model}: esperado exatamente um resultado; recebidos {len(rows)}.")
        row = rows[0]
        required = {"profile", "question_id", "attempt", "verdict", "answer"}
        if not required.issubset(row):
            raise RuntimeError(f"{model}: resultado TSV sem as colunas obrigatórias.")
        if row["profile"] != model or row["question_id"] != question or row["attempt"] != "1":
            raise RuntimeError(f"{model}: o avaliador retornou um resultado que não corresponde à tentativa solicitada.")
        if row["verdict"] not in {"correct", "incorrect"}:
            raise RuntimeError(f"{model}: veredito inválido: {row['verdict']!r}.")
        is_correct = row["verdict"] == "correct"

        # Persiste os contadores e o estado de conclusão antes do histórico legível.
        epochs[model] = epoch
        completed[model] = is_correct
        save_json_atomic(epochs_path, epochs)
        save_json_atomic(completed_path, completed)

        header_needed = not results_path.exists() or results_path.stat().st_size == 0
        with results_path.open("a", encoding="utf-8", newline="") as stream:
            writer = csv.writer(stream, delimiter="\t", lineterminator="\n")
            if header_needed:
                writer.writerow(["model", "epoch", "question_id", "verdict", "answer"])
            writer.writerow([model, epoch, question, row["verdict"], row["answer"]])
            stream.flush()
            os.fsync(stream.fileno())
        print(f"[{model}] época={epoch}; resposta={'CORRETA' if is_correct else 'incorreta'}; saída={row['answer']!r}", flush=True)

        if all(completed.get(name, False) for name in selected):
            break
    if timed_out:
        break

success = all(completed.get(model, False) for model in selected)
with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as output:
    output.write("continue=" + ("false" if success else "true") + "\n")
    output.write("success=" + ("true" if success else "false") + "\n")

if success:
    print("Todos os modelos selecionados produziram uma resposta considerada correta.", flush=True)
else:
    print("Janela desta execução encerrada; os checkpoints serão preservados e outro ciclo será iniciado.", flush=True)
