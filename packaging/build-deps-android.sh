#!/bin/bash
# Builds the engine libraries for Android (2026-11-16): CTranslate2 4.9.0 and SentencePiece 0.2.1 as static libraries
# for one Android ABI, with the NDK's compiler, into deps-android/<abi>/install. On Android CTranslate2 computes with
# Ruy (its GEMM backend for ARM, which also has float and int8 kernels for x86_64) instead of oneDNN, which it uses
# only on Intel and AMD processors; OpenMP is LLVM's from the NDK (linked into the engine statically).
#   packaging/build-deps-android.sh <ndk folder> <abi: arm64-v8a | x86_64>      (about 10 to 20 minutes per ABI)
# The sources are fetched into deps/ (the same tags as packaging/build-deps.sh, which stays the Linux recipe).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
NDK=${1:?ndk folder}
ABI=${2:?abi}
case "$ABI" in arm64-v8a | x86_64) ;; *) echo "unknown ABI: $ABI" >&2; exit 2 ;; esac
D=${TP_DEPS:-$ROOT/deps}
B=$ROOT/deps-android/$ABI
P=$B/install
mkdir -p "$D" "$B"
fetch() { # <dir> <url> <tag>
    [ -d "$D/$1/.git" ] || git clone --depth 1 --branch "$3" --recurse-submodules --shallow-submodules "$2" "$D/$1"
}
fetch CTranslate2 https://github.com/OpenNMT/CTranslate2.git v4.9.0
fetch sentencepiece https://github.com/google/sentencepiece.git v0.2.1
J=${JOBS:-$(nproc)}
# Android 9 (API 28) is the oldest Quillbinder for Android runs on; the C++ library is the shared one the app brings
TC=(-DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" -DANDROID_ABI="$ABI" -DANDROID_PLATFORM=android-28
    -DANDROID_STL=c++_shared -DCMAKE_BUILD_TYPE=Release -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5)

cmake -S "$D/CTranslate2" -B "$B/b-ct2" -G Ninja "${TC[@]}" -DCMAKE_INSTALL_PREFIX="$P" \
  -DWITH_MKL=OFF -DWITH_DNNL=OFF -DWITH_RUY=ON -DOPENMP_RUNTIME=COMP -DBUILD_CLI=OFF -DBUILD_SHARED_LIBS=OFF
cmake --build "$B/b-ct2" -j "$J"
cmake --install "$B/b-ct2"
# CTranslate2 installs none of the static libraries its own needs: Ruy (many small ones), cpuinfo and, on x86_64,
# cpu_features; CMakeLists.txt links every file of this folder in one group
mkdir -p "$P/lib/ct2-deps"
find "$B/b-ct2" -name '*.a' ! -name 'libctranslate2.a' -exec cp {} "$P/lib/ct2-deps/" \;
ls "$P/lib/ct2-deps" | wc -l | sed 's/^/libraries for CTranslate2: /'

cmake -S "$D/sentencepiece" -B "$B/b-spm" -G Ninja "${TC[@]}" -DCMAKE_INSTALL_PREFIX="$P" \
  -DSPM_ENABLE_SHARED=OFF -DSPM_ENABLE_TCMALLOC=OFF
cmake --build "$B/b-spm" -j "$J" --target sentencepiece-static
cmake --install "$B/b-spm" 2>/dev/null || {
    # the install step also wants the command-line programs, which are not built: the library and its headers by hand
    mkdir -p "$P/lib" "$P/include"
    cp "$B/b-spm/src/libsentencepiece.a" "$P/lib/"
    cp "$D/sentencepiece/src/sentencepiece_processor.h" "$D/sentencepiece/src/sentencepiece_trainer.h" "$P/include/"
}
echo "$ABI $(sed -n 's/^Pkg.Revision *= *//p' "$NDK/source.properties")" > "$P/built-for.txt"
echo "engine libraries for Android ($ABI) in $P"
