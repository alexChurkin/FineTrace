#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SAMPLES="$ROOT_DIR/samples"
RODINIA="$SAMPLES/cl_rodinia_benchmarks"

clean_all() {
  echo "-- Cleaning cmake samples"
  rm -rf "$SAMPLES/cl_gemm/build" "$SAMPLES/ze_gemm/build"

  echo "-- Cleaning rodinia benchmarks"
  make -C "$RODINIA/bench_b+tree" clean
  make -C "$RODINIA/bench_bfs"    clean
  make -C "$RODINIA/bench_gaussian" clean
  make -C "$RODINIA/bench_nw"     clean
}

build_all() {
  echo "-- Building cl_gemm"
  mkdir -p "$SAMPLES/cl_gemm/build"
  cmake -S "$SAMPLES/cl_gemm" -B "$SAMPLES/cl_gemm/build" -DCMAKE_BUILD_TYPE=Release
  cmake --build "$SAMPLES/cl_gemm/build"

  echo "-- Building ze_gemm"
  mkdir -p "$SAMPLES/ze_gemm/build"
  cmake -S "$SAMPLES/ze_gemm" -B "$SAMPLES/ze_gemm/build" -DCMAKE_BUILD_TYPE=Release
  cmake --build "$SAMPLES/ze_gemm/build"

  echo "-- Building bench_b+tree"
  make -C "$RODINIA/bench_b+tree" KERNEL_DIM="-DRD_WG_SIZE_0=256 -DRD_WG_SIZE_1=256"

  echo "-- Building bench_bfs"
  make -C "$RODINIA/bench_bfs" release

  echo "-- Building bench_gaussian"
  make -C "$RODINIA/bench_gaussian" KERNEL_DIM="-DRD_WG_SIZE_0=16 -DRD_WG_SIZE_1_0=16 -DRD_WG_SIZE_1_1=16"

  echo "-- Building bench_nw"
  make -C "$RODINIA/bench_nw" KERNEL_DIM="-DRD_WG_SIZE_0=16"
}

if [[ "${1:-}" == "--clean" ]]; then
  clean_all
else
  clean_all
  build_all
  echo "-- All samples built successfully"
fi
