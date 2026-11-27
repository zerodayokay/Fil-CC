#!/usr/bin/env bash
set -euo pipefail

if [[ "$(uname -s):$(uname -m)" != Linux:x86_64 ]]; then
  echo "This build script needs Linux x86_64, including WSL 2." >&2
  exit 2
fi

source_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build_dir=$(realpath -m "${1:-$source_dir/build-filcc}")
jobs=${FILCC_JOBS:-$(nproc)}
cmake -S "$source_dir/llvm" -B "$build_dir" -G Ninja \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE="-O1 -DNDEBUG" \
  -DLLVM_ENABLE_PROJECTS=clang -DLLVM_TARGETS_TO_BUILD=X86 \
  -DLLVM_ENABLE_LLD=ON -DLLVM_ENABLE_ASSERTIONS=ON \
  -DLLVM_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_EXAMPLES=OFF \
  -DLLVM_INCLUDE_BENCHMARKS=OFF -DLLVM_ENABLE_ZSTD=OFF
cmake --build "$build_dir" --target clang --parallel "$jobs"
bash "$source_dir/filcc/install.sh" "$build_dir/bin/clang-20" "$build_dir/runtime"
