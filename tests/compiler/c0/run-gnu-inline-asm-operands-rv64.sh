#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
sudo apt-get install -y --no-install-recommends libc6-dev-riscv64-cross >/dev/null
exec sh "$root/tests/compiler/c0/run-gnu-inline-asm-operands-rv64-staging-canonical.sh"
