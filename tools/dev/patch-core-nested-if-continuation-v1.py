from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    file = Path(path)
    text = file.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"expected exactly one anchor in {path}, found {count}")
    file.write_text(text.replace(old, new, 1))


# A source then/else block may lower into multiple Core blocks.  The branch to the
# enclosing merge must leave from the child block's actual continuation, not from
# its entry block (which may already carry a nested control-flow terminator).
replace_once(
    "src/core/core_lower.c",
    "    MinicCoreBlockId then_block;\n"
    "    MinicCoreBlockId continuation_block;",
    "    MinicCoreBlockId then_block;\n"
    "    MinicCoreBlockId then_continuation_block;\n"
    "    MinicCoreBlockId else_continuation_block;\n"
    "    MinicCoreBlockId continuation_block;",
)
replace_once(
    "src/core/core_lower.c",
    "    status = lower_block(context, then_source, &then_terminated);\n"
    "    if (status != MINIC_CORE_LOWER_OK) {\n"
    "        return status;\n"
    "    }\n"
    "    else_terminated = false;",
    "    status = lower_block(context, then_source, &then_terminated);\n"
    "    if (status != MINIC_CORE_LOWER_OK) {\n"
    "        return status;\n"
    "    }\n"
    "    then_continuation_block = context->block_id;\n"
    "    else_continuation_block = MINIC_CORE_BLOCK_INVALID;\n"
    "    else_terminated = false;",
)
replace_once(
    "src/core/core_lower.c",
    "        if (status != MINIC_CORE_LOWER_OK) {\n"
    "            return status;\n"
    "        }\n"
    "    }\n\n"
    "    needs_merge = !then_terminated || else_source == NULL || !else_terminated;",
    "        if (status != MINIC_CORE_LOWER_OK) {\n"
    "            return status;\n"
    "        }\n"
    "        else_continuation_block = context->block_id;\n"
    "    }\n\n"
    "    needs_merge = !then_terminated || else_source == NULL || !else_terminated;",
)
replace_once(
    "src/core/core_lower.c",
    "        if (!then_terminated) {\n"
    "            status = set_branch(context, then_block, statement->span, merge_block);",
    "        if (!then_terminated) {\n"
    "            status =\n"
    "                set_branch(context, then_continuation_block, statement->span, merge_block);",
)
replace_once(
    "src/core/core_lower.c",
    "        if (else_source != NULL && !else_terminated) {\n"
    "            status = set_branch(context, else_block, statement->span, merge_block);",
    "        if (else_source != NULL && !else_terminated) {\n"
    "            status =\n"
    "                set_branch(context, else_continuation_block, statement->span, merge_block);",
)

Path("tests/compiler/c0/core_nested_if_continuation.c").write_text(
    """int core_nested_then(int outer, int inner) {
    int value = 9;
    if (outer) {
        if (inner)
            return 1;
        value = 2;
    }
    return value;
}

int core_nested_both(int outer, int inner) {
    int value = 9;
    if (outer) {
        if (inner)
            return 1;
        value = 2;
    } else {
        if (inner)
            return 3;
        value = 4;
    }
    return value;
}
"""
)

Path("tests/compiler/c0/core_nested_if_continuation_runtime.c").write_text(
    """#include <stdio.h>

int core_nested_then(int outer, int inner);
int core_nested_both(int outer, int inner);

int main(void) {
    printf("%d %d %d %d %d %d\\n",
           core_nested_then(0, 0),
           core_nested_then(1, 0),
           core_nested_then(1, 1),
           core_nested_both(1, 0),
           core_nested_both(0, 0),
           core_nested_both(0, 1));
    return 0;
}
"""
)

Path("tests/compiler/c0/run-core-nested-if-continuation.sh").write_text(
    """#!/bin/sh
set -eu
: "${MINIC:?MINIC must point to the compiler binary}"
: "${RISCV_CC:=riscv64-linux-gnu-gcc}"
: "${QEMU_RISCV64:=qemu-riscv64}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work="${BUILD_DIR:-$root/build/core-nested-if-continuation}"
source_file="$root/tests/compiler/c0/core_nested_if_continuation.c"
runtime_file="$root/tests/compiler/c0/core_nested_if_continuation_runtime.c"
mkdir -p "$work"
cc -E -P -std=gnu11 "$source_file" -o "$work/input.i"
MINIC_CORE_IR=strict "$MINIC" -S "$work/input.i" -o "$work/strict.s"
MINIC_CORE_CODEGEN=basic-v0 "$MINIC" -S "$work/input.i" -o "$work/core.s"
grep -q '^core_nested_then:' "$work/core.s"
grep -q '^core_nested_both:' "$work/core.s"
"$RISCV_CC" -static -O2 "$source_file" "$runtime_file" -o "$work/reference-rv64"
"$RISCV_CC" -static -O2 "$runtime_file" "$work/core.s" -o "$work/minic-rv64"
"$QEMU_RISCV64" "$work/reference-rv64" >"$work/reference.out"
"$QEMU_RISCV64" "$work/minic-rv64" >"$work/minic.out"
cmp "$work/reference.out" "$work/minic.out"
printf '%s\\n' 'PASS compiler/c0/core-nested-if-continuation'
"""
)

replace_once(
    ".github/scripts/compiler-c0-full-gate.sh",
    "core_condition_and_focused() {\n",
    "core_nested_if_continuation_focused() {\n"
    "    MINIC=\"$root/build/ci-debug/bin/minic\" \\\n"
    "    BUILD_DIR=\"$root/build/ci-core-nested-if-continuation\" \\\n"
    "    RISCV_CC=riscv64-linux-gnu-gcc \\\n"
    "    QEMU_RISCV64=qemu-riscv64 \\\n"
    "        sh tests/compiler/c0/run-core-nested-if-continuation.sh\n"
    "}\n\n"
    "core_condition_and_focused() {\n",
)
replace_once(
    ".github/scripts/compiler-c0-full-gate.sh",
    "start_gate core-condition-and-focused core_condition_and_focused\n",
    "start_gate core-nested-if-continuation-focused core_nested_if_continuation_focused\n"
    "start_gate core-condition-and-focused core_condition_and_focused\n",
)
