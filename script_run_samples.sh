#!/usr/bin/env bash
set -euo pipefail

# Device mapping:
#   OpenCL platform 0, device 0: Intel Arc Graphics        (GPU)
#   OpenCL platform 1, device 0: Intel Core Ultra 5 125H   (CPU)

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SAMPLES="$ROOT_DIR/samples"
RODINIA="$SAMPLES/cl_rodinia_benchmarks"

# Path to the finetrace binary. Override via environment if needed.
FINETRACE="${FINETRACE:-$ROOT_DIR/build/finetrace}"

REPEATS=1
RUN_CPU=0
RUN_GPU=0
RODINIA_DATA_DIR="${RODINIA_DATA_DIR:-$RODINIA/data}"

# Trace variants applied to every benchmark.
# Format: "<label>|<finetrace_args>". Empty args means run without finetrace.
TRACE_VARIANTS=(
  "clean|"
  "call-logging|--call-logging"
  "device-timeline|--device-timeline"
  "both|--call-logging --device-timeline"
)

usage() {
  echo "Usage: $0 [--cpu] [--gpu] [-n N] [--help]"
  echo ""
  echo "Run benchmark samples with per-run timing and average."
  echo "Each benchmark runs in 4 variants: clean, --call-logging,"
  echo "--device-timeline, and both flags together."
  echo ""
  echo "  --cpu    Run on CPU only (Intel Core Ultra 5 125H,"
  echo "           OpenCL platform 1 device 0)."
  echo "  --gpu    Run on GPU only (Intel Arc Graphics,"
  echo "           OpenCL platform 0 device 0). Also includes ze_gemm."
  echo "  (none)   Run on both CPU and GPU (default)."
  echo "  -n N     Number of repeats per benchmark (default: $REPEATS)."
  echo "  --help   Show this help."
  echo ""
  echo "Environment:"
  echo "  FINETRACE          Path to finetrace binary (default: $FINETRACE)."
  echo "  RODINIA_DATA_DIR   Path to Rodinia data dir."
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

# Sanity check: warn early if finetrace is missing for the non-clean variants.
if [[ ! -x "$FINETRACE" ]]; then
  echo "Warning: finetrace not found or not executable at: $FINETRACE"
  echo "         Non-clean variants will fail. Set FINETRACE=/path/to/finetrace to override."
  echo ""
fi

clinfo -l

# Resolve to absolute path so it works regardless of `cd` inside run_bench.
RODINIA_DATA_DIR="$(cd "$RODINIA_DATA_DIR" && pwd)"

# Results storage: one entry per "<mode>|<variant>|<bench>|<avg_ms>".
RESULTS=()

# Run a command N times from a given directory, print per-run and average time,
# and append the average to RESULTS for the final summary table.
#
# Usage: run_bench <mode> <variant_label> <finetrace_args> <name> <dir> <cmd> [args...]
#   - If <finetrace_args> is empty, the command is executed directly.
#   - Otherwise: <FINETRACE> <finetrace_args> <cmd> [args...]
#     (finetrace expects no `--` separator and the wrapped argv as plain
#      unquoted tokens, so $@ is intentionally word-split.)
run_bench() {
  local mode="$1" variant="$2" ft_args="$3" name="$4" dir="$5"
  shift 5
  local total_ms=0
  echo "=== $name [$mode / $variant] ($REPEATS runs) ==="
  for ((i = 1; i <= REPEATS; i++)); do
    local start end elapsed_ms
    start=$(date +%s%3N)
    if [[ -z "$ft_args" ]]; then
      (cd "$dir" && "$@") > /dev/null 2>&1
    else
      # Both ft_args and $@ are intentionally unquoted: finetrace wants its
      # flags and the wrapped command as plain tokens, with no `--` separator
      # and no quoting around the binary path.
      # shellcheck disable=SC2086
      (cd "$dir" && "$FINETRACE" $ft_args $@) > /dev/null 2>&1
    fi
    end=$(date +%s%3N)
    elapsed_ms=$((end - start))
    total_ms=$((total_ms + elapsed_ms))
    printf "  run %2d: %d ms\n" "$i" "$elapsed_ms"
  done
  local avg_ms=$((total_ms / REPEATS))
  printf "  average: %d ms (%.3f s)\n\n" "$avg_ms" "$(echo "scale=3; $avg_ms / 1000" | bc)"
  RESULTS+=("$mode|$variant|$name|$avg_ms")
}

# Run a single benchmark across all trace variants.
# Usage: run_bench_all_variants <mode> <name> <dir> <cmd> [args...]
run_bench_all_variants() {
  local mode="$1" name="$2" dir="$3"
  shift 3
  local v label ft_args
  for v in "${TRACE_VARIANTS[@]}"; do
    label="${v%%|*}"
    ft_args="${v#*|}"
    run_bench "$mode" "$label" "$ft_args" "$name" "$dir" "$@"
  done
}

# Run the full benchmark suite for a given mode (cpu/gpu),
# iterating over all trace variants for every benchmark.
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

  run_bench_all_variants "$mode" "cl_gemm" \
    "$SAMPLES/cl_gemm/build" \
    ./cl_gemm "$mode" 512 512

  if [[ $mode == "gpu" ]]; then
    run_bench_all_variants "$mode" "ze_gemm" \
      "$SAMPLES/ze_gemm/build" \
      ./ze_gemm 512 512
  fi

  run_bench_all_variants "$mode" "bench_b+tree" \
    "$RODINIA/bench_b+tree" \
    ./b+tree.out file "$RODINIA_DATA_DIR/b+tree/mil.txt" command "$RODINIA_DATA_DIR/b+tree/command.txt" \
      -p "$cl_platform" -d "$cl_device"

  run_bench_all_variants "$mode" "bench_bfs" \
    "$RODINIA/bench_bfs" \
    ./bfs.out "$RODINIA_DATA_DIR/bfs/graph1MW_6.txt" \
      -p "$cl_platform" -d "$cl_device"

  run_bench_all_variants "$mode" "bench_gaussian" \
    "$RODINIA/bench_gaussian" \
    ./gaussian.out -s 2048 \
      -p "$cl_platform" -d "$cl_device"

  run_bench_all_variants "$mode" "bench_nw" \
    "$RODINIA/bench_nw" \
    ./nw.out 8192 10 ./nw.cl \
      -p "$cl_platform" -d "$cl_device"
}

# Print final summary tables: one per active mode, one column per trace variant.
print_summary() {
  echo "########################################"
  echo "# Summary (average ms over $REPEATS runs)"
  echo "########################################"
  echo ""

  # Collect a unique, ordered list of benchmark names from RESULTS.
  local names=()
  local entry name n seen rest
  for entry in "${RESULTS[@]}"; do
    # entry = mode|variant|name|avg
    rest="${entry#*|}"   # variant|name|avg
    rest="${rest#*|}"     # name|avg
    name="${rest%|*}"
    seen=0
    for n in "${names[@]:-}"; do
      [[ "$n" == "$name" ]] && { seen=1; break; }
    done
    [[ $seen -eq 0 ]] && names+=("$name")
  done

  # Variant labels in declared order.
  local variant_labels=()
  local v
  for v in "${TRACE_VARIANTS[@]}"; do
    variant_labels+=("${v%%|*}")
  done

  # One table per active mode.
  local modes=()
  [[ $RUN_CPU -eq 1 ]] && modes+=("cpu")
  [[ $RUN_GPU -eq 1 ]] && modes+=("gpu")

  local mode vlabel
  for mode in "${modes[@]}"; do
    echo "--- Mode: $mode ---"

    # Header row.
    printf "%-20s" "Benchmark"
    for vlabel in "${variant_labels[@]}"; do
      printf " | %16s" "$vlabel (ms)"
    done
    printf "\n"

    # Separator row.
    printf "%s" "--------------------"
    for vlabel in "${variant_labels[@]}"; do
      printf "+%s" "------------------"
    done
    printf "\n"

    # Data rows.
    for name in "${names[@]}"; do
      printf "%-20s" "$name"
      for vlabel in "${variant_labels[@]}"; do
        local val="-"
        local emode evar erest ename eavg
        for entry in "${RESULTS[@]}"; do
          emode="${entry%%|*}"
          erest="${entry#*|}"
          evar="${erest%%|*}"
          erest="${erest#*|}"
          ename="${erest%|*}"
          eavg="${erest##*|}"
          if [[ "$emode" == "$mode" && "$evar" == "$vlabel" && "$ename" == "$name" ]]; then
            val="$eavg"
            break
          fi
        done
        printf " | %16s" "$val"
      done
      printf "\n"
    done
    echo ""
  done
}

[[ $RUN_CPU -eq 1 ]] && run_suite "cpu"
[[ $RUN_GPU -eq 1 ]] && run_suite "gpu"

print_summary
