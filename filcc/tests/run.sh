#!/usr/bin/env bash
set -euo pipefail

if [[ $# -eq 0 ]]; then
  echo "Usage: $0 COMPILER [FLAGS]" >&2
  exit 2
fi

compiler=$1
shift
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
scratch=$(mktemp -d)
trap 'rm -rf -- "$scratch"' EXIT
"$compiler" -O3 -pthread "$@" -I "$root" "$root/kernels.c" "$root/tests/regress.c" -o "$scratch/regress"
timeout 60s "$scratch/regress"
