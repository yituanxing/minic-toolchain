from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)


root = Path(__file__).resolve().parents[2]
core_path = root / "src/core/core_lower.c"
gate_path = root / ".github/scripts/compiler-c0-full-gate.sh"
source_path = root / "tests/compiler/c0/core_scalar_assignment_implicit_void.c"
runtime_path = root / "tests/compiler/c0/core_scalar_assignment_implicit_void_runtime.c"
runner_path = root / "tests/compiler/c0/run-core-scalar-assignment-implicit-void.sh"

core = core_path.read_text()

old_assignment = r'''    status = lower_address(context, target_id, &address_id);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    status = lower_integer_assignment_value(context, target->type, source_id, &stored_value);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
'''
new_assignment = r'''    status = lower_address(context, target_id, &address_id);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    {
        MinicType stored_type;

        if (!minic_type_unqualified(target->type, &stored_type) ||
            !core_memory_scalar_type(stored_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_scalar_assignment_value(context, stored_type, source_id, &stored_value);
    }
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
'''
core = replace_once(core, old_assignment, new_assignment, "scalar assignment owner")

old_empty = r'''    if (source_block->statement_count == 0U) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
'''
new_empty = r'''    if (source_block->statement_count == 0U &&
        !minic_type_is_void(source_function->return_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
'''
core = replace_once(core, old_empty, new_empty, "empty void function")

old_fallthrough = r'''    free(local_objects);
    if (status != MINIC_CORE_LOWER_OK) {
        minic_core_function_destroy(&lowered);
        return status;
    }
    if (!terminated) {
        minic_core_function_destroy(&lowered);
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    if (!minic_core_function_verify(&lowered)) {
'''
new_fallthrough = r'''    free(local_objects);
    if (status != MINIC_CORE_LOWER_OK) {
        minic_core_function_destroy(&lowered);
        return status;
    }
    if (!terminated && minic_type_is_void(source_function->return_type)) {
        MinicCoreTerminator terminator;

        (void)memset(&terminator, 0, sizeof(terminator));
        terminator.kind = MINIC_CORE_TERMINATOR_RETURN;
        terminator.return_value = MINIC_CORE_VALUE_INVALID;
        if (!minic_core_function_set_terminator(&lowered, context.block_id, &terminator)) {
            minic_core_function_destroy(&lowered);
            return MINIC_CORE_LOWER_ERROR;
        }
        terminated = true;
    }
    if (!terminated) {
        minic_core_function_destroy(&lowered);
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    if (!minic_core_function_verify(&lowered)) {
'''
core = replace_once(core, old_fallthrough, new_fallthrough, "implicit void return")
core_path.write_text(core)

source_path.write_text(
    r'''struct core_m4_list_head {
    struct core_m4_list_head *next;
    struct core_m4_list_head *prev;
};

#define CORE_M4_WRITE_ONCE(x, value)                                                        \
    do {                                                                                     \
        *(volatile __typeof__(x) *)&(x) = (value);                                           \
    } while (0)

void core_m4_init_list_head(struct core_m4_list_head *list) {
    CORE_M4_WRITE_ONCE(list->next, list);
    CORE_M4_WRITE_ONCE(list->prev, list);
}

void core_m4_pointer_store(const void **slot, const void *value) {
    *slot = value;
}

void core_m4_empty_void(void) {}
'''
)

runtime_path.write_text(
    r'''#include <stdio.h>

struct core_m4_list_head {
    struct core_m4_list_head *next;
    struct core_m4_list_head *prev;
};

void core_m4_init_list_head(struct core_m4_list_head *list);
void core_m4_pointer_store(const void **slot, const void *value);
void core_m4_empty_void(void);

int main(void) {
    struct core_m4_list_head head;
    const void *slot;
    int value;

    head.next = 0;
    head.prev = 0;
    slot = 0;
    value = 23;
    core_m4_init_list_head(&head);
    core_m4_pointer_store(&slot, &value);
    core_m4_empty_void();
    (void)printf("%d %d %d\n", head.next == &head, head.prev == &head, slot == &value);
    return 0;
}
'''
)

runner_path.write_text(
    r'''#!/bin/sh
set -eu

: "${MINIC:?MINIC must point to the compiler binary}"
: "${RISCV_CC:=riscv64-linux-gnu-gcc}"
: "${QEMU_RISCV64:=qemu-riscv64}"

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work="${BUILD_DIR:-$root/build/core-scalar-assignment-implicit-void}"
source_file="$root/tests/compiler/c0/core_scalar_assignment_implicit_void.c"
runtime_file="$root/tests/compiler/c0/core_scalar_assignment_implicit_void_runtime.c"
mkdir -p "$work"

cc -E -P -std=gnu11 "$source_file" -o "$work/core_scalar_assignment_implicit_void.i"
MINIC_CORE_IR=strict "$MINIC" -S "$work/core_scalar_assignment_implicit_void.i" \
    -o "$work/core_scalar_assignment_implicit_void-strict.s"
MINIC_CORE_CODEGEN=basic-v0 "$MINIC" -S "$work/core_scalar_assignment_implicit_void.i" \
    -o "$work/core_scalar_assignment_implicit_void-core.s"

for symbol in core_m4_init_list_head core_m4_pointer_store core_m4_empty_void; do
    grep -q "^${symbol}:" "$work/core_scalar_assignment_implicit_void-core.s"
    grep -q "${symbol}_core_bb0" "$work/core_scalar_assignment_implicit_void-core.s"
done

"$RISCV_CC" -static -O2 "$source_file" "$runtime_file" -o "$work/reference-rv64"
"$RISCV_CC" -static -O2 "$runtime_file" "$work/core_scalar_assignment_implicit_void-core.s" \
    -o "$work/minic-rv64"
"$QEMU_RISCV64" "$work/reference-rv64" >"$work/reference.out"
"$QEMU_RISCV64" "$work/minic-rv64" >"$work/minic.out"
cmp "$work/reference.out" "$work/minic.out"
printf '%s\n' 'PASS compiler/c0/core-scalar-assignment-implicit-void'
'''
)

gate = gate_path.read_text()
anchor = r'''core_fixed_call_scalar_conversions_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \
    BUILD_DIR="$root/build/ci-core-fixed-call-scalar-conversions" \
    RISCV_CC=riscv64-linux-gnu-gcc \
    QEMU_RISCV64=qemu-riscv64 \
        sh tests/compiler/c0/run-core-fixed-call-scalar-conversions.sh
}

'''
replacement = anchor + r'''core_scalar_assignment_implicit_void_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \
    BUILD_DIR="$root/build/ci-core-scalar-assignment-implicit-void" \
    RISCV_CC=riscv64-linux-gnu-gcc \
    QEMU_RISCV64=qemu-riscv64 \
        sh tests/compiler/c0/run-core-scalar-assignment-implicit-void.sh
}

'''
gate = replace_once(gate, anchor, replacement, "gate helper")
gate = replace_once(
    gate,
    "start_gate core-fixed-call-scalar-conversions-focused core_fixed_call_scalar_conversions_focused\n",
    "start_gate core-fixed-call-scalar-conversions-focused core_fixed_call_scalar_conversions_focused\n"
    "start_gate core-scalar-assignment-implicit-void-focused core_scalar_assignment_implicit_void_focused\n",
    "gate invocation",
)
gate_path.write_text(gate)
