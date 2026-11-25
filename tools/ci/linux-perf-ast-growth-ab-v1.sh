#!/usr/bin/env bash
# Retired historical experiment; no current compiler artifact is produced.
set -Eeuo pipefail
printf '%s\n' 'HISTORICAL_PERF_RETIRED: AST capacity growth A/B; PERF-033 rejected and old source anchors no longer match. Replay with its matching historical source checkout 59ad3648637eba4991c0fb8ce27f121cc852b4fa. Current-source qualification is not provided by this experiment.' >&2
exit 78
