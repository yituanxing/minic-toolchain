#!/usr/bin/env bash
# Kbuild command-owner and MiniAS production-assembler trace must agree.
# This intentionally exercises both valid manifests and fail-closed bypasses
# before expensive Linux build/run gates are attempted.
set -Eeuo pipefail

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
python3 -m unittest tests.external.linux.test_minias_as_provenance_check -v
python3 -m unittest discover -s tests/external/linux -p 'test_minias_aarch64_object_oracle.py' -v
echo 'MINIAS_AS_PROVENANCE_SELFTEST=PASS'
