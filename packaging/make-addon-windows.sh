#!/bin/bash
# Stages the add-on from a Windows build and writes the package (2026-11-16; packaging/make-addon.sh is the Linux one).
#   packaging/make-addon-windows.sh <build dir> <out dir>   -> <out dir>/translation-proofreading-<version>-windows.qbpkg
# A release build (TP_TEST_SWITCHES off) against The Qt Company's Qt 6.12.0 for MSVC. As Quillbinder's own Windows
# add-ons: "platform": "windows-x64" in the manifest, DLLs without "lib", no TLS plugin (Quillbinder for Windows brings
# Qt's Schannel plugin, which the model download uses). minApp 0.91.1: the first public Quillbinder for Windows.
# Runs in Git Bash.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD=$(cd "$1" && pwd)
OUT=$2
DEPS=${TP_DEPS:-$ROOT/deps}
PY=$(command -v python || command -v python3)   # (python3 can be the Store stub on Windows)
VERSION=$("$PY" -c "import json,sys; print(json.load(open(sys.argv[1]))['version'])" "$(cygpath -w "$ROOT/packaging/manifest.json")")
CMAKE_VERSION=$(sed -n 's/^set(TP_VERSION *"\(.*\)")/\1/p' "$ROOT/CMakeLists.txt" | tr -d '\r')
[ "$VERSION" = "$CMAKE_VERSION" ] || { echo "manifest.json has $VERSION, CMakeLists.txt $CMAKE_VERSION" >&2; exit 1; }
if grep -q "TP_TEST_SWITCHES:BOOL=ON" "$BUILD/CMakeCache.txt"; then
    echo "$BUILD is a test build (TP_TEST_SWITCHES=ON); packages come from a release build" >&2
    exit 1
fi
E=$BUILD/addon/translation-proofreading
for f in "$BUILD/addon/plugins/qb-translation-proofreading.dll" "$E/qb-tp-engine.dll" "$E/vcomp140.dll"; do
    [ -f "$f" ] || { echo "missing in $BUILD: $f" >&2; exit 1; }
done

STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
P=$STAGE/payload
A=$P/translation-proofreading
mkdir -p "$P/plugins" "$A/icons" "$A/licenses"
"$PY" - "$(cygpath -w "$ROOT/packaging/manifest.json")" "$(cygpath -w "$STAGE/manifest.json")" <<'PY'
import json, sys
m = json.load(open(sys.argv[1], encoding="utf-8"))
m["platform"] = "windows-x64"
m["minApp"] = "0.91.1"
m["description"] = m["description"].replace(" Brings Qt's TLS plugin for the download.", "")
json.dump(m, open(sys.argv[2], "w", encoding="utf-8"), indent=2, ensure_ascii=False)
PY
cp "$BUILD/addon/plugins/qb-translation-proofreading.dll" "$P/plugins/"
cp "$E/qb-tp-engine.dll" "$E/vcomp140.dll" "$A/"
cp "$ROOT/resources/icons/translation-proofreading.svg" "$A/icons/"
QT_VERSION=${QT_VERSION:-6.12.0}

# ---- licenses: the add-on's own, and of every library inside it
L=$A/licenses
cp "$ROOT/LICENSE" "$L/translation-proofreading-Apache-2.0.txt"
cp "$ROOT/NOTICE" "$L/translation-proofreading-NOTICE.txt"
cp "$ROOT/resources/us-spelling-LICENSE.txt" "$L/SCOWL-american-spellings.txt"
cp "$ROOT/resources/en-thesaurus-LICENSE.txt" "$L/WordNet-3.0.txt"
cp "$DEPS/CTranslate2/LICENSE" "$L/CTranslate2-MIT.txt"
cp "$DEPS/CTranslate2/third_party/cpu_features/LICENSE" "$L/cpu_features-Apache-2.0.txt"
cp "$DEPS/CTranslate2/third_party/spdlog/LICENSE" "$L/spdlog-MIT.txt"
cp "$DEPS/oneDNN/LICENSE" "$L/oneDNN-Apache-2.0.txt"
cp "$DEPS/oneDNN/THIRD-PARTY-PROGRAMS" "$L/oneDNN-THIRD-PARTY-PROGRAMS.txt"
cp "$DEPS/sentencepiece/LICENSE" "$L/SentencePiece-Apache-2.0.txt"
cp "$DEPS/sentencepiece/third_party/absl/LICENSE" "$L/SentencePiece-abseil-Apache-2.0.txt"
cp "$DEPS/sentencepiece/third_party/protobuf-lite/LICENSE" "$L/SentencePiece-protobuf-lite-BSD-3-Clause.txt"
cp "$DEPS/sentencepiece/third_party/darts_clone/LICENSE" "$L/SentencePiece-darts_clone-BSD-2-Clause.txt"
cp "$DEPS/sentencepiece/third_party/esaxx/LICENSE" "$L/SentencePiece-esaxx-MIT.txt"
cat > "$L/Microsoft-Visual-C++-OpenMP-runtime.txt" <<'TEXT'
Microsoft Visual C++ OpenMP runtime (vcomp140.dll)
Copyright (c) Microsoft Corporation. All rights reserved.

This DLL is part of Microsoft's redistributable Visual C++ runtime files, copied unchanged from Visual Studio
(Microsoft.VC*.OpenMP). Microsoft allows it to be distributed with programs built with Visual Studio under the terms
for "Distributable Code" of the Microsoft Software License Terms of Visual Studio
(https://visualstudio.microsoft.com/license-terms/, and the file redist.txt of Visual Studio). It is not covered by
the add-on's license, may not be changed, and is used only as part of this add-on.
TEXT
cat > "$L/README.txt" <<TEXT
Libraries in the add-on "Translation and Proofreading" $VERSION for Windows
=========================================================================

plugins/qb-translation-proofreading.dll, translation-proofreading/qb-tp-engine.dll (the add-on's own code)
    Apache License 2.0 (translation-proofreading-Apache-2.0.txt, translation-proofreading-NOTICE.txt).
    Source: https://github.com/Shelin-boop/Quillbinder_Translation_Addon
    Built with Microsoft Visual C++ against Qt $QT_VERSION (The Qt Company's build for MSVC 2022, 64-bit).
    Built into the tool library: the American spellings made from SCOWL by Kevin Atkinson
    (SCOWL-american-spellings.txt), the words of a similar meaning made from Princeton WordNet 3.0
    (WordNet-3.0.txt), and the add-on's icon (drawn for it, Apache License 2.0).
    The engine also holds the tokenizer of the Qwen models (src/engine/Bpe.cpp, the add-on's own code).

Linked into qb-tp-engine.dll (static libraries, built by packaging/build-deps-windows.sh of the source):
    CTranslate2 4.9.0 (github.com/OpenNMT/CTranslate2): MIT (CTranslate2-MIT.txt), with cpu_features
        (Apache-2.0) and spdlog (MIT), and the header-only half.hpp (MIT) and nlohmann/json (MIT) whose notices
        are in their headers in the CTranslate2 source.
    oneDNN 3.9.1 (github.com/uxlfoundation/oneDNN): Apache-2.0 (oneDNN-Apache-2.0.txt); the parts it contains from
        others: oneDNN-THIRD-PARTY-PROGRAMS.txt.
    SentencePiece 0.2.1 (github.com/google/sentencepiece): Apache-2.0, with abseil (Apache-2.0), protobuf-lite
        (BSD-3-Clause), darts_clone (BSD-2-Clause) and esaxx (MIT).

translation-proofreading/vcomp140.dll
    Microsoft's OpenMP runtime of Visual C++, unchanged (Microsoft-Visual-C++-OpenMP-runtime.txt).

Qt's TLS plugin for the model download is not in this add-on: Quillbinder for Windows brings Qt's Schannel plugin.

The models are not part of the add-on: it downloads the ones the writer chooses, from the releases
"models-1" and "models-2" of the add-on's repository, under their own licenses (each model's MODEL-LICENSE.txt):
    CoEdIT Large (Grammarly): CC BY-NC 4.0, non-commercial use only.
    CoEdIT Base (J. Bochi): Apache-2.0.
    OPUS-MT German to English (University of Helsinki): Apache-2.0.
    Qwen3 1.7B and Qwen3 4B (Qwen team, Alibaba Cloud): Apache-2.0.
    OPUS-MT Big, German to English (University of Helsinki): CC BY 4.0.
TEXT

mkdir -p "$OUT"
"$PY" "$(cygpath -w "$ROOT/packaging/make-package.py")" "$(cygpath -w "$STAGE")" \
    "$(cygpath -w "$OUT/translation-proofreading-$VERSION-windows.qbpkg")"
