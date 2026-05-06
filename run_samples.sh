#!/usr/bin/env bash
set -euo pipefail

# Device mapping:
#   OpenCL platform 0, device 0: Intel Arc Graphics        (GPU)
#   OpenCL platform 1, device 0: Intel Core Ultra 5 125H   (CPU)

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SAMPLES="$ROOT_DIR/samples"
RODINIA="$SAMPLES/cl_rodinia_benchmarks"

REPEATS=10
MODE="cpu"

usage() {
  echo "Usage: $0 [--gpu] [-n N] [--help]"
  echo ""
  echo "Run benchmark samples with per-run timing and average."
  echo ""
  echo "  --gpu    Run on GPU (Intel Arc Graphics, OpenCL platform 0 device 0)."
  echo "           Also includes ze_gemm. Default: CPU (Intel Core Ultra 5 125H,"
  echo "           OpenCL platform 1 device 0)."
  echo "  -n N     Number of repeats per benchmark (default: $REPEATS)."
  echo "  --help   Show this help."
  echo ""
  echo "CPU benchmarks: cl_gemm, bench_b+tree, bench_bfs, bench_gaussian, bench_nw"
  echo "GPU benchmarks: same + ze_gemm"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --gpu)      MODE="gpu"; shift ;;
    -n)         REPEATS="$2"; shift 2 ;;
    --help|-h)  usage; exit 0 ;;
    *)          echo "Unknown option: $1"; usage; exit 1 ;;
  esac
done

if [[ $MODE == "gpu" ]]; then
  CL_PLATFORM=0; CL_DEVICE=0
else
  CL_PLATFORM=1; CL_DEVICE=0
fi

# Run a command N times from a given directory, print per-run and average time.
# Usage: run_bench <name> <dir> <cmd> [args...]
run_bench() {
  local name="$1" dir="$2"
  shift 2
  local total_ms=0
  echo "=== $name ($REPEATS runs) ==="
  for ((i = 1; i <= REPEATS; i++)); do
    local start end elapsed_ms
    start=$(date +%s%3N)
    (cd "$dir" && "$@") > /dev/null 2>&1
    end=$(date +%s%3N)
    elapsed_ms=$((end - start))
    total_ms=$((total_ms + elapsed_ms))
    printf "  run %2d: %d ms\n" "$i" "$elapsed_ms"
  done
  local avg_ms=$((total_ms / REPEATS))
  printf "  average: %d ms (%.3f s)\n\n" "$avg_ms" "$(echo "scale=3; $avg_ms / 1000" | bc)"
}

run_bench "cl_gemm ($MODE)" \
  "$SAMPLES/cl_gemm/build" \
  ./cl_gemm "$MODE" 1024 1

if [[ $MODE == "gpu" ]]; then
  run_bench "ze_gemm" \
    "$SAMPLES/ze_gemm/build" \
    ./ze_gemm 1024 1
fi

run_bench "bench_b+tree" \
  "$RODINIA/bench_b+tree" \
  ./b+tree.out file ../data/b+tree/mil.txt command ../data/b+tree/command.txt \
    -p "$CL_PLATFORM" -d "$CL_DEVICE"

run_bench "bench_bfs" \
  "$RODINIA/bench_bfs" \
  ./bfs.out ../data/bfs/graph1MW_6.txt \
    -p "$CL_PLATFORM" -d "$CL_DEVICE"

run_bench "bench_gaussian" \
  "$RODINIA/bench_gaussian" \
  ./gaussian.out -s 2048 \
    -p "$CL_PLATFORM" -d "$CL_DEVICE"

run_bench "bench_nw" \
  "$RODINIA/bench_nw" \
  ./nw.out 8192 10 ./nw.cl \
    -p "$CL_PLATFORM" -d "$CL_DEVICE"
