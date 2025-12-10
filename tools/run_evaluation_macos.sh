#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

printf "[1/3] Building binaries...\n"
bash "${REPO_ROOT}/build.sh"

printf "\n[2/3] Generating evaluation fixtures...\n"
bash "${REPO_ROOT}/tools/gen_dtmf_tests.sh"

printf "\n[3/3] Running decoder evaluation...\n"
python3 "${REPO_ROOT}/tools/evaluate_dtmf.py"
