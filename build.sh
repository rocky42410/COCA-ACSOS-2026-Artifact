#!/usr/bin/env bash
# Build the COCA artifact. Usage: ./build.sh [--test] [--native]
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$HERE"
NATIVE=OFF
RUN_TESTS=0
for a in "$@"; do
  case "$a" in
    --native) NATIVE=ON ;;
    --test)   RUN_TESTS=1 ;;
    *) echo "Unknown arg: $a" ; exit 2 ;;
  esac
done
mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=Release -DCOCA_NATIVE_ARCH="$NATIVE" ..
cmake --build . -j
if [[ "$RUN_TESTS" -eq 1 ]]; then
  ctest --output-on-failure
fi
echo "Built: $(pwd)/coca_train , $(pwd)/coca_test"
