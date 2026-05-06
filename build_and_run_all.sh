#!/usr/bin/env bash
set -euo pipefail

bash ./script_build_finetrace.sh
bash ./script_build_samples.sh
bash ./script_run_samples.sh

bash ./script_build_samples.sh --clean
# rm -rf ./build