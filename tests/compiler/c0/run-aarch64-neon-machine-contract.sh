#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=${BUILD_DIR:-"$root/build/debug"}/tests/aarch64-neon-machine-contract
host_cc=${CC:-cc}
mkdir -p "$work"

"$host_cc" -std=c11 -Wall -Wextra -Werror \
    -I"$root/src" \
    "$root/tests/compiler/c0/aarch64_neon_machine_contract_test.c" \
    "$root/src/frontend/type.c" \
    "$root/src/core/core_type.c" \
    "$root/src/target/aarch64/frame_emit.c" \
    "$root/src/target/aarch64/neon_emit.c" \
    -o "$work/contract"
"$work/contract"

printf '%s\n' 'PASS AArch64 typed 128-bit Q-machine-contract load/store/xor; invalid types rejected; ARM runtime=NOT_TESTED'
