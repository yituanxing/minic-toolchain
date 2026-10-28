#!/usr/bin/env bash
# Actual AAPCS64 x8 indirect record return, GNU caller <-> MiniC callee,
# MiniC caller <-> GNU callee, and nested-call x8 lifetime. Source-only mode
# remains useful on workstations without AArch64 GNU GAS/QEMU, but MUST be
# marked SKIP for runtime and MUST fail when MINIC_REQUIRE_AARCH64_SRET=1.
set -Eeuo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
minic_cc=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host_cc=${HOST_CC:-cc}
target_cc=${AARCH64_CC:-aarch64-linux-gnu-gcc}
target_qemu=${QEMU_AARCH64:-qemu-aarch64}
mini_source="$root/tests/compiler/c0/aarch64_indirect_record_return_minic.c"
gnu_source="$root/tests/compiler/c0/aarch64_indirect_record_return_gnu.c"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

fail() { printf '%s\n' "AAPCS64_SRET=FAIL $*" >&2; exit 1; }
require_contains() {
    if ! grep -Eq "$1" "$2"; then
        fail "missing expected machine proof: $1 in $2"
    fi
}
require_rejected() {
    local target=$1 source=$2 name=$3
    if "$minic_cc" --target="$target" -S "$source" \
        -o "$work/$name.s" >"$work/$name.stdout" 2>"$work/$name.stderr"; then
        fail "unsafe machine ABI admitted: target=$target case=$name"
    fi
    if [[ -e "$work/$name.s" ]]; then
        fail "failed machine transaction left partial assembly: $name"
    fi
    if ! grep -Eq 'unsupported|not yet|failed|Core IR|qualified' \
        "$work/$name.stderr"; then
        fail "rejection lacked a useful diagnostic: $name"
    fi
}

test -x "$minic_cc" || fail "missing MiniC compiler: $minic_cc"
test -s "$mini_source" || fail 'missing MiniC fixture'
test -s "$gnu_source" || fail 'missing GNU caller and callee'

# The frontend accepts original preprocessed C. The host is used only for
# C preprocessing in source-only mode, NEVER to certify foreign machine ABI.
"$host_cc" -E -P -std=gnu11 -x c "$mini_source" -o "$work/mini.i"
"$minic_cc" --target=aarch64-linux -S "$work/mini.i" \
    -o "$work/aarch64.s" || fail 'AArch64 indirect-result compilation'
test -s "$work/aarch64.s" || fail 'AArch64 assembly empty'

for name in minic_three minic_four minic_eighteen minic_nested_three \
            minic_nested_eighteen minic_branch_three minic_ten_arguments \
            minic_nested_record minic_seventeen minic_sixty_four \
            minic_2048 minic_nested_2048 minic_two minic_dynamic_three \
            minic_roundtrip_eighteen minic_record_argument_and_result \
            minic_two_gnu_returns minic_direct_return_gnu_result minic_bits; do
    require_contains "^${name}:$" "$work/aarch64.s"
done

# The incoming x8 pointer must be stored in a private frame slot before
# GNU can clobber it, then reloaded to COPY the actual full object bytes.
# Counted copy has one branch per record return, not O(record bytes) text.
require_contains '^  str x8, \[x11\]$' "$work/aarch64.s"
require_contains '^  ldr x10, \[x10\]$' "$work/aarch64.s"
require_contains '^  add x8, sp, x8$' "$work/aarch64.s"
require_contains '^  bl gnu_make_three$' "$work/aarch64.s"
require_contains '^  bl gnu_make_eighteen$' "$work/aarch64.s"
require_contains '^  bl gnu_tap$' "$work/aarch64.s"
require_contains '^  ldrb w9, \[x11\], #1$' "$work/aarch64.s"
require_contains '^  strb w9, \[x10\], #1$' "$work/aarch64.s"
require_contains '^  b.ne 1b$' "$work/aarch64.s"
require_contains '^  movz x12, #144$' "$work/aarch64.s"
require_contains '^  movz x12, #40$' "$work/aarch64.s"
require_contains '^  movz x12, #24$' "$work/aarch64.s"
require_contains '^  movz x12, #32$' "$work/aarch64.s"
require_contains '^  movz x12, #17$' "$work/aarch64.s"
require_contains '^  movz x12, #64$' "$work/aarch64.s"
require_contains '^  movz x12, #2048$' "$work/aarch64.s"
# Alloca moves the stack pointer; the return-object and saved x8 slot must
# still be addressed relative to the stable AAPCS64 callee-saved x19.
require_contains '^  add x11, x19, x11$' "$work/aarch64.s"
require_contains '^  add x10, x19, x10$' "$work/aarch64.s"

# LoongArch now has its OWN independently qualified LP64D return policy.
# The same source is legal there, but its hidden result MUST use $a0, never
# copy the AAPCS64 x8 machine sequence. Runtime evidence belongs to the
# separate LoongArch GNU/GAS/QEMU differential probe.
"$minic_cc" --target=loongarch64-linux -S "$work/mini.i" \
    -o "$work/loong.s" || fail 'LoongArch independent LP64D result ABI'
require_contains '^  st\.d \$a0, ' "$work/loong.s"
require_contains '^  move \$t2, \$a0$' "$work/loong.s"

# A 24-byte homogeneous floating-point aggregate is AAPCS64 HFA, not the
# x8 memory-result class. This feature must never turn an unimplemented
# v0..v3 result into an integer/memory ABI by accident.
cat >"$work/hfa.c" <<'SRC'
struct Hfa { double value[3]; };
struct Hfa unsupported_hfa(double x) {
    struct Hfa out;
    out.value[0] = x;
    out.value[1] = x + 1.0;
    out.value[2] = x + 2.0;
    return out;
}
SRC
"$host_cc" -E -P -x c "$work/hfa.c" -o "$work/hfa.i"
require_rejected aarch64-linux "$work/hfa.i" aarch64-hfa-not-sret

# A nonhomogeneous FP+integer aggregate also needs its own ABI classifier;
# accepting it as an integer-only memory return would silently change the
# semantics of real GNU AAPCS64 calls. This deliberately remains rejected.
cat >"$work/mixed-fp.c" <<'SRC'
struct Mixed { double fraction; unsigned long count[2]; };
struct Mixed mixed_return(unsigned long value) {
    struct Mixed result;
    result.fraction = 1.25;
    result.count[0] = value;
    result.count[1] = value + 1UL;
    return result;
}
SRC
"$host_cc" -E -P -x c "$work/mixed-fp.c" -o "$work/mixed-fp.i"
require_rejected aarch64-linux "$work/mixed-fp.i" aarch64-mixed-fp-not-qualified

# Per-target ownership also bounds the callee's result copy. No silent
# truncation of an over-capacity aggregate, no partial ELF output.
cat >"$work/overlimit.c" <<'SRC'
struct OverLimit { unsigned char bytes[2049]; };
struct OverLimit too_large(void) {
    struct OverLimit value;
    value.bytes[0] = 2;
    value.bytes[2048] = 9;
    return value;
}
SRC
"$host_cc" -E -P -x c "$work/overlimit.c" -o "$work/overlimit.i"
require_rejected aarch64-linux "$work/overlimit.i" aarch64-overlimit

# Only an independently validated direct result_object is implemented. An
# indirect-call result needs a separate caller-side Core result contract.
cat >"$work/indirect.c" <<'SRC'
struct Large { unsigned long words[3]; };
extern struct Large (*factory)(unsigned long);
struct Large call_factory(unsigned long x) {
    return factory(x);
}
SRC
"$host_cc" -E -P -x c "$work/indirect.c" -o "$work/indirect.i"
require_rejected aarch64-linux "$work/indirect.i" aarch64-indirect-callee

printf '%s\n' 'AAPCS64_SRET_SOURCE=PASS direct_return=19 nested_call=4 independent_x8=1 LP64D_a0=1 return_blocks=2 ABI_negative=4'

if ! command -v "$target_cc" >/dev/null 2>&1 ||
   ! command -v "$target_qemu" >/dev/null 2>&1; then
    if [[ ${MINIC_REQUIRE_AARCH64_SRET:-0} == 1 ]]; then
        fail 'required AArch64 GNU GAS/QEMU toolchain absent'
    fi
    printf '%s\n' 'AAPCS64_SRET_RUNTIME=SKIP AArch64 GNU/QEMU unavailable'
    exit 0
fi

# Re-preprocess with the exact same GNU target compiler that will assemble
# and statically link the test, avoiding host model/long assumptions.
"$target_cc" -E -P -std=gnu11 -x c "$mini_source" -o "$work/aarch64.i"
"$minic_cc" --target=aarch64-linux -S "$work/aarch64.i" \
    -o "$work/aarch64.s" || fail 'target-preprocessed MiniC compile'
"$target_cc" -std=gnu11 -O2 -static "$mini_source" "$gnu_source" \
    -o "$work/gnu.elf" || fail 'GNU reference link'
"$target_cc" -std=gnu11 -O2 -c "$gnu_source" \
    -o "$work/gnu.o" || fail 'GNU caller/callee object'
"$target_cc" -static -x assembler "$work/aarch64.s" -x none \
    "$work/gnu.o" -o "$work/mini.elf" || fail 'MiniC + GNU GAS/static link'

set +e
timeout 45s "$target_qemu" "$work/gnu.elf" >"$work/gnu.out" 2>"$work/gnu.err"
gnu_status=$?
timeout 45s "$target_qemu" "$work/mini.elf" >"$work/mini.out" 2>"$work/mini.err"
mini_status=$?
set -e
if [[ $gnu_status -ne 42 || $mini_status -ne 42 ]]; then
    printf '%s\n' "GNU return=$gnu_status MiniC return=$mini_status" >&2
    sed -n '1,20p' "$work/gnu.err" >&2
    sed -n '1,20p' "$work/mini.err" >&2
    fail 'real GNU/QEMU ABI output mismatch'
fi
cmp "$work/gnu.out" "$work/mini.out" || fail 'stdout differs'
cmp "$work/gnu.err" "$work/mini.err" || fail 'stderr differs'
require_contains 'AAPCS64_INDIRECT_RECORD_RETURN=PASS' "$work/mini.out"
printf '%s\n' 'AAPCS64_SRET_RUNTIME=PASS GNU_GAS=1 GNU_static_link=1 GNU_QEMU=2 mixed_ABI=2 sizes=17,24,32,40,64,144,2048'
