#!/bin/sh
# Linux 6.6.143 AArch64 KVM __build_check_flag: a specialized mask passes
# through __fls(), whose __builtin_clzl() reads a constant callee argument.
# This must remove the impossible compiletime-assert branch in all backends.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
mini=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host=${HOST_CC:-cc}
src="$root/tests/compiler/c0/a64_kvm_flag_guard.c"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

"$host" -E -P -std=gnu11 "$src" -o "$work/kvm.i"
"$host" -O2 -std=gnu11 -S "$src" -o "$work/gnu.s"
if grep -Fq '__kvm_flag_error' "$work/gnu.s"; then
    echo 'KVM_CONSTANT_GUARD=FAIL original GNU -O2 retained impossible guard' >&2
    exit 1
fi
for target in aarch64-linux x86_64-linux riscv64-linux loongarch64-linux; do
    "$mini" --target="$target" -S "$work/kvm.i" -o "$work/$target.s"
    if grep -Fq '__kvm_flag_error' "$work/$target.s"; then
        echo "KVM_CONSTANT_GUARD=FAIL target=$target retained false __fls(mask) guard" >&2
        exit 1
    fi
    if ! grep -Fq 'runtime_only' "$work/$target.s" ||
       ! grep -Fq 'zero_operand_guard' "$work/$target.s"; then
        echo "KVM_CONSTANT_GUARD=FAIL target=$target removed volatile/undefined runtime guard" >&2
        exit 1
    fi
done
echo 'KVM_CONSTANT_GUARD=PASS GNU_O2=1 architectures=4 inline_clz_local_fact=1 runtime_volatile_preserved=4 zero_ub_fail_closed=4'
