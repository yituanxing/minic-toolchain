#!/usr/bin/env bash
# P70: exported canonical labels and executable indirect landing pads are
# independent. Integer-specialized clones share C statements but must own
# their own IBT entries without defining the canonical owner's .Luser labels.
set -Eeuo pipefail

root=$(cd "$(dirname "$0")/../../.." && pwd)
minic=${1:-"$root/build/debug/bin/minic-cc"}
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
fail() { printf 'P70_CORE_INDIRECT_ALIAS_IBT=FAIL %s\n' "$*" >&2; exit 1; }

source="$root/tests/compiler/audit/mc072_inline_computed_label_owner.c"
cc -E -P -DMC072_RUNTIME -std=gnu11 -x c "$source" -o "$work/input.i"
"$minic" -S "$work/input.i" --target=x86_64-linux \
    -fcf-protection=branch -o "$work/mini.s"

# Check actual label/ENDBR adjacency per clone, not just existence of one
# arbitrary ENDBR elsewhere (the function entry always has one).
awk '
    /^__minic_inline_spec_[a-zA-Z0-9_]+:$/ {
        in_clone = 1
        ++clones
    }
    /^\.size __minic_inline_spec_/ { in_clone = 0 }
    in_clone && /^\.Luser_[0-9]+:$/ { bad_alias = 1 }
    in_clone && /^\.Lmini_x86_f[0-9]+_b[0-9]+:$/ {
        if (getline next_line <= 0) exit 2
        if (next_line == "  endbr64") ++pads
    }
    /^\.Luser_[0-9]+:$/ {
        ++exports
        if (getline next_line <= 0 || next_line != "  endbr64")
            bad_export_pad = 1
    }
    END {
        if (clones != 2 || pads != 4 || exports != 2 ||
            bad_alias || bad_export_pad) exit 1
    }
' "$work/mini.s" || fail 'clone landing pads or canonical exports'

cc -fno-pie -no-pie -Wl,-z,noexecstack -x assembler "$work/mini.s" \
    -o "$work/mini"
cc -fno-pie -no-pie -O0 -DMC072_RUNTIME -std=gnu11 -x c "$source" \
    -o "$work/gnu"
"$work/mini" || fail 'MiniC native runtime'
"$work/gnu" || fail 'GNU native runtime oracle'
printf '%s\n' \
    'P70_CORE_INDIRECT_ALIAS_IBT=PASS canonical_alias=2 clones=2 clone_landing_pads=4 no_duplicate_alias=1 GNU_AS_LD=1 native_diff=1'
