#!/bin/sh
set -eu

probe_dir=$(mktemp -d)
trap 'rm -rf -- "$probe_dir"' EXIT HUP INT TERM
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/../../.." && pwd)
cd "$repo_root"

host_cc=${HOST_CC:-cc}
minic_cc=${MINIC_CC:-build/release/bin/minic-cc}

# Link the exact backend contract under function-section GC: other AArch64
# code emission helpers in the same translation unit are irrelevant here.
"$host_cc" -std=c11 -O2 -Wall -Wextra -Werror \
    -ffunction-sections -fdata-sections -Isrc -Iinclude \
    tests/compiler/c0/aarch64_clone_safe_asm_policy.c \
    src/target/aarch64/asm_contract.c -Wl,--gc-sections \
    -o "$probe_dir/asm-policy"
"$probe_dir/asm-policy"

source=tests/compiler/c0/aarch64_fixmap_opaque_asm_clone.c
"$host_cc" -std=gnu11 -O2 -S "$source" -o "$probe_dir/gnu.s"
if grep -Fq minic_bad_fixmap "$probe_dir/gnu.s"; then
    echo 'ERROR: GNU -O2 failed to discharge fixmap guard' >&2
    exit 1
fi

"$host_cc" -std=gnu11 -E -P "$source" -o "$probe_dir/fixmap.i"
"$minic_cc" -S "$probe_dir/fixmap.i" --target=aarch64-linux \
    -o "$probe_dir/minic.s"
if grep -Fq minic_bad_fixmap "$probe_dir/minic.s"; then
    echo 'ERROR: MiniC static clone retained the fixmap guard' >&2
    exit 1
fi
if ! grep -Fq __minic_inline_spec_ "$probe_dir/minic.s"; then
    echo 'ERROR: MiniC did not specialize the ASM-bearing static wrapper' >&2
    exit 1
fi
if ! grep -Fq '14470: .long 14471f - .' "$probe_dir/minic.s"; then
    echo 'ERROR: MiniC failed to retain the closed local-label BUG table' >&2
    exit 1
fi

echo 'AARCH64_FIXMAP_OPAQUE_ASM_CLONE=PASS policy=positive+negative GNU_O2=guard_elided MiniC_AArch64=guard_elided+BUG_table_retained'
