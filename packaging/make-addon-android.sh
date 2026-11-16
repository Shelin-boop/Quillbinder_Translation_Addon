#!/bin/bash
# Stages the add-on for Quillbinder for Android from an Android build and writes the package (2026-11-16).
#   packaging/make-addon-android.sh <build dir> <out dir> <abi: arm64-v8a | x86_64> <ndk folder>
#     -> <out dir>/translation-proofreading-<version>-android-arm64.qbpkg (or -android-x86_64, for emulated tablets)
# Quillbinder for Android takes add-ons with "platform": "android-arm64" (or "android-x86_64"), installs them into the
# app's private folder (Android loads libraries an app carries only from there) and brings Qt's TLS plugin with
# OpenSSL itself, which the model download uses. minApp 0.91.3: the first Quillbinder for Android that installs
# add-ons. The build must be a release build (TP_TEST_SWITCHES off) against Qt 6.12.0 for Android.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD=$(cd "$1" && pwd)
OUT=$2
ABI=${3:?abi}
NDK=${4:?ndk folder}
DEPS=${TP_DEPS:-$ROOT/deps}
case "$ABI" in
  arm64-v8a) PLATFORM=android-arm64 ;;
  x86_64)    PLATFORM=android-x86_64 ;;
  *) echo "unknown ABI: $ABI" >&2; exit 2 ;;
esac
VERSION=$(python3 -c "import json,sys; print(json.load(open(sys.argv[1]))['version'])" "$ROOT/packaging/manifest.json")
CMAKE_VERSION=$(sed -n 's/^set(TP_VERSION *"\(.*\)")/\1/p' "$ROOT/CMakeLists.txt")
[ "$VERSION" = "$CMAKE_VERSION" ] || { echo "manifest.json has $VERSION, CMakeLists.txt $CMAKE_VERSION" >&2; exit 1; }
if grep -q "TP_TEST_SWITCHES:BOOL=ON" "$BUILD/CMakeCache.txt"; then
    echo "$BUILD is a test build (TP_TEST_SWITCHES=ON); packages come from a release build" >&2
    exit 1
fi
E=$BUILD/addon/translation-proofreading
[ -f "$E/libqb-tp-engine.so" ] || { echo "no engine in $BUILD" >&2; exit 1; }
STRIP="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip"

STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
P=$STAGE/payload
A=$P/translation-proofreading
mkdir -p "$P/plugins" "$A/icons" "$A/licenses"
python3 - "$ROOT/packaging/manifest.json" "$STAGE/manifest.json" "$PLATFORM" <<'PY'
import json, sys
m = json.load(open(sys.argv[1], encoding="utf-8"))
m["platform"] = sys.argv[3]
m["minApp"] = "0.91.3"
m["description"] = (m["description"].replace(" Brings Qt's TLS plugin for the download.", "")
                    .replace("Engine: CTranslate2 with oneDNN and SentencePiece.", "Engine: CTranslate2 with Ruy and SentencePiece.")
                    .replace("on this computer only", "on this device only")
                    .replace("too large for the computer", "too large for the device"))
json.dump(m, open(sys.argv[2], "w", encoding="utf-8"), indent=2, ensure_ascii=False)
PY
cp "$BUILD/addon/plugins/libqb-translation-proofreading.so" "$P/plugins/"
cp "$E/libqb-tp-engine.so" "$A/"
cp "$ROOT/resources/icons/translation-proofreading.svg" "$A/icons/"
"$STRIP" --strip-unneeded "$P"/plugins/*.so "$A"/*.so

# ---- licenses: the add-on's own, and of every library inside it
L=$A/licenses
cp "$ROOT/LICENSE" "$L/translation-proofreading-Apache-2.0.txt"
cp "$ROOT/NOTICE" "$L/translation-proofreading-NOTICE.txt"
cp "$ROOT/resources/us-spelling-LICENSE.txt" "$L/SCOWL-american-spellings.txt"
cp "$ROOT/resources/en-thesaurus-LICENSE.txt" "$L/WordNet-3.0.txt"
cp "$ROOT/resources/llvm-openmp-LICENSE.txt" "$L/LLVM-OpenMP-Apache-2.0-with-LLVM-exception.txt"
cp "$DEPS/CTranslate2/LICENSE" "$L/CTranslate2-MIT.txt"
cp "$DEPS/CTranslate2/third_party/ruy/LICENSE" "$L/ruy-Apache-2.0.txt"
cp "$DEPS/CTranslate2/third_party/ruy/third_party/cpuinfo/LICENSE" "$L/cpuinfo-BSD-2-Clause.txt"
cp "$DEPS/CTranslate2/third_party/spdlog/LICENSE" "$L/spdlog-MIT.txt"
X86=""
if [ "$ABI" = x86_64 ]; then
    cp "$DEPS/CTranslate2/third_party/cpu_features/LICENSE" "$L/cpu_features-Apache-2.0.txt"
    X86="
        and, in this package for x86_64, cpu_features (Apache-2.0)"
fi
cp "$DEPS/sentencepiece/LICENSE" "$L/SentencePiece-Apache-2.0.txt"
cp "$DEPS/sentencepiece/third_party/absl/LICENSE" "$L/SentencePiece-abseil-Apache-2.0.txt"
cp "$DEPS/sentencepiece/third_party/protobuf-lite/LICENSE" "$L/SentencePiece-protobuf-lite-BSD-3-Clause.txt"
cp "$DEPS/sentencepiece/third_party/darts_clone/LICENSE" "$L/SentencePiece-darts_clone-BSD-2-Clause.txt"
cp "$DEPS/sentencepiece/third_party/esaxx/LICENSE" "$L/SentencePiece-esaxx-MIT.txt"
NDKV=$(sed -n 's/^Pkg.Revision *= *//p' "$NDK/source.properties" | tr -d '\r')
cat > "$L/README.txt" <<TEXT
Libraries in the add-on "Translation and Proofreading" $VERSION for Android ($ABI)
================================================================================

plugins/libqb-translation-proofreading.so, translation-proofreading/libqb-tp-engine.so (the add-on's own code)
    Apache License 2.0 (translation-proofreading-Apache-2.0.txt, translation-proofreading-NOTICE.txt).
    Source: https://github.com/Shelin-boop/Quillbinder_Translation_Addon
    Built into the tool library: the American spellings made from SCOWL by Kevin Atkinson
    (SCOWL-american-spellings.txt), the words of a similar meaning made from Princeton WordNet 3.0
    (WordNet-3.0.txt), and the add-on's icon (drawn for it, Apache License 2.0).
    The engine also holds the tokenizer of the Qwen models (src/engine/Bpe.cpp, the add-on's own code).

Linked into libqb-tp-engine.so (static libraries, built by packaging/build-deps-android.sh of the source with the
Android NDK $NDKV):
    CTranslate2 4.9.0 (github.com/OpenNMT/CTranslate2): MIT (CTranslate2-MIT.txt), with Ruy (Apache-2.0,
        ruy-Apache-2.0.txt), cpuinfo (BSD-2-Clause, cpuinfo-BSD-2-Clause.txt) and spdlog (MIT)$X86, and the
        header-only half.hpp (MIT) and nlohmann/json (MIT) whose notices are in their headers in the CTranslate2
        source.
    SentencePiece 0.2.1 (github.com/google/sentencepiece): Apache-2.0, with abseil (Apache-2.0), protobuf-lite
        (BSD-3-Clause), darts_clone (BSD-2-Clause) and esaxx (MIT).
    LLVM's OpenMP runtime (libomp of the Android NDK $NDKV, unchanged): Apache License 2.0 with LLVM Exceptions
        (LLVM-OpenMP-Apache-2.0-with-LLVM-exception.txt). Source: https://github.com/llvm/llvm-project (openmp).

The C++ library (libc++_shared.so) and Qt (Core, Gui, Widgets, Network) are Quillbinder for Android's own: the add-on
uses those of the app.

The models are not part of the add-on: it downloads the ones the writer chooses, from the releases
"models-1" and "models-2" of the add-on's repository, under their own licenses (each model's MODEL-LICENSE.txt):
    CoEdIT Large (Grammarly): CC BY-NC 4.0, non-commercial use only.
    CoEdIT Base (J. Bochi): Apache-2.0.
    OPUS-MT German to English (University of Helsinki): Apache-2.0.
    Qwen3 1.7B and Qwen3 4B (Qwen team, Alibaba Cloud): Apache-2.0.
    OPUS-MT Big, German to English (University of Helsinki): CC BY 4.0.
TEXT

mkdir -p "$OUT"
python3 "$ROOT/packaging/make-package.py" "$STAGE" "$OUT/translation-proofreading-$VERSION-$PLATFORM.qbpkg"
