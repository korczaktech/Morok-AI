#!/usr/bin/env bash
set -euo pipefail

: "${GITHUB_WORKSPACE:?GITHUB_WORKSPACE não definido}"
: "${RUNNER_TEMP:?RUNNER_TEMP não definido}"
: "${GH_TOKEN:?GH_TOKEN não definido}"

ROOT="$GITHUB_WORKSPACE"
STATE_DIR="$RUNNER_TEMP/tsukuyomi-state-cycle"
CHECKPOINT="$ROOT/Morok/build/g2/tsukuyomi-treinamento-ciclico.ckpt"
ADAM="$ROOT/Morok/build/g2/tsukuyomi-treinamento-ciclico.ckpt.adam"
FEEDBACK="$ROOT/Morok/build/g2/tsukuyomi-memoria-erros.txt"
HISTORY="$ROOT/Morok/build/g2/tsukuyomi-historico-perguntas.json"
AUTH_HEADER="AUTHORIZATION: basic $(printf 'x-access-token:%s' "$GH_TOKEN" | base64 -w0)"

for file in "$CHECKPOINT" "$ADAM" "$HISTORY"; do
  if [ ! -s "$file" ]; then
    echo "Estado incompleto; não será persistido: $file" >&2
    exit 1
  fi
done

git -C "$ROOT" -c http.extraheader="$AUTH_HEADER" fetch origin \
  refs/heads/tsukuyomi-state:refs/remotes/origin/tsukuyomi-state 2>/dev/null || true

if git -C "$ROOT" show-ref --verify --quiet refs/remotes/origin/tsukuyomi-state; then
  git -C "$ROOT" worktree add -B tsukuyomi-state "$STATE_DIR" origin/tsukuyomi-state
else
  git -C "$ROOT" worktree add -b tsukuyomi-state "$STATE_DIR" HEAD
  git -C "$STATE_DIR" rm -rf . >/dev/null 2>&1 || true
fi

cleanup() {
  git -C "$ROOT" worktree remove --force "$STATE_DIR" >/dev/null 2>&1 || true
}
trap cleanup EXIT

mkdir -p "$STATE_DIR/state"
install -m 0644 "$CHECKPOINT" "$STATE_DIR/state/tsukuyomi-g2.ckpt"
install -m 0644 "$ADAM" "$STATE_DIR/state/tsukuyomi-g2.ckpt.adam"
if [ -f "$FEEDBACK" ]; then
  install -m 0644 "$FEEDBACK" "$STATE_DIR/state/tsukuyomi-g2-feedback.txt"
else
  : > "$STATE_DIR/state/tsukuyomi-g2-feedback.txt"
fi
install -m 0644 "$HISTORY" "$STATE_DIR/state/tsukuyomi-g2-question-history.json"
rm -f "$STATE_DIR/state/tsukuyomi-g2-learning-rate.txt"
cat > "$STATE_DIR/state/README.txt" <<'EOF'
Tsukuyomi G2 persistent training state.
Keep the .ckpt and .adam files together; both are required for exact optimizer continuation.
This branch is machine state, not source code. Do not merge it into main.
EOF

git -C "$STATE_DIR" add state/
if git -C "$STATE_DIR" diff --cached --quiet; then
  echo "Estado do ciclo já estava persistido; nenhuma alteração necessária."
else
  git -C "$STATE_DIR" -c user.name="github-actions[bot]" \
    -c user.email="41898282+github-actions[bot]@users.noreply.github.com" \
    commit -m "Persistir estado Tsukuyomi G2 após ciclo"
  git -C "$STATE_DIR" -c http.extraheader="$AUTH_HEADER" push origin tsukuyomi-state
  echo "Checkpoint, Adam, memória e histórico persistidos na branch tsukuyomi-state."
fi
