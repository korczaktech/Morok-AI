#!/usr/bin/env python3
"""Treina 100 épocas e testa 100 respostas por modelo em cada ciclo."""
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
    if model in completed and not isinstance(completed[model], bool):
        raise RuntimeError(f"Estado de conclusão inválido para {model}.")
if results_path.exists() and results_path.stat().st_size:
    with results_path.open(encoding="utf-8", newline="") as stream:
        reader = csv.DictReader(stream, delimiter="\t")
        header = reader.fieldnames
        if header == ["model", "epoch", "question_id", "verdict", "answer"]:
            # Migra o histórico legado para o formato com número de tentativa e latência.
            legacy_rows = list(reader)
            migrated_path = results_path.with_suffix(".tsv.migrating")
            with migrated_path.open("w", encoding="utf-8", newline="") as migrated:
                writer = csv.writer(migrated, delimiter="\t", lineterminator="\n")
                writer.writerow(["model", "epoch", "question_id", "attempt",
                                 "verdict", "latency_ms", "answer"])
                for row in legacy_rows:
                    writer.writerow([row["model"], row["epoch"], row["question_id"],
                                     "1", row["verdict"], "", row["answer"]])
            migrated_path.replace(results_path)
        elif header != ["model", "epoch", "question_id", "attempt",
                        "verdict", "latency_ms", "answer"]:
            raise RuntimeError("Cabeçalho de history.tsv incompatível; não será anexado histórico corrompido.")

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
        epoch = int(epochs.get(model, 0)) + 100
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
        print(f"[{model}] Treinando até a época {epoch} (100 novas épocas).", flush=True)
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
                model, question, "100",
            ], check=True, timeout=remaining)
        except subprocess.TimeoutExpired:
            print(f"[{model}] Limite de tempo atingido durante a avaliação.", flush=True)
            timed_out = True
            break

        with temporary_results.open(encoding="utf-8", newline="") as stream:
            rows = list(csv.DictReader(stream, delimiter="\t"))
        if len(rows) != 100:
            raise RuntimeError(f"{model}: esperado exatamente 100 resultados; recebidos {len(rows)}.")
        required = {"profile", "question_id", "attempt", "verdict", "latency_ms", "answer"}
        correct_attempts = 0
        for expected_attempt, row in enumerate(rows, start=1):
            if not required.issubset(row):
                raise RuntimeError(f"{model}: resultado TSV sem as colunas obrigatórias.")
            if (row["profile"] != model or row["question_id"] != question or
                    row["attempt"] != str(expected_attempt)):
                raise RuntimeError(f"{model}: resultado não corresponde à tentativa {expected_attempt}/100.")
            if row["verdict"] not in {"correct", "incorrect"}:
                raise RuntimeError(f"{model}: veredito inválido: {row['verdict']!r}.")
            if not row["latency_ms"]:
                raise RuntimeError(f"{model}: latência ausente na tentativa {expected_attempt}.")
            if row["verdict"] == "correct":
                correct_attempts += 1

        is_correct = correct_attempts > 0

        # Persiste os contadores e o estado de conclusão antes do histórico legível.
        epochs[model] = epoch
        completed[model] = is_correct
        save_json_atomic(epochs_path, epochs)
        save_json_atomic(completed_path, completed)

        header_needed = not results_path.exists() or results_path.stat().st_size == 0
        with results_path.open("a", encoding="utf-8", newline="") as stream:
            writer = csv.writer(stream, delimiter="\t", lineterminator="\n")
            if header_needed:
                writer.writerow(["model", "epoch", "question_id", "attempt", "verdict", "latency_ms", "answer"])
            for row in rows:
                writer.writerow([model, epoch, question, row["attempt"],
                                 row["verdict"], row["latency_ms"], row["answer"]])
            stream.flush()
            os.fsync(stream.fileno())
        print(f"[{model}] épocas={epoch}; acertos={correct_attempts}/100; "
              f"estado={'APROVADO' if is_correct else 'não aprovado'}; "
              f"última saída={rows[-1]['answer']!r}", flush=True)

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
