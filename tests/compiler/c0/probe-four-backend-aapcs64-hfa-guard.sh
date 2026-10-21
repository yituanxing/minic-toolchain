#!/bin/sh
# AAPCS64 B.3/B.4: 4x binary128 HFA (64B) must not use a GP pointer.
set -eu
: "$MINIC_CC"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
cat >"$work/hfa.c" <<'SRC'
struct Hfa4 { long double values[4]; };
int hfa_callee(struct Hfa4 value) { return 42; }
SRC
# GCC accepts this perfectly valid HFA signature.
aarch64-linux-gnu-gcc -c "$work/hfa.c" -o "$work/hfa.gnu.o"
aarch64-linux-gnu-gcc -E -P "$work/hfa.c" -o "$work/hfa.i"
if MINIC_M1_TRACE=1 "$MINIC_CC" --target=aarch64-linux -S \
       "$work/hfa.i" -o "$work/hfa.mini.s" 2>"$work/mini.err"; then
    echo "FOUR_BACKEND_AAPCS64_HFA_GUARD=FAIL unexpected_abi_admission=1" >&2
    exit 1
fi
if ! grep -q 'MINIC_BACKEND_REJECT arch=aarch64 function=hfa_callee stage=function_preflight' \
        "$work/mini.err" || [ -e "$work/hfa.mini.s" ]; then
    echo "FOUR_BACKEND_AAPCS64_HFA_GUARD=FAIL not_abi_preflight_or_partial_output" >&2
    head -20 "$work/mini.err" >&2
    exit 1
fi
echo "FOUR_BACKEND_AAPCS64_HFA_GUARD=PASS GNU_accepts=1 MiniC_explicit_reject=1 no_partial_asm=1"
