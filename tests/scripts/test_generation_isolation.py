#!/usr/bin/env python3
"""Fail when a generation imports another generation's private implementation."""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
G2_CODE = [
    ROOT / "G2" / "engine",
    ROOT / "G2" / "legacy",
    ROOT / "G2" / "apps",
    ROOT / "G2" / "tests",
]
FORBIDDEN = re.compile(
    r"morok/model/(?:g1_forward|parameters/g1_parameters|learn/g1_)|morok/g1/"
    r"|\b(?:G1Parameters|G1State|G1Checkpoint|G1TrainingConfig|"
    r"G1GenerationConfig|train_g1_corpus|generate_g1|advance_g1_state|"
    r"compute_g1_bptt_gradients|apply_g1_optimizer_step)\b"
)

def source_files(path: Path):
    if path.is_file():
        return [path]
    if path.is_dir():
        return sorted(p for p in path.rglob("*") if p.suffix in {".hpp", ".cpp"})
    raise AssertionError(f"required G2 source path is missing: {path}")

def main() -> int:
    violations = []
    for root in G2_CODE:
        for file in source_files(root):
            content = file.read_text(encoding="utf-8")
            if FORBIDDEN.search(content):
                violations.append(str(file.relative_to(ROOT)))
    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    core = cmake.split("add_library(morok_core STATIC", 1)[1].split("add_library(Morok::Core", 1)[0]
    if re.search(r"src/model/[^\n]*g[12]_", core):
        violations.append("CMakeLists.txt: model-specific source compiled into Morok::Core")
    g2 = cmake.split("add_library(morok_g2 STATIC", 1)[1].split("add_library(Morok::G2", 1)[0]
    if "G2/engine/src/" not in g2:
        violations.append("CMakeLists.txt: G2 engine sources are not part of Morok::G2")
    if "Morok::G1" in g2:
        violations.append("CMakeLists.txt: Morok::G2 links Morok::G1")
    if any(token in core for token in ("G1/shared/src/", "G2/engine/src/", "G2/legacy/src/")):
        violations.append("CMakeLists.txt: generation-specific source compiled into Morok::Core")
    if "G1/shared/src/" not in cmake.split("add_library(morok_g1 STATIC", 1)[1].split("add_library(Morok::G1", 1)[0]:
        violations.append("CMakeLists.txt: G1 shared engine sources are not part of Morok::G1")
    if violations:
        print("Generation isolation violations:")
        for item in violations:
            print(f"- {item}")
        return 1
    print("Generation isolation checks passed.")
    return 0

if __name__ == "__main__":
    sys.exit(main())
