#!/usr/bin/env bash
set -euo pipefail
bash ./validation/cross_build.sh transition-reference strict-reference
bash ./validation/cross_build.sh transition-candidate diagnostic-reference
