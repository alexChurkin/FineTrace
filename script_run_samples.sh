#!/usr/bin/env bash
set -euo pipefail

# Device mapping:
#   OpenCL platform 0, device 0: Intel Arc Graphics        (GPU)
#   OpenCL platform 1, device 0: Intel Core Ultra 5 125H   (CPU)

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SAMPLES="$ROOT_DIR/samples"
RODINIA="$SAMPLES/cl_rodinia_benchmarks"

REPEATS=10
RUN_CPU=0
RUN_GPU=0
RODINIA_DATA_DIR="${RODINIA_DATA_DIR:-$RODINIA/data}"

usage() {
  echo "Usage: $0 [--cpu] [--gpu] [-n N] [--help]"
  echo ""
  echo "Run benchmark samples with per-run timing and average."
  echo ""
  echo "  --cpu    Run on CPU only (Intel Core Ultra 5 125H,"
  echo "           OpenCL platform 1 device 0)."
  echo "  --gpu    Run on GPU only (Intel Arc Graphics,"
  echo "           OpenCL platform 0 device 0). Also includes ze_gemm."
  echo "  (none)   Run on both CPU and GPU (default)."
  echo "  -n N     Number of repeats per benchmark (default: $REPEATS)."
  echo "  --help   Show this help."
  echo ""
  echo "CPU benchmarks: cl_gemm, bench_b+tree, bench_bfs, bench_gaussian, bench_nw"
  echo "GPU benchmarks: CPU benchmarks + ze_gemm"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --cpu)       RUN_CPU=1; shift ;;
    --gpu)       RUN_GPU=1; shift ;;
    -n)          REPEATS="$2"; shift 2 ;;
    --help|-h)   usage; exit 0 ;;
    *)           echo "Unknown option: $1"; usage; exit 1 ;;
  esac
done

# Default: run on both if neither flag was specified.
if [[ $RUN_CPU -eq 0 && $RUN_GPU -eq 0 ]]; then
  RUN_CPU=1
  RUN_GPU=1
fi

clinfo -l

# Resolve to absolute path so it works regardless of `cd` inside run_bench.
RODINIA_DATA_DIR="$(cd "$RODINIA_DATA_DIR" && pwd)"

# Results storage: one entry per "<mode>|<bench>|<avg_ms>".
RESULTS=()

# Run a command N times from a given directory, print per-run and average time,
# and append the average to RESULTS for the final summary table.
# Usage: run_bench <mode> <name> <dir> <cmd> [args...]
run_bench() {
  local mode="$1" name="$2" dir="$3"
  shift 3
  local total_ms=0
  echo "=== $name [$mode] ($REPEATS runs) ==="
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
  RESULTS+=("$mode|$name|$avg_ms")
}

# Run the full benchmark suite for a given mode (cpu/gpu).
# Usage: run_suite <mode>
run_suite() {
  local mode="$1"
  local cl_platform cl_device
  if [[ $mode == "gpu" ]]; then
    cl_platform=0; cl_device=0
  else
    cl_platform=1; cl_device=0
  fi

  echo "########################################"
  echo "# Running suite on: $mode"
  echo "########################################"
  echo ""

  run_bench "$mode" "cl_gemm" \
    "$SAMPLES/cl_gemm/build" \
    ./cl_gemm "$mode" 512 512

  if [[ $mode == "gpu" ]]; then
    run_bench "$mode" "ze_gemm" \
      "$SAMPLES/ze_gemm/build" \
      ./ze_gemm 512 512
  fi

  run_bench "$mode" "bench_b+tree" \
    "$RODINIA/bench_b+tree" \
    ./b+tree.out file "$RODINIA_DATA_DIR/b+tree/mil.txt" command "$RODINIA_DATA_DIR/b+tree/command.txt" \
      -p "$cl_platform" -d "$cl_device"

  run_bench "$mode" "bench_bfs" \
    "$RODINIA/bench_bfs" \
    ./bfs.out "$RODINIA_DATA_DIR/bfs/graph1MW_6.txt" \
      -p "$cl_platform" -d "$cl_device"

  run_bench "$mode" "bench_gaussian" \
    "$RODINIA/bench_gaussian" \
    ./gaussian.out -s 2048 \
      -p "$cl_platform" -d "$cl_device"

  run_bench "$mode" "bench_nw" \
    "$RODINIA/bench_nw" \
    ./nw.out 8192 10 ./nw.cl \
      -p "$cl_platform" -d "$cl_device"
}

# Print a final summary table comparing CPU and GPU averages per benchmark.
print_summary() {
  echo "########################################"
  echo "# Summary (average ms over $REPEATS runs)"
  echo "########################################"

  # Collect a unique, ordered list of benchmark names from RESULTS.
  local names=()
  local entry mode name avg
  for entry in "${RESULTS[@]}"; do
    name="${entry#*|}"; name="${name%|*}"
    local seen=0 n
    for n in "${names[@]:-}"; do
      [[ "$n" == "$name" ]] && { seen=1; break; }
    done
    [[ $seen -eq 0 ]] && names+=("$name")
  done

  printf "%-20s | %12s | %12s\n" "Benchmark" "CPU (ms)" "GPU (ms)"
  printf -- "---------------------+--------------+--------------\n"

  for name in "${names[@]}"; do
    local cpu_val="-" gpu_val="-"
    for entry in "${RESULTS[@]}"; do
      mode="${entry%%|*}"
      local rest="${entry#*|}"
      local rname="${rest%|*}"
      avg="${rest##*|}"
      if [[ "$rname" == "$name" ]]; then
        if [[ "$mode" == "cpu" ]]; then cpu_val="$avg"; fi
        if [[ "$mode" == "gpu" ]]; then gpu_val="$avg"; fi
      fi
    done
    printf "%-20s | %12s | %12s\n" "$name" "$cpu_val" "$gpu_val"
  done
  echo ""
}

[[ $RUN_CPU -eq 1 ]] && run_suite "cpu"
[[ $RUN_GPU -eq 1 ]] && run_suite "gpu"

print_summary
