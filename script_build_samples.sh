#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SAMPLES="$ROOT_DIR/samples"
RODINIA="$SAMPLES/cl_rodinia_benchmarks"

clean_all() {
  echo "-- Cleaning cmake samples"
  rm -rf "$SAMPLES/cl_gemm/build" "$SAMPLES/ze_gemm/build"

  echo "-- Cleaning rodinia benchmarks"
  make -C "$RODINIA/b+tree" clean
  make -C "$RODINIA/bfs"    clean
  make -C "$RODINIA/gaussian" clean
  make -C "$RODINIA/nw"     clean
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

  echo "-- Building b+tree"
  make -C "$RODINIA/b+tree" KERNEL_DIM="-DRD_WG_SIZE_0=256 -DRD_WG_SIZE_1=256"

  echo "-- Building bfs"
  make -C "$RODINIA/bfs" release

  echo "-- Building gaussian"
  make -C "$RODINIA/gaussian" KERNEL_DIM="-DRD_WG_SIZE_0=16 -DRD_WG_SIZE_1_0=16 -DRD_WG_SIZE_1_1=16"

  echo "-- Building nw"
  make -C "$RODINIA/nw" KERNEL_DIM="-DRD_WG_SIZE_0=16"
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
    echo "Samples available:"
    echo "  samples/cl_gemm              (CMake)"
    echo "  samples/ze_gemm              (CMake)"
    echo "  samples/cl_rodinia_benchmarks/b+tree"
    echo "  samples/cl_rodinia_benchmarks/bfs"
    echo "  samples/cl_rodinia_benchmarks/gaussian"
    echo "  samples/cl_rodinia_benchmarks/nw"
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
