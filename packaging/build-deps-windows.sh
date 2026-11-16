#!/bin/bash
# The engine libraries for Windows (2026-11-16): oneDNN (static), CTranslate2 (static, with oneDNN) and SentencePiece
# (static), the same versions as packaging/build-deps.sh, built with MSVC into deps/install (CMakeLists.txt looks there).
# Runs in Git Bash inside MSVC's environment (vcvars64.bat), as the workflow "Build" does:
#   packaging/build-deps-windows.sh
# OpenMP is MSVC's own (vcomp140.dll, which the add-on brings beside its engine). The C++ runtime is the DLL one (/MD),
# as Quillbinder's program and every add-on of it use. packaging/build-deps.sh stays the Linux recipe (its hash is the
# key of the Linux caches).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
D=${TP_DEPS:-$ROOT/deps}
P=$D/install
mkdir -p "$D"
fetch() { # <dir> <url> <tag>
    [ -d "$D/$1/.git" ] || git clone -q --depth 1 --branch "$3" --recurse-submodules --shallow-submodules "$2" "$D/$1"
}
fetch oneDNN https://github.com/uxlfoundation/oneDNN.git v3.9.1
fetch CTranslate2 https://github.com/OpenNMT/CTranslate2.git v4.9.0
fetch sentencepiece https://github.com/google/sentencepiece.git v0.2.1
W() { cygpath -m "$1"; }
common=(-G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl.exe -DCMAKE_CXX_COMPILER=cl.exe
        -DCMAKE_INSTALL_PREFIX="$(W "$P")" -DCMAKE_PREFIX_PATH="$(W "$P")" -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL
        -DCMAKE_POLICY_DEFAULT_CMP0091=NEW -DCMAKE_POLICY_VERSION_MINIMUM=3.5)
# (CMAKE_POLICY_VERSION_MINIMUM: the Windows machine has CMake 4, which refuses cpu_features' cmake_minimum_required
# below 3.5; first run of 2026-11-16)
command -v sccache > /dev/null && common+=(-DCMAKE_C_COMPILER_LAUNCHER=sccache -DCMAKE_CXX_COMPILER_LAUNCHER=sccache)
t0=$(date +%s)
cmake -S "$(W "$D/oneDNN")" -B "$(W "$D/b-onednn")" "${common[@]}" \
  -DDNNL_LIBRARY_TYPE=STATIC -DDNNL_CPU_RUNTIME=OMP -DDNNL_GPU_RUNTIME=NONE -DDNNL_BUILD_EXAMPLES=OFF \
  -DDNNL_BUILD_TESTS=OFF -DDNNL_ENABLE_WORKLOAD=INFERENCE -DDNNL_ENABLE_PRIMITIVE="MATMUL;REORDER;INNER_PRODUCT;CONVOLUTION" \
  -DONEDNN_BUILD_GRAPH=OFF
cmake --build "$(W "$D/b-onednn")"
cmake --install "$(W "$D/b-onednn")"
echo "oneDNN: $(( ($(date +%s) - t0) / 60 )) min"
cmake -S "$(W "$D/CTranslate2")" -B "$(W "$D/b-ct2")" "${common[@]}" \
  -DWITH_MKL=OFF -DWITH_DNNL=ON -DOPENMP_RUNTIME=COMP -DBUILD_CLI=OFF -DBUILD_SHARED_LIBS=OFF
cmake --build "$(W "$D/b-ct2")"
cmake --install "$(W "$D/b-ct2")"
# CTranslate2 does not install cpu_features, which its static library needs
find "$D/b-ct2" -name 'cpu_features.lib' -exec cp {} "$P/lib/" \;
echo "CTranslate2: $(( ($(date +%s) - t0) / 60 )) min"
cmake -S "$(W "$D/sentencepiece")" -B "$(W "$D/b-spm")" "${common[@]}" -DSPM_ENABLE_SHARED=OFF
cmake --build "$(W "$D/b-spm")"
cmake --install "$(W "$D/b-spm")"
echo "SentencePiece: $(( ($(date +%s) - t0) / 60 )) min"
ls "$P/lib"
echo "oneDNN, CTranslate2 and SentencePiece are in $P"
