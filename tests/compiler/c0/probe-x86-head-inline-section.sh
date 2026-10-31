#!/usr/bin/env bash
set -Eeuo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
cc=${HOST_CC:-gcc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail() { echo "X86_HEAD_INLINE_SECTION=FAIL $*" >&2; exit 1; }

test -x "$minic" || fail 'MiniC compiler missing'
source="$root/tests/compiler/c0/x86_head_inline_section_probe.c"
driver="$root/tests/compiler/c0/x86_head_inline_section_driver.c"
"$cc" -E -P -std=gnu11 "$source" -o "$work/probe.i" \
    || fail 'GNU preprocessing'
"$minic" --target=x86_64-linux -S "$work/probe.i" -o "$work/probe.s" \
    || fail 'MiniC section-aware clone lowering'
"$cc" -c "$work/probe.s" -o "$work/probe.o" \
    || fail 'GNU assembler rejected MiniC section-local clones'
objdump -t "$work/probe.o" > "$work/symbols"
clone_for() {
    awk -v name="$1" '
        $0 == name ":" { inside = 1; next }
        inside && /^\.size / { exit }
        inside && /call __minic_inline_spec_/ {
            clone = $2
            sub(/@PLT$/, "", clone)
            print clone
            exit
        }
    ' "$work/probe.s"
}
clone_in_section() {
    awk -v section="$1" -v name="$2" '
        $4 == section && $NF == name { found = 1 }
        END { exit !found }
    ' "$work/symbols"
}
head_clone=$(clone_for probe_early)
init_clone=$(clone_for probe_later)
head_again_clone=$(clone_for probe_early_again)
explicit_clone=$(clone_for probe_explicit_owner)
nested_head_clone=$(clone_for probe_nested_early)
nested_init_clone=$(clone_for probe_nested_later)
nested_head_leaf=$(clone_for "$nested_head_clone")
nested_init_leaf=$(clone_for "$nested_init_clone")
test -n "$head_clone" && test -n "$init_clone" &&
    test -n "$head_again_clone" && test -n "$explicit_clone" &&
    test -n "$nested_head_clone" && test -n "$nested_init_clone" &&
    test -n "$nested_head_leaf" && test -n "$nested_init_leaf" \
    || fail 'fixture calls were not specialized'
test "$head_clone" = "$head_again_clone" &&
    test "$head_clone" != "$init_clone" \
    || fail 'same-section clone reuse / cross-section identity violated'
clone_in_section .head.text "$head_clone" \
    || fail 'head caller clone escaped .head.text'
clone_in_section .init.text "$init_clone" \
    || fail 'different caller section reused head clone'
clone_in_section .special.text "$explicit_clone" \
    || fail 'callee explicit section was overwritten'
test "$nested_head_clone" != "$nested_init_clone" &&
    test "$nested_head_leaf" != "$nested_init_leaf" \
    || fail 'transitive source-AST call reused a wrong-section clone'
clone_in_section .head.text "$nested_head_clone" &&
    clone_in_section .head.text "$nested_head_leaf" \
    || fail 'head transitive clone escaped .head.text'
clone_in_section .init.text "$nested_init_clone" &&
    clone_in_section .init.text "$nested_init_leaf" \
    || fail 'init transitive clone escaped .init.text'
"$cc" "$driver" "$work/probe.o" -o "$work/actual" \
    || fail 'GNU link rejected cloned sections'
"$work/actual" || fail 'MiniC clone runtime differs from C semantics'
"$cc" -O2 "$driver" "$source" -o "$work/reference" \
    || fail 'GNU reference link failed'
"$work/reference" || fail 'GNU reference runtime failed'
echo 'X86_HEAD_INLINE_SECTION=PASS head=local init=distinct explicit=preserved runtime=GNU_MATCH'
