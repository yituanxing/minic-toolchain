#!/usr/bin/env bash
# Prove actual AArch64 GNU Linux __ex_table fault-recovery metadata survives
# source qualification -> neutral Core roles -> real target .s -> GNU GAS.
# QEMU validates non-faulting loads against independent GNU-produced code.
# User-mode QEMU cannot exercise a kernel page-fault fixup: the relocatable
# 12-byte __ex_table records are separately checked by the target assembler.
set -Eeuo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
mini=${1:-${MINIC_CC:-"$root/build/release/bin/minic-cc"}}
cross=${AARCH64_CC:-aarch64-linux-gnu-gcc}
qemu=${QEMU_AARCH64:-qemu-aarch64}
objcopy=${AARCH64_OBJCOPY:-aarch64-linux-gnu-objcopy}
ref="$root/tests/compiler/c0/aarch64_linux_zeropad_extable.c"
caller="$root/tests/compiler/c0/aarch64_linux_zeropad_extable_caller.c"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail() { printf 'AARCH64_ZEROPAD=FAIL %s\n' "$*" >&2; exit 1; }

test -x "$mini" || fail 'MiniC compiler absent'
test -s "$ref" || fail 'GNU kernel template absent'
test -s "$caller" || fail 'GNU caller absent'
cc -E -P -std=gnu11 -x c "$ref" -o "$work/original.i"
"$mini" --target=aarch64-linux -S "$work/original.i" \
    -o "$work/minic.s" || fail 'original source contract refused'
test -s "$work/minic.s" || fail 'empty machine program'

# The originally source-defined GNU .irp is specialized to registers x9/x10.
# Literal register indices are valid because the emitter itself chose those
# machine registers. The target-side relocation operands must not disappear.
grep -Eq '^1: ldr x9, \[x11\]$' "$work/minic.s" ||
    fail 'hardware faulting LDR missing'
grep -Eq '^  \.pushsection __ex_table, "a"$' "$work/minic.s" ||
    fail '__ex_table section missing'
grep -Eq '^  \.long \(1b - \.\)$' "$work/minic.s" ||
    fail 'fault PC-relative relocation missing'
grep -Eq '^  \.long \(2b - \.\)$' "$work/minic.s" ||
    fail 'fixup PC-relative relocation missing'
grep -Eq '^  \.short 4$' "$work/minic.s" ||
    fail 'kernel fixup discriminator missing'
grep -Eq '^  \.short \(\(9 << 0\) \| \(10 << 5\)\)$' "$work/minic.s" ||
    fail 'result/address register indices differ from actual GPRs'
grep -Eq '^  str x9, \[x12\]$' "$work/minic.s" ||
    fail 'GNU ASM output result lost'

# Mutating ANY of the dangerous contracts must fail, without leaving
# misleading partial assembly. The template and constraints are both typed.
sed 's/\.short\\t\\t(4)/.short\\t\\t(5)/' "$ref" >"$work/wrong-fixup.c"
sed 's/"=&r" (ret)/"=r" (ret)/' "$ref" >"$work/wrong-output.c"
sed 's/"Q" (\*(unsigned long \*)addr)/"m" (*(unsigned long *)addr)/' \
    "$ref" >"$work/wrong-address.c"
sed 's/\.pushsection\\t__ex_table/.pushsection\\t__other_table/' \
    "$ref" >"$work/wrong-section.c"
for variant in wrong-fixup wrong-output wrong-address wrong-section; do
    cmp -s "$ref" "$work/$variant.c" && fail "test mutation ineffective: $variant"
    cc -E -P -std=gnu11 -x c "$work/$variant.c" -o "$work/$variant.i"
    if "$mini" --target=aarch64-linux -S "$work/$variant.i" \
        -o "$work/$variant.s" >"$work/$variant.out" 2>"$work/$variant.err"; then
        fail "unqualified $variant incorrectly accepted"
    fi
    test ! -e "$work/$variant.s" || fail "partial output for $variant"
done
if "$mini" --target=x86_64-linux -S "$work/original.i" \
    -o "$work/foreign.s" >"$work/foreign.out" 2>"$work/foreign.err"; then
    fail 'AArch64 template accepted by x86-64 ISA'
fi
test ! -e "$work/foreign.s" || fail 'foreign backend left partial output'
printf '%s\n' 'AARCH64_ZEROPAD_SOURCE=PASS kernel_template=1 extable_relocations=2 typed_roles=3 negative=5'

if ! command -v "$cross" >/dev/null 2>&1 ||
   ! command -v "$qemu" >/dev/null 2>&1 ||
   ! command -v "$objcopy" >/dev/null 2>&1; then
    if [[ ${MINIC_REQUIRE_AARCH64_ZEROPAD:-0} == 1 ]]; then
        fail 'required GNU AArch64 GAS/QEMU not available'
    fi
    printf '%s\n' 'AARCH64_ZEROPAD_RUNTIME=SKIP GNU AArch64/QEMU not installed'
    exit 0
fi

# The original GNU and generated MiniC programs must both be valid GNU GAS
# inputs. The resulting .o must retain a complete ELF section + relocations,
# not merely contain textual .pushsection that never reaches a binary.
"$cross" -O0 -c -x c "$ref" -o "$work/gnu.o"
"$cross" -c -x assembler "$work/minic.s" -o "$work/minic.o"
"$cross" -O0 -c "$caller" -o "$work/caller.o"
for product in gnu minic; do
    readelf -SW "$work/$product.o" | grep -q '__ex_table' ||
        fail "$product assembly lost ELF __ex_table"
    readelf -rW "$work/$product.o" | grep -q 'rela__ex_table' ||
        fail "$product assembly lost fault/fixup relocations"
    # The arm64 exception handler walks exactly 12-byte records. Section
    # existence alone would not catch a bogus/truncated or padded entry.
    "$objcopy" --dump-section "__ex_table=$work/$product.extable" \
        "$work/$product.o" || fail "$product cannot extract GNU ELF fixups"
    extable_bytes=$(wc -c <"$work/$product.extable")
    (( extable_bytes == 12 )) ||
        fail "$product emitted $extable_bytes bytes instead of one 12-byte entry"
    prel_count=$(readelf -rW "$work/$product.o" | \
        grep -c 'R_AARCH64_PREL32' || true)
    (( prel_count >= 2 )) ||
        fail "$product lost one of two PREL32 fault/fixup relocations"
    "$cross" -static "$work/caller.o" "$work/$product.o" \
        -o "$work/$product.elf" || fail "$product GNU static link failed"
    set +e
    timeout 45s "$qemu" "$work/$product.elf" \
        >"$work/$product.out" 2>"$work/$product.err"
    status=$?
    set -e
    (( status == 42 )) || fail "$product GNU/QEMU exit=$status"
done
cmp "$work/gnu.out" "$work/minic.out" || fail 'GNU/MiniC stdout differs'
cmp "$work/gnu.err" "$work/minic.err" || fail 'GNU/MiniC stderr differs'
printf '%s\n' 'AARCH64_ZEROPAD_RUNTIME=PASS GNU_GAS=2 GNU_static=2 QEMU=2 extable_ELF=2'
