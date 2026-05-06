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

case "${1:-}" in
  --clean)
    clean_all
    ;;
  --help|-h)
    echo "Usage: $0 [--clean | --help]"
    echo ""
    echo "Without arguments: clean and build all samples."
    echo ""
    echo "Options:"
    echo "  --clean   Clean build artifacts only, do not build."
    echo "            cmake samples (cl_gemm, ze_gemm): removes their build/ directory."
    echo "            rodinia benchmarks: runs 'make clean' in each."
    echo "  --help    Show this help message."
    echo ""
    echo "Samples built:"
    echo "  samples/cl_gemm              (CMake)"
    echo "  samples/ze_gemm              (CMake)"
    echo "  samples/cl_rodinia_benchmarks/bench_b+tree"
    echo "  samples/cl_rodinia_benchmarks/bench_bfs"
    echo "  samples/cl_rodinia_benchmarks/bench_gaussian"
    echo "  samples/cl_rodinia_benchmarks/bench_nw"
    ;;
  "")
    clean_all
    build_all
    echo "-- All samples built successfully"
    ;;
  *)
    echo "Unknown option: $1"
    echo "Run '$0 --help' for usage."
    exit 1
    ;;
esac
