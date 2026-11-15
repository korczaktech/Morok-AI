#!/usr/bin/env python3
"""Validate G1 layout, profile isolation and shared-core ownership."""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
PROFILES = ("Mokosh", "Tsukuyomi", "Apollo")
SUFFIXES = {".hpp", ".cpp"}


def sources(root: Path):
    if not root.exists():
        return []
    return sorted(p for p in root.rglob("*") if p.is_file() and p.suffix in SUFFIXES)


def main() -> int:
    errors = []
    g1 = ROOT / "Gerações" / "G1"
    for path in (g1 / "shared/include", g1 / "shared/src", g1 / "shared/tests", g1 / "apps"):
        if not path.is_dir():
            errors.append(f"missing G1 directory: {path.relative_to(ROOT)}")

    for profile in PROFILES:
        root = g1 / profile
        files = sources(root)
        if not any(p.name == "profile.hpp" for p in files):
            errors.append(f"G1/{profile}: missing profile.hpp")
        if not any(p.name == "profile.cpp" for p in files):
            errors.append(f"G1/{profile}: missing profile.cpp")
        if not any(p.name == "test_profile.cpp" for p in files):
            errors.append(f"G1/{profile}: missing test_profile.cpp")
        for file in files:
            content = file.read_text(encoding="utf-8")
            for other in PROFILES:
                if other != profile and f"morok::g1::{other.lower()}::" in content:
                    errors.append(f"G1/{profile} imports private G1/{other}: {file.relative_to(ROOT)}")

    private_profile = re.compile(r"morok::g1::(?:mokosh|tsukuyomi|apollo)::")
    for file in sources(ROOT / "include"):
        if private_profile.search(file.read_text(encoding="utf-8")):
            errors.append(f"shared public API imports private G1 profile: {file.relative_to(ROOT)}")

    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    core = cmake.split("add_library(morok_core STATIC", 1)[1].split("add_library(Morok::Core", 1)[0]
    if "Gerações/G1/" in core or "Gerações/G3/" in core:
        errors.append("Morok::Core contains generation-private sources")
    g1_block = cmake.split("add_library(morok_g1 STATIC", 1)[1].split("add_library(Morok::G1", 1)[0]
    if "Gerações/G1/shared/src/" not in g1_block:
        errors.append("Morok::G1 does not compile the G1 shared engine")
    if "Gerações/G2/" in cmake or "MOROK_BUILD_G2" in cmake or "Artemis" in cmake:
        errors.append("CMake contains removed G2 or G1 Artemis configuration")
    profile_block = cmake.split("foreach(_profile IN ITEMS", 1)[1].split(")", 1)[0]
    if not all(profile in profile_block.split() for profile in PROFILES):
        errors.append("CMake does not declare all three G1 profiles in its target loop")
    if "Morok::G1::${_profile}" not in cmake:
        errors.append("CMake does not expose namespaced G1 profile targets")

    if errors:
        print("G1 architecture validation failed:", file=sys.stderr)
        for error in errors:
            print(f"- {error}", file=sys.stderr)
        return 1
    print("G1 directory layout and profile isolation checks passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
