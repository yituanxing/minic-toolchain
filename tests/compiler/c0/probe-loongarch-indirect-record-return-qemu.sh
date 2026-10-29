#!/usr/bin/env bash
# LP64D large-record RESULT, not merely by-reference aggregate arguments.
# The original LoongArch ELF psABI says that an indirect result consumes $a0,
# shifts named GP parameters to $a1+, and RETURNS the buffer pointer in $a0.
# GNU-produced callers and callees are the independent binary ABI oracle.
set -Eeuo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host_cc=${HOST_CC:-cc}
cross_cc=${LOONGARCH64_CC:-loongarch64-linux-gnu-gcc-13}
cross_qemu=${QEMU_LOONGARCH64:-qemu-loongarch64}
mini_source="$root/tests/compiler/c0/aarch64_indirect_record_return_minic.c"
gnu_source="$root/tests/compiler/c0/aarch64_indirect_record_return_gnu.c"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

fail() { printf '%s\n' "LOONGARCH64_SRET=FAIL $*" >&2; exit 1; }
contains() {
    if ! grep -Eq -- "$1" "$2"; then
        fail "assembly proof missing: $1"
    fi
}
reject() {
    local name=$1 source=$2
    if "$minic" --target=loongarch64-linux -S "$source" -o "$work/$name.s" \
         >"$work/$name.out" 2>"$work/$name.err"; then
        fail "unsafe aggregate ABI accepted: $name"
    fi
    test ! -e "$work/$name.s" || fail "partial output left: $name"
}

test -x "$minic" || fail "MiniC executable missing: $minic"
test -s "$mini_source" || fail 'missing C callee fixture'
test -s "$gnu_source" || fail 'missing GNU caller fixture'

"$host_cc" -E -P -std=gnu11 -x c "$mini_source" -o "$work/mini.i"
"$minic" --target=loongarch64-linux -S "$work/mini.i" -o "$work/mini.s" \
    || fail 'original MiniC large-result compilation'
test -s "$work/mini.s" || fail 'empty LP64D assembly'
for symbol in minic_three minic_four minic_eighteen minic_nested_three \
              minic_nested_eighteen minic_branch_three minic_ten_arguments \
              minic_nested_record minic_seventeen minic_sixty_four \
              minic_2048 minic_nested_2048 minic_dynamic_three \
              minic_roundtrip_eighteen minic_record_argument_and_result \
              minic_two_gnu_returns minic_direct_return_gnu_result minic_bits; do
    contains "^${symbol}:$" "$work/mini.s"
done

# Callee preserves hidden $a0; return must copy precisely the declared
# object extent, then leave the ORIGINAL destination in $a0.
contains '^  st\.d \$a0, \$(sp|s0|t3),' "$work/mini.s"
contains '^  ld\.d \$a0, \$(sp|s0|t3),' "$work/mini.s"
contains '^  move \$t2, \$a0$' "$work/mini.s"
contains '^  ld\.bu \$t4, \$t1, 0$' "$work/mini.s"
contains '^  st\.b \$t4, \$t2, 0$' "$work/mini.s"
contains '^  bnez \$t0, 1b$' "$work/mini.s"
contains '^  li\.d \$t0, 17$' "$work/mini.s"
contains '^  li\.d \$t0, 24$' "$work/mini.s"
contains '^  li\.d \$t0, 32$' "$work/mini.s"
contains '^  li\.d \$t0, 40$' "$work/mini.s"
contains '^  li\.d \$t0, 64$' "$work/mini.s"
contains '^  li\.d \$t0, 144$' "$work/mini.s"
contains '^  li\.d \$t0, 2048$' "$work/mini.s"
contains '^  add\.d \$a0, \$sp, \$a0$' "$work/mini.s"
contains '^  add\.d \$a0, \$s0, \$a0$' "$work/mini.s"
contains '^  bl gnu_make_three$' "$work/mini.s"
contains '^  bl gnu_make_eighteen$' "$work/mini.s"
contains '^  bl gnu_tap$' "$work/mini.s"

# The same Core result_object and LoongArch hidden $a0 plan are now used for
# direct and indirect record-return calls. The old negative gate rejected
# working code and caused CI to fail even after successful -S emission.
printf '%s\n' \
  'struct Large { unsigned long words[3]; };' \
  'extern struct Large (*factory)(unsigned long);' \
  'struct Large call_factory(unsigned long x) { return factory(x); }' \
  >"$work/indirect.c"
"$host_cc" -E -P -x c "$work/indirect.c" -o "$work/indirect.i"
"$minic" --target=loongarch64-linux -S "$work/indirect.i" \
    -o "$work/indirect.s" || fail 'indirect callee aggregate result rejected'
contains '^call_factory:$' "$work/indirect.s"
contains '^  st\.d \$a0, \$(sp|s0|t3),' "$work/indirect.s"
contains '^  ld\.d \$a0, \$(sp|s0|t3),' "$work/indirect.s"
contains '^  jirl \$ra, \$t2, 0$' "$work/indirect.s"

# The bounded ABI copy is deliberately 17..2048 bytes. Never silently
# truncate a 2049-byte result, and never leave a partial .s on rejection.
printf '%s\n' \
  'struct TooLarge { unsigned char bytes[2049]; };' \
  'struct TooLarge too_large(void) {' \
  '  struct TooLarge v; v.bytes[0] = 1; v.bytes[2048] = 2; return v;' \
  '}' >"$work/too-large.c"
"$host_cc" -E -P -x c "$work/too-large.c" -o "$work/too-large.i"
reject too-large "$work/too-large.i"
printf '%s\n' 'LOONGARCH64_SRET_SOURCE=PASS hidden_a0=1 explicit_a1=1 dynamic_frame=1 sizes=17..2048 indirect_positive=1 overlimit_negative=1'

if ! command -v "$cross_cc" >/dev/null 2>&1 ||
   ! command -v "$cross_qemu" >/dev/null 2>&1; then
    if [[ ${MINIC_REQUIRE_LOONG_SRET:-0} == 1 ]]; then
        fail 'required LoongArch64 GNU GAS/QEMU toolchain absent'
    fi
    printf '%s\n' 'LOONGARCH64_SRET_RUNTIME=SKIP GNU cross tools/QEMU missing'
    exit 0
fi

# Use GNU target preprocessing for BOTH the reference and mixed ABI run.
"$cross_cc" -E -P -std=gnu11 -x c "$mini_source" -o "$work/target-mini.i"
"$minic" --target=loongarch64-linux -S "$work/target-mini.i" -o "$work/target-mini.s" \
    || fail 'LoongArch-target preprocessing/compilation'
"$cross_cc" -std=gnu11 -O2 -static "$mini_source" "$gnu_source" \
    -o "$work/gnu.elf" || fail 'GNU reference static link'
"$cross_cc" -std=gnu11 -O2 -c "$gnu_source" \
    -o "$work/gnu.o" || fail 'GNU caller/callee object'
"$cross_cc" -static -x assembler "$work/target-mini.s" -x none "$work/gnu.o" \
    -o "$work/mini.elf" || fail 'MiniC/GNU mixed static link'

set +e
timeout 45s "$cross_qemu" "$work/gnu.elf" >"$work/gnu.out" 2>"$work/gnu.err"
gnu_status=$?
timeout 45s "$cross_qemu" "$work/mini.elf" >"$work/mini.out" 2>"$work/mini.err"
mini_status=$?
set -e
if [[ $gnu_status -ne 42 || $mini_status -ne 42 ]]; then
    printf '%s\n' "GNU=$gnu_status MiniC=$mini_status" >&2
    sed -n '1,14p' "$work/gnu.err" >&2
    sed -n '1,14p' "$work/mini.err" >&2
    fail 'GNU/GNU and GNU/MiniC runtime ABI differ'
fi
cmp "$work/gnu.out" "$work/mini.out" || fail 'mixed stdout mismatch'
cmp "$work/gnu.err" "$work/mini.err" || fail 'mixed stderr mismatch'
printf '%s\n' 'LOONGARCH64_SRET_RUNTIME=PASS GNU_AS=1 GNU_static=2 QEMU=2 mixed_ABI=1 sizes=17,24,32,40,64,144,2048'
