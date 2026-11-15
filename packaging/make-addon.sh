#!/bin/bash
# Stages the add-on from a build and writes the package.
#   packaging/make-addon.sh <build dir> <out dir>      -> <out dir>/translation-proofreading-<version>.qbpkg
# The build must be a release build (TP_TEST_SWITCHES off) against Qt 6.12.0. QB_QT_DIR (default /opt/qt/6.12.0/gcc_64)
# gives Qt's TLS plugin for the model download; without it the package is made without the plugin and a warning (the
# download then needs the plugin of another add-on, such as Speech Transcription).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD=$(cd "$1" && pwd)
OUT=$2
DEPS=${TP_DEPS:-$ROOT/deps}
QT=${QB_QT_DIR:-/opt/qt/6.12.0/gcc_64}
VERSION=$(python3 -c "import json,sys; print(json.load(open(sys.argv[1]))['version'])" "$ROOT/packaging/manifest.json")
CMAKE_VERSION=$(sed -n 's/^set(TP_VERSION *"\(.*\)")/\1/p' "$ROOT/CMakeLists.txt")
[ "$VERSION" = "$CMAKE_VERSION" ] || { echo "manifest.json has $VERSION, CMakeLists.txt $CMAKE_VERSION" >&2; exit 1; }
if grep -q "TP_TEST_SWITCHES:BOOL=ON" "$BUILD/CMakeCache.txt"; then
    echo "$BUILD is a test build (TP_TEST_SWITCHES=ON); packages come from a release build" >&2
    exit 1
fi
[ -f "$BUILD/addon/translation-proofreading/libqb-tp-engine.so" ] || { echo "no engine in $BUILD" >&2; exit 1; }

STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
P=$STAGE/payload
A=$P/translation-proofreading
mkdir -p "$P/plugins" "$A/lib" "$A/icons" "$A/licenses"
cp "$ROOT/packaging/manifest.json" "$STAGE/"
cp "$BUILD/addon/plugins/libqb-translation-proofreading.so" "$P/plugins/"
cp "$BUILD/addon/translation-proofreading/libqb-tp-engine.so" "$A/"
cp -L "$BUILD/addon/translation-proofreading/lib/libgomp.so.1" "$A/lib/"
cp "$ROOT/resources/icons/translation-proofreading.svg" "$A/icons/"
QT_VERSION=none
if [ -f "$QT/plugins/tls/libqopensslbackend.so" ]; then
    mkdir -p "$A/qtplugins/tls"
    cp "$QT/plugins/tls/libqopensslbackend.so" "$A/qtplugins/tls/"
    QT_VERSION=$(sed -n 's/.*QT_VERSION_STR *"\(.*\)".*/\1/p' "$QT/include/QtCore/qtcore-config.h" 2>/dev/null | head -1)
    [ -n "$QT_VERSION" ] || QT_VERSION=$(basename "$(dirname "$QT")")
else
    echo "warning: no Qt TLS plugin in $QT (QB_QT_DIR); the package cannot download models by itself" >&2
fi
strip --strip-unneeded "$P"/plugins/*.so "$A"/*.so "$A"/lib/*.so* 2>/dev/null || true
[ -d "$A/qtplugins" ] && strip --strip-unneeded "$A"/qtplugins/tls/*.so 2>/dev/null || true

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
cp /usr/share/common-licenses/GPL-3 "$L/GPL-3.0.txt"
cp /usr/share/common-licenses/LGPL-3 "$L/LGPL-3.0.txt"
# the GCC Runtime Library Exception 3.1, as the Ubuntu package libgomp1 gives it
awk '/^GCC RUNTIME LIBRARY EXCEPTION/{f=1} f{print} f && /requirements of the license of GCC\./{exit}' \
    /usr/share/doc/libgomp1/copyright > "$L/GCC-Runtime-Library-Exception-3.1.txt"
[ -s "$L/GCC-Runtime-Library-Exception-3.1.txt" ] || { echo "the GCC Runtime Library Exception was not found" >&2; exit 1; }
cat > "$L/README.txt" <<TEXT
Libraries in the add-on "Translation and Proofreading" $VERSION
================================================================

plugins/libqb-translation-proofreading.so, translation-proofreading/libqb-tp-engine.so (the add-on's own code)
    Apache License 2.0 (translation-proofreading-Apache-2.0.txt, translation-proofreading-NOTICE.txt).
    Source: https://github.com/Shelin-boop/Quillbinder_Translation_Addon
    Built into the tool library: the American spellings made from SCOWL by Kevin Atkinson
    (SCOWL-american-spellings.txt), the words of a similar meaning made from Princeton WordNet 3.0
    (WordNet-3.0.txt), and the add-on's icon (drawn for it, Apache License 2.0).
    The engine also holds the tokenizer of the Qwen models (src/engine/Bpe.cpp, the add-on's own code).

Linked into libqb-tp-engine.so (static libraries, built by packaging/build-deps.sh of the source):
    CTranslate2 4.9.0 (github.com/OpenNMT/CTranslate2): MIT (CTranslate2-MIT.txt), with cpu_features
        (Apache-2.0) and spdlog (MIT), and the header-only half.hpp (MIT) and nlohmann/json (MIT) whose notices
        are in their headers in the CTranslate2 source.
    oneDNN 3.9.1 (github.com/uxlfoundation/oneDNN): Apache-2.0 (oneDNN-Apache-2.0.txt); the parts it contains from
        others: oneDNN-THIRD-PARTY-PROGRAMS.txt.
    SentencePiece 0.2.1 (github.com/google/sentencepiece): Apache-2.0, with abseil (Apache-2.0), protobuf-lite
        (BSD-3-Clause), darts_clone (BSD-2-Clause) and esaxx (MIT).

translation-proofreading/lib/libgomp.so.1
    GNU OpenMP runtime of GCC 14 (Ubuntu 24.04's package libgomp1), unchanged: GPL 3.0 with the GCC Runtime
    Library Exception (GPL-3.0.txt, GCC-Runtime-Library-Exception-3.1.txt).
    Source: https://gcc.gnu.org/ (Ubuntu source package: gcc-14).

translation-proofreading/qtplugins/tls/libqopensslbackend.so
    Qt's TLS plugin for OpenSSL (Qt Network $QT_VERSION), The Qt Company's own build for Linux, unchanged, for
    the download of a model over https; it uses the system's OpenSSL 3. GNU LGPL 3.0 (LGPL-3.0.txt, which refers
    to GPL-3.0.txt). Source: https://download.qt.io/official_releases/qt/ (qtbase of that version).

The models are not part of the add-on: it downloads the ones the writer chooses, from the releases
"models-1" and "models-2" of the add-on's repository, under their own licenses (each model's MODEL-LICENSE.txt):
    CoEdIT Large (Grammarly): CC BY-NC 4.0, non-commercial use only.
    CoEdIT Base (J. Bochi): Apache-2.0.
    OPUS-MT German to English (University of Helsinki): Apache-2.0.
    Qwen3 1.7B and Qwen3 4B (Qwen team, Alibaba Cloud): Apache-2.0.
    OPUS-MT Big, German to English (University of Helsinki): CC BY 4.0.
TEXT

mkdir -p "$OUT"
python3 "$ROOT/packaging/make-package.py" "$STAGE" "$OUT/translation-proofreading-$VERSION.qbpkg"
