#!/usr/bin/env bash
set -euo pipefail

BUILD_TYPE="Release"
CLEAN=0
INSTALL=0
JOBS=$(nproc 2>/dev/null || echo 4)

usage() {
  echo "Usage: $0 [options]"
  echo "  -d, --debug      Build in Debug mode (default: Release)"
  echo "  -c, --clean      Remove build directory before building"
  echo "  -i, --install    Run 'make install' after build"
  echo "  -j N             Number of parallel jobs (default: $JOBS)"
  echo "  -h, --help       Show this help"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -d|--debug)   BUILD_TYPE="Debug"; shift ;;
    -c|--clean)   CLEAN=1; shift ;;
    -i|--install) INSTALL=1; shift ;;
    -j)           JOBS="$2"; shift 2 ;;
    -h|--help)    usage; exit 0 ;;
    *)            echo "Unknown option: $1"; usage; exit 1 ;;
  esac
done

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$ROOT_DIR/build"

if [[ $CLEAN -eq 1 && -d "$BUILD_DIR" ]]; then
  echo "-- Cleaning $BUILD_DIR"
  rm -rf "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR"

echo "-- Build type : $BUILD_TYPE"
echo "-- Parallel jobs: $JOBS"
echo "-- Build dir  : $BUILD_DIR"

START=$(date +%s)

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE"

cmake --build "$BUILD_DIR" -- -j"$JOBS"

if [[ $INSTALL -eq 1 ]]; then
  cmake --install "$BUILD_DIR"
fi

END=$(date +%s)
echo "-- Done in $((END - START))s"
