#!/usr/bin/env bash
# Clean generated output files from the repository root.
#
# Default: removes metric data files, log files, and report files.
# --builds: also removes all build directories (finetrace + samples).
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SAMPLES="$ROOT_DIR/samples"
RODINIA="$SAMPLES/cl_rodinia_benchmarks"

CLEAN_BUILDS=0

usage() {
  cat <<EOF
Usage: $0 [--builds] [--help]

Remove generated output files from the repository (all subdirectories).

  (default)  Remove metric data files, log files, and Excel reports.
  --builds   Also remove all build directories (finetrace + all samples).
  --help     Show this help.

Files removed by default (searched recursively):
  data.*.raw / data.*.bin / data.*.query   Metric collection intermediate files
  result.*.bin                             Metric result files
  finetrace_run_*.log                      Benchmark run logs
  finetrace_overhead_stat.xlsx             Excel overhead report
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --builds)   CLEAN_BUILDS=1; shift ;;
    --help|-h)  usage; exit 0 ;;
    *)          echo "Unknown option: $1" >&2; usage >&2; exit 1 ;;
  esac
done

# ---------------------------------------------------------------------------
# Output artifacts (always cleaned)
# ---------------------------------------------------------------------------
echo "-- Removing metric output files"
find "$ROOT_DIR" ! -path "*/.git/*" \
  \( -name "data.*.raw" -o -name "data.*.bin" -o -name "data.*.query" -o -name "result.*.bin" \) \
  -print -delete

echo "-- Removing log files"
find "$ROOT_DIR" ! -path "*/.git/*" -name "finetrace_run_*.log" -print -delete

# echo "-- Removing Excel report"
# find "$ROOT_DIR" -maxdepth 1 -name "finetrace_overhead_stat.xlsx" -print -delete

# ---------------------------------------------------------------------------
# Build directories (opt-in)
# ---------------------------------------------------------------------------
if [[ $CLEAN_BUILDS -eq 1 ]]; then
  # echo "-- Removing finetrace build"
  # rm -rf "$ROOT_DIR/build"

  echo "-- Removing sample builds"
  rm -rf "$SAMPLES/cl_gemm/build" "$SAMPLES/ze_gemm/build"

  echo "-- Cleaning Rodinia benchmarks"
  make -C "$RODINIA/bench_b+tree"  clean 2>/dev/null || true
  make -C "$RODINIA/bench_bfs"     clean 2>/dev/null || true
  make -C "$RODINIA/bench_gaussian" clean 2>/dev/null || true
  make -C "$RODINIA/bench_nw"      clean 2>/dev/null || true
fi

echo "-- Done"
