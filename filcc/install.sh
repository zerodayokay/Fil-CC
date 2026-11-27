#!/usr/bin/env bash
set -euo pipefail

if [[ "$(uname -s):$(uname -m)" != Linux:x86_64 ]]; then
  echo "This installer needs Linux x86_64, including WSL 2." >&2
  exit 2
fi

if [[ $# -ne 2 ]]; then
  echo "Usage: bash filcc/install.sh /path/to/patched/clang-20 INSTALL_DIRECTORY" >&2
  exit 2
fi

compiler=$(realpath "$1")
[[ -x "$compiler" ]] || { echo "Compiler is not executable: $compiler" >&2; exit 2; }
stock=$(realpath -m "$2")
if [[ ! -f "$stock/.filcc-stock-0.687" ]]; then
  if [[ -e "$stock" ]]; then
    echo "Choose a new directory for the Fil-C 0.687 runtime: $stock" >&2
    exit 2
  fi
  mkdir -p "$(dirname "$stock")"
  scratch=$(mktemp -d "$(dirname "$stock")/.filcc-stock.XXXXXX")
  trap 'rm -rf -- "$scratch"' EXIT
  archive="$scratch/stock.tar.xz"
  curl --fail --location --retry 3 -o "$archive" \
    https://github.com/pizlonator/fil-c/releases/download/v0.687/filc-0.687-linux-x86_64.tar.xz
  echo "aab538401f78a47ab261ee79afed8c6b99e1d4848e263ae159a75b03c1288da1  $archive" | sha256sum -c -
  mkdir "$scratch/dist"
  tar -xJf "$archive" --strip-components=1 -C "$scratch/dist"
  mv -T --no-clobber "$scratch/dist" "$stock"
  [[ ! -d "$scratch/dist" ]] || { echo "Installation directory already exists" >&2; exit 2; }
  (cd "$stock" && ./setup.sh)
  touch "$stock/.filcc-stock-0.687"
fi
install -m 0755 "$compiler" "$stock/build/bin/filcc-clang"
printf 'Stock Fil-C: %s/build/bin/clang\nFil-CC: %s/build/bin/filcc-clang\n' "$stock" "$stock"
