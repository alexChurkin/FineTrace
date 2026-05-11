#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "=== Step 1: Clean output artifacts ==="
bash "$ROOT_DIR/script_clean.sh"

echo ""
echo "=== Step 2: Build finetrace ==="
bash "$ROOT_DIR/script_build_finetrace.sh"

echo ""
echo "=== Step 3: Build samples ==="
bash "$ROOT_DIR/script_build_samples.sh"

echo ""
echo "=== Step 4: Run benchmarks and generate report ==="
python3 "$ROOT_DIR/script_run_samples.py"
