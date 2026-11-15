#!/usr/bin/env python3
"""Validate the curated factual corpus and its provenance manifest."""
from __future__ import annotations

import hashlib
import json
import sys
from pathlib import Path


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    corpus = root / "shared" / "tests" / "fixtures" / "continual"
    manifest_path = corpus / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("version") != 2:
        raise ValueError("corpus manifest version must be 2")
    if manifest.get("license") != "CC0-1.0":
        raise ValueError("corpus license must be CC0-1.0")
    if manifest.get("language") != "pt-BR":
        raise ValueError("corpus language must be pt-BR")

    declared = manifest.get("documents")
    if not isinstance(declared, list) or not declared:
        raise ValueError("manifest documents must be a non-empty list")
    if len(declared) != len(set(declared)):
        raise ValueError("manifest contains duplicate document names")
    if any(Path(name).name != name or not name.startswith("doc-") or not name.endswith(".txt") for name in declared):
        raise ValueError("manifest contains an invalid document path")

    actual = sorted(path.name for path in corpus.glob("doc-*.txt"))
    if sorted(declared) != actual:
        missing = sorted(set(actual) - set(declared))
        absent = sorted(set(declared) - set(actual))
        raise ValueError(f"manifest/document mismatch; unlisted={missing}, missing={absent}")

    digest = hashlib.sha256()
    total_bytes = 0
    seen: set[bytes] = set()
    for name in sorted(declared):
        path = corpus / name
        raw = path.read_bytes()
        text = raw.decode("utf-8", errors="strict")
        lines = text.strip().splitlines()
        if len(lines) != 2 or not lines[0].startswith("Pergunta: ") or not lines[1].startswith("Resposta: "):
            raise ValueError(f"{name} must contain exactly one Pergunta and one Resposta line")
        if not lines[0][len("Pergunta: "):].strip() or not lines[1][len("Resposta: "):].strip():
            raise ValueError(f"{name} contains an empty question or answer")
        if raw in seen:
            raise ValueError(f"duplicate training document: {name}")
        seen.add(raw)
        digest.update(name.encode("utf-8"))
        digest.update(raw)
        total_bytes += len(raw)

    result = {
        "valid": True,
        "manifest_version": manifest["version"],
        "license": manifest["license"],
        "language": manifest["language"],
        "documents": len(declared),
        "bytes": total_bytes,
        "sha256": digest.hexdigest(),
        "topics": manifest.get("topics", []),
    }
    print(json.dumps(result, ensure_ascii=False, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, UnicodeError, ValueError, json.JSONDecodeError) as error:
        print(f"validate-g1-corpus: {error}", file=sys.stderr)
        raise SystemExit(1)
