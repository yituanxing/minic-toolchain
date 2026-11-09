#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-block-scope-extern-object

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/block_scope_extern_object.c" -o "$work/extern.i"
"$minic" -S "$work/extern.i" -o "$work/extern.s"
grep -F 'zero_pfn' "$work/extern.s" >/dev/null
grep -F 'promoted' "$work/extern.s" >/dev/null
grep -E '^[[:space:]]*\.weak[[:space:]]+except_vec_cex$' "$work/extern.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/block_scope_extern_object scoped-global=1 repeated-compatible=1 cross-function-same-entity=1 file-scope-promotion=1 block-scope-weak-object=1'

# Original AArch64 Linux 6.6 mmu_context.h: block-scope extern declarations
# whose typedef itself carries the function signature (no trailing "(...)").
"$host_cc" -E -P -x c "$root/tests/compiler/c0/block_scope_extern_function_typedef.c" \
    -o "$work/function-typedef.i"
"$minic" -S "$work/function-typedef.i" --target=aarch64-linux \
    -o "$work/function-typedef.s"
grep -F 'idmap_cpu_replace_ttbr1' "$work/function-typedef.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/block_scope_extern_function_typedef signature=typedef-function scoped=1 aarch64=1'


"$host_cc" -E -P -x c "$root/tests/compiler/c0/invalid_block_scope_extern_leak.c" -o "$work/leak.i"
if "$minic" -S "$work/leak.i" -o "$work/leak.s" >"$work/leak.stdout" 2>"$work/leak.stderr"; then
    echo 'FAIL block-scope extern name leaked outside scope' >&2
    exit 1
fi
grep -F 'undeclared' "$work/leak.stderr" >/dev/null
printf '%s\n' 'PASS compiler/c0/invalid_block_scope_extern_leak visibility=scoped'

"$host_cc" -E -P -x c "$root/tests/compiler/c0/invalid_block_scope_extern_conflict.c" -o "$work/conflict.i"
if "$minic" -S "$work/conflict.i" -o "$work/conflict.s" >"$work/conflict.stdout" 2>"$work/conflict.stderr"; then
    echo 'FAIL conflicting block-scope extern redeclaration unexpectedly compiled' >&2
    exit 1
fi
grep -F 'conflicting extern object redeclaration' "$work/conflict.stderr" >/dev/null
printf '%s\n' 'PASS compiler/c0/invalid_block_scope_extern_conflict'
