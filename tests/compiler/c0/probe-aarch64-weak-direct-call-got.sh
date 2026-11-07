#!/bin/sh
set -eu

probe_dir=$(mktemp -d)
trap 'rm -rf -- "$probe_dir"' EXIT HUP INT TERM
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/../../.." && pwd)
cd "$repo_root"

host_cc=${HOST_CC:-cc}
minic_cc=${MINIC_CC:-build/release/bin/minic-cc}
source=tests/compiler/c0/aarch64_weak_direct_call_got.c
provider=tests/compiler/c0/aarch64_weak_direct_call_got_provider.c

"$host_cc" -E -P -std=gnu11 "$source" -o "$probe_dir/input.i"
for target in x86_64 riscv32 riscv64 aarch64 loongarch64; do
    "$minic_cc" -S "$probe_dir/input.i" --target="$target-linux" \
        -fno-pie -o "$probe_dir/$target.s"
done

asm="$probe_dir/aarch64.s"
for symbol in minic_missing_weak_call minic_present_weak_call; do
    grep -Fq ".weak $symbol" "$asm"
    test "$(grep -Fc "adrp x16, :got:$symbol" "$asm")" -eq 1
    test "$(grep -Fc "ldr x16, [x16, :got_lo12:$symbol]" "$asm")" -eq 1
    if grep -Fq "  bl $symbol" "$asm"; then
        echo "ERROR: undefined weak direct CALL26 retained: $symbol" >&2
        exit 1
    fi
done
test "$(grep -Fc '  blr x16' "$asm")" -eq 2

# Negative boundary: strong undefined and TU-defined weak function are not
# undefined weak, and therefore must preserve their direct local BL calls.
for symbol in minic_strong_call minic_defined_weak_call; do
    grep -Fq "  bl $symbol" "$asm"
    if grep -Fq ":got:$symbol" "$asm"; then
        echo "ERROR: non-undefined-weak call unexpectedly routed to GOT: $symbol" >&2
        exit 1
    fi
done

# GNU owns preprocessing, assembler and linker. MiniC contributes only the C
# translation into .s. Compare real native outputs with independent GNU C.
"$host_cc" -std=gnu11 -O2 -fno-pie -no-pie \
    "$source" "$provider" -o "$probe_dir/gnu-runtime"
"$host_cc" -fno-pie -x assembler -c "$probe_dir/x86_64.s" \
    -o "$probe_dir/minic-owner.o"
"$host_cc" -std=gnu11 -O2 -fno-pie -c "$provider" \
    -o "$probe_dir/gnu-provider.o"
"$host_cc" -no-pie -Wl,-z,noexecstack \
    "$probe_dir/minic-owner.o" "$probe_dir/gnu-provider.o" \
    -o "$probe_dir/minic-runtime"
"$probe_dir/gnu-runtime"
"$probe_dir/minic-runtime"

echo 'AARCH64_WEAK_DIRECT_CALL_GOT=PASS five_ISA_S=5 undefined_weak=GOT+BLR strong_and_defined_weak=BL GNU_AS_LD_native_runtime=2'
