#!/usr/bin/env bash
# Authentic GNU x86 VDSO .o.cmd carries -mindirect-branch=thunk-inline and
# -mindirect-branch-register. Both indirect CALL and computed goto must use
# the nested-call retpoline contract; a raw indirect branch is not equivalent.
set -Eeuo pipefail

root=$(cd "$(dirname "$0")/../../.." && pwd)
minic=${1:-"$root/build/release/bin/minic-cc"}
driver=${2:-"$root/build/release/bin/minic"}
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
fail() { printf 'X86_KBUILD_THUNK_INLINE=FAIL %s\n' "$*" >&2; exit 1; }
fixture="$root/tests/compiler/c0/x86_kbuild_mitigations.c"
cc -E -P -x c "$fixture" -o "$work/source.i"
flags=(-mindirect-branch=thunk-inline -mindirect-branch-register)
"$minic" -S "$work/source.i" --target=x86_64-linux "${flags[@]}" \
    -o "$work/mini.s"
"$driver" -S --target=x86_64-linux "${flags[@]}" \
    "$work/source.i" -o "$work/driver.s"
cmp "$work/mini.s" "$work/driver.s" || fail 'two MiniC driver entry points disagree'

# Assert both call and jump sites enter an independent nested-call thunk,
# preserve the caller return address and trap speculative RET at pause/lfence.
for pattern in '^  call 1b$' '^  jmp 1b$'; do
    grep -Eq "$pattern" "$work/mini.s" || fail "missing site $pattern"
done
test "$(grep -Ec '^  pause$' "$work/mini.s")" -ge 2 ||
    fail 'inline thunk speculative loop missing'
test "$(grep -Ec '^  lfence$' "$work/mini.s")" -ge 2 ||
    fail 'inline thunk lfence missing'
for register in r11 rax; do
    grep -Fq "  movq %$register, (%rsp)" "$work/mini.s" ||
        fail "missing ABI stack-slot replacement from $register"
done
if grep -Eq '^  (call|jmp) \*%|^  (call|jmp) __x86_indirect_thunk_' \
    "$work/mini.s"; then
    fail 'inline-thunk target escaped into raw or external indirect branch'
fi

as --64 "$work/mini.s" -o "$work/mini.o"
cc -no-pie -Wl,-z,noexecstack "$work/mini.o" -o "$work/mini"
"$work/mini" || fail 'GNU-linked MiniC retpoline runtime failed'

# A frozen VDSO command can include CET/IBT together with the later
# per-directory thunk-inline flags. Unlike GCC's C-mode option rejection,
# assembly mode accepts both; retain ENDBR on real indirect destinations,
# while the retpoline RET preserves the ordinary call/branch stack ABI.
"$minic" -S "$work/source.i" --target=x86_64-linux \
    -fcf-protection=branch "${flags[@]}" -o "$work/ibt.s"
grep -Fq '  endbr64' "$work/ibt.s" ||
    fail 'IBT computed-goto landing pad disappeared'
grep -Fq '  pause' "$work/ibt.s" ||
    fail 'IBT unexpectedly disabled the retpoline'
as --64 "$work/ibt.s" -o "$work/ibt.o"
cc -no-pie -Wl,-z,noexecstack "$work/ibt.o" -o "$work/ibt"
"$work/ibt" || fail 'IBT+retpoline GNU-linked runtime failed'

# Compare with actual GNU codegen strategy and execution, using GCC's
# supported non-CET combination for an indirect call/branch function.
cc -O0 -fno-pie -no-pie -fcf-protection=none "${flags[@]}" \
    -S "$fixture" -o "$work/gnu.s"
grep -Fq 'pause' "$work/gnu.s" || fail 'GNU retpoline oracle absent'
grep -Fq 'lfence' "$work/gnu.s" || fail 'GNU speculation fence oracle absent'
cc -O0 -fno-pie -no-pie -fcf-protection=none "${flags[@]}" \
    "$fixture" -o "$work/gnu"
"$work/gnu" || fail 'GNU C retpoline runtime failed'

# Last flag wins, exactly as machine option order is interpreted by GCC.
"$minic" -S "$work/source.i" --target=x86_64-linux \
    -mindirect-branch=thunk-inline -mindirect-branch=thunk-extern \
    -o "$work/extern.s"
grep -Fq '__x86_indirect_thunk_r11' "$work/extern.s" ||
    fail 'external mode did not replace inline mode'
"$minic" -S "$work/source.i" --target=x86_64-linux \
    -mindirect-branch=thunk-extern -mindirect-branch=thunk-inline \
    -o "$work/inline.s"
grep -Fq '  call 1b' "$work/inline.s" ||
    fail 'inline mode did not replace external mode'
"$minic" -S "$work/source.i" --target=x86_64-linux \
    -mindirect-branch=thunk-inline -mindirect-branch=keep \
    -o "$work/keep.s"
grep -Fq '  call *%r11' "$work/keep.s" ||
    fail 'keep did not restore unmitigated mode'
if "$minic" -S "$work/source.i" --target=aarch64-linux "${flags[@]}" \
    -o "$work/bad.s" >/dev/null 2>&1; then
    fail 'x86 retpoline flag accepted by another architecture'
fi
printf 'X86_KBUILD_THUNK_INLINE=PASS nested_call=1 computed_goto=1 GNU_AS=1 GNU_LD=1 MiniC_runtime=1 GNU_C_runtime=1 IBT_combination=1 option_order=3\n'
