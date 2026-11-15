#!/bin/bash
# Builds the engine libraries of the add-on: oneDNN (static), CTranslate2 (static, with oneDNN), SentencePiece (static),
# from their sources at fixed versions, into deps/install (CMakeLists.txt looks there).
#   packaging/build-deps.sh          (about 40 minutes on 2 processor cores)
# The sources are fetched into deps/ (git, public GitHub repositories).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
D=${TP_DEPS:-$ROOT/deps}
P=$D/install
mkdir -p "$D"
fetch() { # <dir> <url> <tag>
    [ -d "$D/$1/.git" ] || git clone --depth 1 --branch "$3" --recurse-submodules --shallow-submodules "$2" "$D/$1"
}
fetch oneDNN https://github.com/uxlfoundation/oneDNN.git v3.9.1
fetch CTranslate2 https://github.com/OpenNMT/CTranslate2.git v4.9.0
fetch sentencepiece https://github.com/google/sentencepiece.git v0.2.1
J=${JOBS:-$(nproc)}
cmake -S "$D/oneDNN" -B "$D/b-onednn" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$P" \
  -DDNNL_LIBRARY_TYPE=STATIC -DDNNL_CPU_RUNTIME=OMP -DDNNL_GPU_RUNTIME=NONE -DDNNL_BUILD_EXAMPLES=OFF \
  -DDNNL_BUILD_TESTS=OFF -DDNNL_ENABLE_WORKLOAD=INFERENCE -DDNNL_ENABLE_PRIMITIVE="MATMUL;REORDER;INNER_PRODUCT;CONVOLUTION" \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DONEDNN_BUILD_GRAPH=OFF
cmake --build "$D/b-onednn" -j"$J"
cmake --install "$D/b-onednn"
cmake -S "$D/CTranslate2" -B "$D/b-ct2" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$P" \
  -DCMAKE_PREFIX_PATH="$P" -DWITH_MKL=OFF -DWITH_DNNL=ON -DOPENMP_RUNTIME=COMP -DBUILD_CLI=OFF \
  -DBUILD_SHARED_LIBS=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON
cmake --build "$D/b-ct2" -j"$J"
cmake --install "$D/b-ct2"
# CTranslate2 does not install cpu_features, which its static library needs
cp "$D/b-ct2/third_party/cpu_features/libcpu_features.a" "$P/lib/"
cmake -S "$D/sentencepiece" -B "$D/b-spm" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$P" \
  -DSPM_ENABLE_SHARED=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON
cmake --build "$D/b-spm" -j"$J"
cmake --install "$D/b-spm"
echo "oneDNN, CTranslate2 and SentencePiece are in $P"
