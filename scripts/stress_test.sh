#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build"
PORT="${PORT:-9000}"

cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD" -j"$(nproc)"

SERVER="$BUILD/tds_server"
BENCH="$BUILD/tds_bench"

"$SERVER" --port "$PORT" &
SERVER_PID=$!
trap 'kill $SERVER_PID 2>/dev/null || true' EXIT
sleep 0.5

echo "warmup..."
"$BENCH" --port "$PORT" --connections 20 --requests 200 --mode ping >/dev/null

echo "stress test: 200 connections, ping mode"
RESULT=$("$BENCH" --port "$PORT" --connections 200 --requests 500 --mode ping)
echo "$RESULT"

RPS=$(echo "$RESULT" | awk '/throughput_rps/ {print $2}')
P99=$(echo "$RESULT" | awk '/latency_p99_us/ {print $2}')

echo ""
echo "Summary:"
echo "  throughput: ${RPS} req/s"
echo "  p99 latency: ${P99} us ($(echo "scale=2; $P99/1000" | bc) ms)"
