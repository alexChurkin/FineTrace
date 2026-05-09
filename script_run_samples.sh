#!/usr/bin/env bash
# Run OpenCL/Level Zero benchmarks under several finetrace configurations and
# print average wall-clock time per (mode, variant, benchmark).
#
# Variants measured:
#   CPU mode: clean | host-timing | call-logging | host+call
#   GPU mode: clean | host-timing | call-logging | host+call | metrics | all
#
# "metrics"  = --aggregation (ComputeBasic, time-based HW counters)
# "all"      = --host-timing --call-logging --aggregation
#
# Device mapping:
#   OpenCL platform 0, device 0: Intel Arc Graphics      (GPU)
#   OpenCL platform 1, device 0: Intel Core Ultra 5 125H (CPU)
#
# Targets bash 3.2+ (works with the system bash on macOS).
set -euo pipefail

# ---------------------------------------------------------------------------
# Paths and defaults
# ---------------------------------------------------------------------------
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SAMPLES="$ROOT_DIR/samples"
RODINIA="$SAMPLES/cl_rodinia_benchmarks"

FINETRACE="${FINETRACE:-$ROOT_DIR/build/finetrace}"
RODINIA_DATA_DIR="${RODINIA_DATA_DIR:-$RODINIA/data}"

REPEATS=2
RUN_CPU=0
RUN_GPU=0

# ---------------------------------------------------------------------------
# Variant definitions
# ---------------------------------------------------------------------------
# Base variants — run for every mode.
BASE_LABELS=( "clean"  "host-timing"    "call-logging"   "host+call" )
BASE_ARGS=(   ""       "--host-timing"  "--call-logging"  "--host-timing --call-logging" )

# Metric variants — GPU only (requires Level Zero device).
METRIC_LABELS=( "metrics"       "all" )
METRIC_ARGS=(   "--aggregation" "--host-timing --call-logging --aggregation" )

# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------
usage() {
  cat <<EOF
Usage: $0 [--cpu] [--gpu] [-n N] [--help]

Run benchmark samples with per-run timing and average.

  Variants (CPU):  ${BASE_LABELS[*]}
  Variants (GPU):  ${BASE_LABELS[*]} ${METRIC_LABELS[*]}

  --cpu    Run CPU-only suite (Intel Core Ultra 5 125H, OpenCL 1:0).
  --gpu    Run GPU suite (Intel Arc Graphics, OpenCL 0:0 + ze_gemm).
  (none)   Run both CPU and GPU (default).
  -n N     Repeats per benchmark (default: $REPEATS).
  --help   Show this help.

Environment:
  FINETRACE         Path to finetrace binary (default: $FINETRACE).
  RODINIA_DATA_DIR  Path to Rodinia data directory.

CPU benchmarks: cl_gemm, bench_b+tree, bench_bfs, bench_gaussian, bench_nw
GPU benchmarks: CPU benchmarks + ze_gemm
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --cpu)     RUN_CPU=1; shift ;;
    --gpu)     RUN_GPU=1; shift ;;
    -n)        REPEATS="$2"; shift 2 ;;
    --help|-h) usage; exit 0 ;;
    *)         echo "Unknown option: $1" >&2; usage >&2; exit 1 ;;
  esac
done

if [[ $RUN_CPU -eq 0 && $RUN_GPU -eq 0 ]]; then
  RUN_CPU=1
  RUN_GPU=1
fi

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
log()      { printf '%s\n' "$*"; }
log_head() { printf '\n=== %s ===\n' "$*"; }

now_us() {
  if [[ -n "${EPOCHREALTIME:-}" ]]; then
    printf '%s\n' "${EPOCHREALTIME/./}"
  elif [[ "$(date +%s%6N 2>/dev/null)" =~ ^[0-9]+$ ]]; then
    date +%s%6N
  else
    python3 -c 'import time; print(int(time.time()*1000000))'
  fi
}

fmt_s() { awk "BEGIN{printf \"%.5f s\", $1/1000000}"; }

RODINIA_DATA_DIR="$(cd "$RODINIA_DATA_DIR" && pwd)"

if [[ ! -x "$FINETRACE" ]]; then
  log "Warning: finetrace not found at: $FINETRACE"
  log "         Non-clean variants will fail. Set FINETRACE=/path/to/finetrace."
  log ""
fi

clinfo -l || true

# ---------------------------------------------------------------------------
# Result storage
#
# Each entry: "<mode>\t<variant>\t<bench>\t<avg_us>"
# ---------------------------------------------------------------------------
RESULTS=()
BENCH_NAMES=()

remember_bench() {
  local name="$1" n
  for n in "${BENCH_NAMES[@]:-}"; do
    [[ "$n" == "$name" ]] && return 0
  done
  BENCH_NAMES+=("$name")
}

# ---------------------------------------------------------------------------
# Core runner
#
# Usage: run_one <mode> <label> <ft_args> <bench_name> <dir> <cmd> [args...]
# ---------------------------------------------------------------------------
run_one() {
  local mode="$1" label="$2" ft_args="$3" name="$4" dir="$5"
  shift 5

  local -a cmd
  if [[ -z "$ft_args" ]]; then
    cmd=( "$@" )
  else
    # shellcheck disable=SC2206
    cmd=( "$FINETRACE" $ft_args "$@" )
  fi

  log_head "$name [$mode / $label] ($REPEATS runs)"
  printf '  command: %s\n' "${cmd[*]}"
  local total_us=0 i start end elapsed
  for ((i = 1; i <= REPEATS; i++)); do
    start=$(now_us)
    ( cd "$dir" && "${cmd[@]}" ) >/dev/null 2>&1
    end=$(now_us)
    elapsed=$(( end - start ))
    total_us=$(( total_us + elapsed ))
    printf '  run %2d: %.5f s\n' "$i" "$(awk "BEGIN{printf \"%.5f\", $elapsed/1000000}")"
  done

  local avg_us=$(( total_us / REPEATS ))
  printf '  average: %s\n' "$(fmt_s "$avg_us")"

  RESULTS+=( "$(printf '%s\t%s\t%s\t%s' "$mode" "$label" "$name" "$avg_us")" )
  remember_bench "$name"
}

# Run a benchmark through all variants for the given mode.
# Usage: run_all_variants <mode> <bench_name> <dir> <cmd> [args...]
run_all_variants() {
  local mode="$1" name="$2" dir="$3"
  shift 3

  # Build variant list for this mode.
  local -a labels=("${BASE_LABELS[@]}")
  local -a args=("${BASE_ARGS[@]}")
  if [[ "$mode" == "gpu" ]]; then
    labels+=("${METRIC_LABELS[@]}")
    args+=("${METRIC_ARGS[@]}")
  fi

  local i
  for ((i = 0; i < ${#labels[@]}; i++)); do
    run_one "$mode" "${labels[$i]}" "${args[$i]}" "$name" "$dir" "$@"
  done
}

# ---------------------------------------------------------------------------
# Suite
# ---------------------------------------------------------------------------
run_suite() {
  local mode="$1"
  local plat dev
  if [[ "$mode" == "gpu" ]]; then plat=0; dev=0; else plat=1; dev=0; fi

  log ""
  log "########################################"
  log "# Running suite on: $mode"
  log "########################################"

  run_all_variants "$mode" "cl_gemm" \
    "$SAMPLES/cl_gemm/build" \
    ./cl_gemm "$mode" 512 128

  if [[ "$mode" == "gpu" ]]; then
    run_all_variants "$mode" "ze_gemm" \
      "$SAMPLES/ze_gemm/build" \
      ./ze_gemm 512 128
  fi

  run_all_variants "$mode" "bench_b+tree" \
    "$RODINIA/bench_b+tree" \
    ./b+tree.out file "$RODINIA_DATA_DIR/b+tree/mil.txt" \
                 command "$RODINIA_DATA_DIR/b+tree/command.txt" \
                 -p "$plat" -d "$dev"

  run_all_variants "$mode" "bench_bfs" \
    "$RODINIA/bench_bfs" \
    ./bfs.out "$RODINIA_DATA_DIR/bfs/graph1MW_6.txt" \
              -p "$plat" -d "$dev"

  run_all_variants "$mode" "bench_gaussian" \
    "$RODINIA/bench_gaussian" \
    ./gaussian.out -s 2048 -p "$plat" -d "$dev"

  run_all_variants "$mode" "bench_nw" \
    "$RODINIA/bench_nw" \
    ./nw.out 8192 10 ./nw.cl -p "$plat" -d "$dev"
}

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
lookup_avg() {
  local want_mode="$1" want_var="$2" want_name="$3"
  local entry mode variant bench avg
  for entry in "${RESULTS[@]:-}"; do
    IFS=$'\t' read -r mode variant bench avg <<<"$entry"
    if [[ "$mode" == "$want_mode" && "$variant" == "$want_var" && "$bench" == "$want_name" ]]; then
      printf '%s' "$avg"
      return 0
    fi
  done
  printf '%s' "-"
}

print_summary() {
  log ""
  log "########################################"
  log "# Summary (average wall-clock time in seconds over $REPEATS runs)"
  log "########################################"

  local modes=()
  [[ $RUN_CPU -eq 1 ]] && modes+=("cpu")
  [[ $RUN_GPU -eq 1 ]] && modes+=("gpu")

  local name_w=20 col_w=17
  local mode name vlabel val sep_name sep_col
  printf -v sep_name '%*s' "$name_w" ''; sep_name="${sep_name// /-}"
  printf -v sep_col  '%*s' "$col_w"  ''; sep_col="${sep_col// /-}"

  for mode in "${modes[@]}"; do
    # Determine which variants were actually run for this mode.
    local -a mode_variants=("${BASE_LABELS[@]}")
    [[ "$mode" == "gpu" ]] && mode_variants+=("${METRIC_LABELS[@]}")

    log ""
    log "--- Mode: $mode ---"

    # Header.
    printf '%-*s' "$name_w" "Benchmark"
    for vlabel in "${mode_variants[@]}"; do
      printf ' | %*s' "$col_w" "$vlabel (s)"
    done
    printf '\n'

    # Separator.
    printf '%s' "$sep_name"
    for vlabel in "${mode_variants[@]}"; do
      printf -- '-+-%s' "$sep_col"
    done
    printf -- '-\n'

    # Rows.
    for name in "${BENCH_NAMES[@]:-}"; do
      printf '%-*s' "$name_w" "$name"
      for vlabel in "${mode_variants[@]}"; do
        val="$(lookup_avg "$mode" "$vlabel" "$name")"
        [[ "$val" != "-" ]] && val="$(awk "BEGIN{printf \"%.5f\", $val/1000000}")"
        printf ' | %*s' "$col_w" "$val"
      done
      printf '\n'
    done
  done
  log ""
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
[[ $RUN_CPU -eq 1 ]] && run_suite "cpu"
[[ $RUN_GPU -eq 1 ]] && run_suite "gpu"

print_summary
