#!/usr/bin/env bash
# One shared Source CALL must resolve from the *active* Core caller for scalar
# and aggregate returns alike.  All five targets compile exactly the same
# GNU-preprocessed input.  GNU AS/LD/native and section-symbol checks are
# deliberately limited to the host x86-64 acceptance path.
set -euo pipefail

root=$(cd "$(dirname "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/debug/bin/minic-cc"}
gnu_cc=${HOST_CC:-cc}
source="$root/tests/compiler/c0/p43_direct_record_callsite.c"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail() { printf 'P43_DIRECT_RECORD_CALLSITE=FAIL %s\n' "$*" >&2; exit 1; }

test -x "$minic" || fail 'MiniC executable missing'
"$gnu_cc" -E -P -std=gnu11 "$source" -o "$work/source.i" \
    || fail 'GNU preprocessing'
"$gnu_cc" -O2 -std=gnu11 -no-pie "$source" -o "$work/gnu" \
    || fail 'GNU native build'
"$work/gnu" || fail 'GNU native result mismatch'

targets=(riscv32-linux riscv64-linux x86_64-linux aarch64-linux loongarch64-linux)
for target in "${targets[@]}"; do
    if ! "$minic" --target="$target" -S "$work/source.i" \
             -o "$work/$target.s" >"$work/$target.stdout" \
             2>"$work/$target.stderr"; then
        tail -n 18 "$work/$target.stderr" >&2 || true
        fail "MiniC -S target=$target"
    fi
    test -s "$work/$target.s" || fail "empty assembly target=$target"
done

"$gnu_cc" -no-pie "$work/x86_64-linux.s" -o "$work/minic" \
    || fail 'GNU assembler/linker rejected MiniC x86 output'
status=0
"$work/minic" || status=$?
if (( status != 0 )); then
    fail "MiniC x86 native result mismatch exit=$status"
fi

# All direct functions have explicit source section identity.  Verify
# the emitted ELF symbols rather than trusting textual .section order.
objdump -t "$work/minic" >"$work/symbols"
for name in p43_record_head p43_record_head_other p43_record_head_reuse; do
    awk -v name="$name" '$4 == ".head.text" && $NF == name { found=1 } END { exit !found }' \
        "$work/symbols" || fail "missing .head.text owner=$name"
done
awk '$4 == ".init.text" && $NF == "p43_record_init" { found=1 } END { exit !found }' \
    "$work/symbols" || fail 'missing .init.text record caller'

# Each direct Record caller must select its own middle clone, and each
# middle clone must in turn select the leaf clone appropriate to that
# original caller's integer/symbol/boolean facts and ELF section.  Verifying
# the *second* edge is critical: the historical bug routed every cloned
# middle through one stale leaf, even though the outer section was correct.
clone_callee() {
    local name=$1
    awk -v name="$name" '
        $0 == name ":" { inside=1; next }
        inside && /^\.size / { exit }
        inside && /call __minic_inline_spec_/ {
            callee=$2
            sub(/@PLT$/, "", callee)
            print callee
            exit
        }
    ' "$work/x86_64-linux.s"
}
assert_elf_section() {
    local symbol=$1 section=$2
    awk -v name="$symbol" -v section="$section" \
        '$4 == section && $NF == name { found=1 } END { exit !found }' \
        "$work/symbols" || fail "clone $symbol missing ELF section $section"
}

head_middle=$(clone_callee p43_record_head)
init_middle=$(clone_callee p43_record_init)
other_middle=$(clone_callee p43_record_head_other)
reuse_middle=$(clone_callee p43_record_head_reuse)
for owner in head init other reuse; do
    case "$owner" in
        head) middle=$head_middle ;;
        init) middle=$init_middle ;;
        other) middle=$other_middle ;;
        reuse) middle=$reuse_middle ;;
    esac
    [[ -n "$middle" ]] || fail "missing direct Record CALL middle clone owner=$owner"
    if [[ "$owner" == init ]]; then
        assert_elf_section "$middle" .init.text
    else
        assert_elf_section "$middle" .head.text
    fi
done
head_leaf=$(clone_callee "$head_middle")
init_leaf=$(clone_callee "$init_middle")
other_leaf=$(clone_callee "$other_middle")
reuse_leaf=$(clone_callee "$reuse_middle")
[[ -n "$head_leaf" && -n "$init_leaf" && -n "$other_leaf" && -n "$reuse_leaf" ]] \
    || fail 'shared source Record CALL lost a transitive leaf clone'
for leaf in "$head_leaf" "$other_leaf" "$reuse_leaf"; do
    assert_elf_section "$leaf" .head.text
done
assert_elf_section "$init_leaf" .init.text
[[ "$head_leaf" != "$init_leaf" && "$head_leaf" != "$other_leaf" &&
   "$head_leaf" == "$reuse_leaf" ]] \
    || fail 'source CALL leaf clone identity ignored per-caller facts'

echo 'P43_DIRECT_RECORD_CALLSITE=PASS five_targets=5 gnu_native=PASS minic_x86_native=PASS head_init_elf=PASS nested_clone_section=PASS leaf_identity=PASS'
