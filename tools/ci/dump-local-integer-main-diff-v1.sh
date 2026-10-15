#!/usr/bin/env bash
set -Eeuo pipefail
python3 tools/ci/apply-local-integer-constants-v0.py
git diff -- src/core/core_lower.c src/core/core_lower_internal.h
