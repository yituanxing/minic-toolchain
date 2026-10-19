#!/bin/sh
# x86-64 cJSON gate. Reuse frozen upstream, existing runtime harness and
# original upstream test.c GCC/MiniC byte-exact differential.
set -eu
: "${MINIC_CC:?set MINIC_CC to the built minic-cc}"
: "${HOST_CC:=cc}"
case "$(uname -m)" in
    x86_64|amd64) ;;
    *) echo "SKIP cJSON x86-64: native x86-64 host required"; exit 0 ;;
esac

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
: "${BUILD_DIR:=$root/build/debug}"
export BUILD_DIR

MINIC_CC="$MINIC_CC" HOST_CC="$HOST_CC" \
    sh "$root/tests/external/cjson/probe-x86_64-frontier.sh"

RISCV_CC="$HOST_CC" QEMU_RISCV64=env BUILD_DIR="$BUILD_DIR" \
    sh "$root/tests/external/cjson/final_differential.sh"
echo "PASS x86_64 cJSON 1.7.19 runtime and upstream test.c GCC differential"
