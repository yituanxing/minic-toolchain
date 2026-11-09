#!/bin/sh
# Evidence-only audit. Known MC-043/068 issues remain OPEN; do not add to
# check-fast or treat a refused valid source as successful certification.
set -eu

: "${MINIC_CC:?set MINIC_CC to the compiler under test}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
host_cc=${HOST_CC:-cc}
generator="$root/tests/compiler/c0/generate-mc043-budget-hostile.py"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

audit_case() {
    name=$1
    kind=$2
    count=$3
    policy=$4
    python3 "$generator" --kind "$kind" --count "$count" > "$work/$name.c"
    "$host_cc" -std=gnu11 -O2 -Wall -Wextra -Werror \
        "$work/$name.c" -o "$work/$name.gnu"
    "$work/$name.gnu"
    "$host_cc" -std=gnu11 -E -P "$work/$name.c" -o "$work/$name.i"

    if "$MINIC_CC" --target=x86_64-linux -S "$work/$name.i" \
            -o "$work/$name.s" > "$work/$name.out" 2> "$work/$name.err"; then
        "$host_cc" -no-pie "$work/$name.s" -o "$work/$name.minic"
        if "$work/$name.minic"; then
            printf 'MC043_HOSTILE_CASE %s GNU=PASS MiniC=SEMANTIC_PASS\n' "$name"
        elif [ "$policy" = known_debt ]; then
            printf 'MC043_HOSTILE_CASE %s GNU=PASS MiniC=KNOWN_MC068_WRONGCODE\n' "$name"
        else
            printf 'MC043_HOSTILE_CASE %s GNU=PASS MiniC=UNSAFE_SILENT_WRONGCODE\n' "$name" >&2
            exit 1
        fi
    else
        if [ "$policy" = require_pass ]; then
            printf 'MC043_HOSTILE_CASE %s GNU=PASS MiniC=UNEXPECTED_REFUSAL\n' "$name" >&2
            exit 1
        fi
        grep -F 'cannot specialize inline' "$work/$name.err" >/dev/null
        printf 'MC043_HOSTILE_CASE %s GNU=PASS MiniC=KNOWN_BUDGET_REFUSAL\n' "$name"
    fi
}

# Both budgets accept their exact lower boundary and refuse valid source
# beyond it on the unchanged baseline compiler.
audit_case integer2048 integer 2048 require_pass
audit_case integer2049 integer 2049 guard_or_correct
audit_case boolean1024 boolean 1024 require_pass
audit_case boolean1025 boolean 1025 guard_or_correct

# Inlining can make a formal parameter a constant. A fallback out-of-line
# call is not observationally equivalent even when its Core lowers cleanly.
audit_case constantp2048 constant-p 2048 require_pass
audit_case constantp2049 constant-p 2049 guard_or_correct

# More fundamental: __builtin_return_address(0) denotes the caller's
# address in a GNU always_inline body, but an out-of-line clone sees the
# helper's caller. This is already wrong on ONE MiniC callsite (MC-068).
audit_case frame1 return-address 1 known_debt
audit_case frame2049 return-address 2049 guard_or_correct

echo 'MC043_HOSTILE_AUDIT=COMPLETE GNU_O2=8 threshold_refusals_are_known_debt NOT_CLOSED=MC043,MC068'
