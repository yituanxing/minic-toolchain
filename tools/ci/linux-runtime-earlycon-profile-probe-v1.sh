#!/usr/bin/env bash
# Historical patch-prefix diagnostic; use the original checkout for replay.
set -Eeuo pipefail
printf '%s\n' 'HISTORICAL_PROFILE_RETIRED: pre-split Core/RV64 source overlay is incompatible with current source. Replay the original experiment in its own historical checkout eb583f9dd9bf9cb2ebf530a8579930311b8539d9. This is not a compiler or runtime PASS.' >&2
exit 78
