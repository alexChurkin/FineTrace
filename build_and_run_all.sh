#!/usr/bin/env bash
set -euo pipefail

./script_build_finetrace.sh
./script_build_samples.sh
./script_run_samples.sh

./script_build_samples.sh --clean
