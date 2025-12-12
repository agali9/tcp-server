#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build-tsan"

cmake -S "$ROOT" -B "$BUILD" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DTDS_ENABLE_TSAN=ON
cmake --build "$BUILD" -j"$(nproc)"

echo "TSan build ready: $BUILD"
