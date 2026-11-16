#!/usr/bin/env python3
"""Validate G1/G2 layout, profile isolation and shared-core ownership."""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
PROFILES = ("TsukuyomiMax",)
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
        for file in files:
            content = file.read_text(encoding="utf-8")
            for other in PROFILES:
                if other != profile and f"morok::g1::{other.lower()}::" in content:
                    errors.append(f"G1/{profile} imports private G1/{other}: {file.relative_to(ROOT)}")

    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    core = cmake.split("add_library(morok_core STATIC", 1)[1].split("add_library(Morok::Core", 1)[0]
    if "Gerações/G1/" in core or "Gerações/G3/" in core:
        errors.append("Morok::Core contains generation-private sources")
    g1_block = cmake.split("add_library(morok_g1 STATIC", 1)[1].split("add_library(Morok::G1", 1)[0]
    if "Gerações/G1/shared/src/" not in g1_block:
        errors.append("Morok::G1 does not compile the G1 shared engine")
    if "MOROK_BUILD_G2" in cmake or "Artemis" in cmake:
        errors.append("CMake contains removed G2 build option or G1 Artemis configuration")
    if "TsukuyomiMax" not in cmake or "morok_g1_tsukuyomimax" not in cmake:
        errors.append("CMake does not declare the Tsukuyomi Max G1 target")
    g2_tsukuyomi = ROOT / "Gerações" / "G2" / "Tsukuyomi"
    g2_sources = sources(g2_tsukuyomi)
    required_g2 = ("model.hpp", "model.cpp", "train.cpp", "generate.cpp", "test_model.cpp")
    for filename in required_g2:
        if not any(path.name == filename for path in g2_sources):
            errors.append(f"G2/Tsukuyomi: missing {filename}")
    for file in g2_sources:
        content = file.read_text(encoding="utf-8")
        if "Gerações/G1/" in content or "morok::model::G1" in content:
            errors.append(f"G2/Tsukuyomi imports private G1 implementation: {file.relative_to(ROOT)}")
    g2_block = cmake.split("add_library(morok_g2_tsukuyomi STATIC", 1)
    if len(g2_block) != 2 or "Gerações/G2/Tsukuyomi/src/" not in g2_block[1].split("add_library(Morok::G2::Tsukuyomi", 1)[0]:
        errors.append("Morok::G2::Tsukuyomi does not compile its private model source")
    if "Gerações/G2/" in core:
        errors.append("Morok::Core contains G2-private sources")

    # G3 byte-level representation is shared Core infrastructure, not a new engine.
    tokenizer_header = ROOT / "include" / "morok" / "lang" / "byte_tokenizer.hpp"
    tokenizer_source = ROOT / "src" / "lang" / "byte_tokenizer.cpp"
    tokenizer_test = ROOT / "tests" / "unit" / "lang" / "test_byte_tokenizer.cpp"
    if not tokenizer_header.is_file() or not tokenizer_source.is_file():
        errors.append("G3 shared byte tokenizer implementation is incomplete")
    if not tokenizer_test.is_file():
        errors.append("G3 byte tokenizer regression tests are missing")
    if tokenizer_header.is_file():
        tokenizer_text = tokenizer_header.read_text(encoding="utf-8")
        for token_id in ("kPadToken = 256", "kBosToken = 257",
                         "kEosToken = 258", "kUsrToken = 259",
                         "kBotToken = 260"):
            if token_id not in tokenizer_text:
                errors.append(f"G3 vocabulary contract is missing {token_id}")
    if "src/lang/byte_tokenizer.cpp" not in core:
        errors.append("shared byte tokenizer is not compiled into Morok::Core")
    if "find_package(ICU REQUIRED COMPONENTS uc)" not in cmake or "ICU::uc" not in cmake:
        errors.append("Morok::Core does not link ICU for complete NFC normalization")

    # The public backend contract belongs to the existing Core include surface.
    backend_contract = ROOT / "include" / "morok" / "model" / "model_backend.hpp"
    if not backend_contract.is_file():
        errors.append("shared model backend contract is missing from Morok::Core")

    model_session = ROOT / "include" / "morok" / "model" / "model_session.hpp"
    if not model_session.is_file():
        errors.append("shared model session facade is missing from Morok::Core")

    g1_backend_header = g1 / "shared" / "include" / "morok" / "model" / "g1_backend.hpp"
    g1_backend_source = g1 / "shared" / "src" / "model" / "g1_backend.cpp"
    if not g1_backend_header.is_file() or not g1_backend_source.is_file():
        errors.append("G1 adapter for the shared model backend contract is incomplete")
    if "g1_backend.cpp" not in g1_block:
        errors.append("Morok::G1 does not compile its shared backend adapter")

    g3 = ROOT / "Gerações" / "G3"
    g3_readme = g3 / "README.md"
    g3_architecture = g3 / "ARQUITETURA.md"
    if not g3_architecture.is_file():
        errors.append("G3 shared motor integration architecture document is missing")
    if (g3 / "shared").exists():
        errors.append("G3 must not create a duplicate shared motor directory")
    if g3_readme.is_file():
        g3_text = g3_readme.read_text(encoding="utf-8").lower()
        if "morok::core" not in g3_text or "modelbackend" not in g3_text:
            errors.append("G3 README must point to the existing Core and ModelBackend contract")
        if "próprio motor" in g3_text or "own engine" in g3_text:
            errors.append("G3 README still prescribes a separate engine")

    model_directories = sorted(p.name for p in g1.iterdir()
                                if p.is_dir() and (p / "src" / "profile.cpp").is_file())
    if model_directories != ["TsukuyomiMax"]:
        errors.append("G1 must contain only the Tsukuyomi Max model directory")

    if errors:
        print("G1 architecture validation failed:", file=sys.stderr)
        for error in errors:
            print(f"- {error}", file=sys.stderr)
        return 1
    print("G1/G2 directory layout and profile isolation checks passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
