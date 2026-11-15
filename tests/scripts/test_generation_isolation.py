#!/usr/bin/env python3
"""Enforce generation, profile and public-core source isolation."""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
PROFILES = ("Mokosh", "Tsukuyomi", "Artemis", "Apollo")

G1_CODE = [
    ROOT / "Gerações" / "G1" / "shared",
    ROOT / "Gerações" / "G1" / "apps",
    ROOT / "Gerações" / "G1" / "tests",
    *(ROOT / "Gerações" / "G1" / profile for profile in PROFILES),
]
G2_CODE = [
    ROOT / "Gerações" / "G2" / "engine",
    ROOT / "Gerações" / "G2" / "model",
    ROOT / "Gerações" / "G2" / "apps",
    ROOT / "Gerações" / "G2" / "tests",
    *(ROOT / "Gerações" / "G2" / profile for profile in PROFILES),
]

FORBIDDEN_G1 = re.compile(
    r"morok/g2/|morok::g2::|\b(?:G2Parameters|G2ExpertParameters|G2ExpertState|"
    r"G2TrainingConfig|G2GenerationReport|train_g2_corpus|generate_g2|"
    r"advance_g2_state|advance_g2_expert_state)\b"
)
FORBIDDEN_G2 = re.compile(
    r"morok/model/(?:g1_forward|parameters/g1_parameters|learn/g1_)|morok::model::|morok/g1/|morok::g1::|"
    r"\b(?:G1Parameters|G1State|G1Checkpoint|G1TrainingConfig|"
    r"G1GenerationConfig|train_g1_corpus|generate_g1|advance_g1_state|"
    r"compute_g1_bptt_gradients|apply_g1_optimizer_step)\b"
)
FORBIDDEN_PUBLIC = re.compile(
    r"morok/g[123]/|morok::g[123]::|\b(?:G1Parameters|G2Parameters|G3Parameters)\b"
)


def source_files(path: Path):
    if not path.exists():
        raise AssertionError(f"required source directory is missing: {path}")
    if path.is_file():
        return [path] if path.suffix in {".hpp", ".cpp"} else []
    return sorted(p for p in path.rglob("*") if p.suffix in {".hpp", ".cpp"})


def inspect_generation(name: str, roots, forbidden, violations):
    for root in roots:
        for file in source_files(root):
            content = file.read_text(encoding="utf-8")
            if forbidden.search(content):
                violations.append(f"{name}: forbidden cross-generation reference in {file.relative_to(ROOT)}")


def inspect_profiles(generation: str, violations):
    for profile in PROFILES:
        root = ROOT / generation / profile
        files = source_files(root)
        if not any(p.suffix == ".hpp" for p in files):
            violations.append(f"{generation}/{profile}: profile header missing")
        if not any(p.name == "profile.cpp" for p in files):
            violations.append(f"{generation}/{profile}: profile implementation missing")
        if not any(p.name == "test_profile.cpp" for p in files):
            violations.append(f"{generation}/{profile}: profile test missing")
        for file in files:
            content = file.read_text(encoding="utf-8")
            for other in PROFILES:
                if other == profile:
                    continue
                if f"morok::{generation.lower()}::{other.lower()}::" in content:
                    violations.append(f"{generation}/{profile}: imports private profile {other}")


def main() -> int:
    violations = []
    inspect_generation("G1", G1_CODE, FORBIDDEN_G1, violations)
    inspect_generation("G2", G2_CODE, FORBIDDEN_G2, violations)
    inspect_profiles("G1", violations)
    inspect_profiles("G2", violations)

    public_include = ROOT / "include"
    for file in source_files(public_include):
        content = file.read_text(encoding="utf-8")
        if FORBIDDEN_PUBLIC.search(content):
            violations.append(f"public include contains generation-specific API: {file.relative_to(ROOT)}")

    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    core = cmake.split("add_library(morok_core STATIC", 1)[1].split("add_library(Morok::Core", 1)[0]
    if any(token in core for token in ("G1/", "G2/", "G3/")):
        violations.append("CMakeLists.txt: generation-specific source compiled into Morok::Core")

    g1 = cmake.split("add_library(morok_g1 STATIC", 1)[1].split("add_library(Morok::G1", 1)[0]
    if "Gerações/G1/shared/src/" not in g1 or "G2/" in g1:
        violations.append("CMakeLists.txt: G1 target source ownership is invalid")

    g2 = cmake.split("add_library(morok_g2 STATIC", 1)[1].split("add_library(Morok::G2", 1)[0]
    if "Gerações/G2/engine/src/" not in g2 or "Gerações/G2/model/src/" not in g2 or "G1/" in g2:
        violations.append("CMakeLists.txt: G2 target source ownership is invalid")
    if "Morok::G1" in g2:
        violations.append("CMakeLists.txt: Morok::G2 links Morok::G1")

    for generation in ("G1", "G2"):
        for profile in PROFILES:
            if f"Morok::{generation}::{profile}" not in cmake:
                violations.append(f"CMakeLists.txt: missing isolated target {generation}/{profile}")

    if violations:
        print("Model isolation violations:")
        for item in violations:
            print(f"- {item}")
        return 1
    print("Generation and profile isolation checks passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
