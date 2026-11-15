#!/usr/bin/env bash
set -euo pipefail
bash ./validation/cross_build.sh texture-diag active
bash ./validation/cross_build.sh reference-v3 diagnostic-reference
