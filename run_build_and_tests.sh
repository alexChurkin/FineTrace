#!/usr/bin/env bash
set -euo pipefail

bash ./script_build_finetrace.sh
bash ./script_build_samples.sh
python3 ./script_run_samples.py

bash ./script_build_samples.sh --clean
# rm -rf ./build