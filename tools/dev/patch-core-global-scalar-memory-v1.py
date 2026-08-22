from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)


root = Path(__file__).resolve().parents[2]
header_path = root / "src/core/core_ir.h"
ir_path = root / "src/core/core_ir.c"
lower_path = root / "src/core/core_lower.c"
codegen_path = root / "src/target/riscv64/core_codegen.c"
gate_path = root / ".github/scripts/compiler-c0-full-gate.sh"
source_path = root / "tests/compiler/c0/core_global_scalar_memory.c"
runtime_path = root / "tests/compiler/c0/core_global_scalar_memory_runtime.c"
runner_path = root / "tests/compiler/c0/run-core-global-scalar-memory.sh"

header = header_path.read_text()
header = replace_once(
    header,
    "typedef uint32_t MinicCoreObjectId;\ntypedef uint32_t MinicCoreCalleeId;\n",
    "typedef uint32_t MinicCoreObjectId;\ntypedef uint32_t MinicCoreGlobalId;\ntypedef uint32_t MinicCoreCalleeId;\n",
    "global id typedef",
)
header = replace_once(
    header,
    "#define MINIC_CORE_OBJECT_INVALID UINT32_MAX\n#define MINIC_CORE_CALLEE_INVALID UINT32_MAX\n",
    "#define MINIC_CORE_OBJECT_INVALID UINT32_MAX\n#define MINIC_CORE_GLOBAL_INVALID UINT32_MAX\n#define MINIC_CORE_CALLEE_INVALID UINT32_MAX\n",
    "global invalid id",
)
header = replace_once(
    header,
    "    MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS,\n    MINIC_CORE_INSTRUCTION_FIELD_ADDRESS,\n",
    "    MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS,\n    MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS,\n    MINIC_CORE_INSTRUCTION_FIELD_ADDRESS,\n",
    "global address opcode",
)
header = replace_once(
    header,
    "typedef struct MinicCoreObject {\n    MinicSourceSpan span;\n    MinicType type;\n} MinicCoreObject;\n\ntypedef struct MinicCoreCallee {\n",
    "typedef struct MinicCoreObject {\n    MinicSourceSpan span;\n    MinicType type;\n} MinicCoreObject;\n\ntypedef struct MinicCoreGlobal {\n    char *name;\n    size_t name_length;\n    MinicType type;\n} MinicCoreGlobal;\n\ntypedef struct MinicCoreCallee {\n",
    "global reference record",
)
header = replace_once(
    header,
    "        size_t parameter_index;\n        MinicCoreObjectId object_id;\n        struct {\n",
    "        size_t parameter_index;\n        MinicCoreObjectId object_id;\n        MinicCoreGlobalId global_id;\n        struct {\n",
    "global instruction payload",
)
header = replace_once(
    header,
    "    MinicCoreCallee *callees;\n    size_t callee_count;\n    size_t callee_capacity;\n",
    "    MinicCoreGlobal *globals;\n    size_t global_count;\n    size_t global_capacity;\n    MinicCoreCallee *callees;\n    size_t callee_count;\n    size_t callee_capacity;\n",
    "function globals arena",
)
header = replace_once(
    header,
    "bool minic_core_function_add_object(MinicCoreFunction *function,\n                                    MinicSourceSpan span,\n                                    MinicType type,\n                                    MinicCoreObjectId *object_id);\nbool minic_core_function_add_callee(MinicCoreFunction *function,\n",
    "bool minic_core_function_add_object(MinicCoreFunction *function,\n                                    MinicSourceSpan span,\n                                    MinicType type,\n                                    MinicCoreObjectId *object_id);\nbool minic_core_function_add_global(MinicCoreFunction *function,\n                                    const char *name,\n                                    size_t name_length,\n                                    MinicType type,\n                                    MinicCoreGlobalId *global_id);\nbool minic_core_function_add_callee(MinicCoreFunction *function,\n",
    "global add api",
)
header_path.write_text(header)

ir = ir_path.read_text()
ir = replace_once(
    ir,
    "    size_t block_index;\n    size_t callee_index;\n",
    "    size_t block_index;\n    size_t callee_index;\n    size_t global_index;\n",
    "destroy global index",
)
ir = replace_once(
    ir,
    "    for (callee_index = 0U; callee_index < function->callee_count; ++callee_index) {\n        free(function->callees[callee_index].name);\n        free(function->callees[callee_index].parameter_types);\n    }\n    free(function->name);\n",
    "    for (callee_index = 0U; callee_index < function->callee_count; ++callee_index) {\n        free(function->callees[callee_index].name);\n        free(function->callees[callee_index].parameter_types);\n    }\n    for (global_index = 0U; global_index < function->global_count; ++global_index) {\n        free(function->globals[global_index].name);\n    }\n    free(function->name);\n",
    "destroy global names",
)
ir = replace_once(
    ir,
    "    free(function->parameter_types);\n    free(function->callees);\n",
    "    free(function->parameter_types);\n    free(function->globals);\n    free(function->callees);\n",
    "destroy globals arena",
)
add_global = r'''
bool minic_core_function_add_global(MinicCoreFunction *function,
                                    const char *name,
                                    size_t name_length,
                                    MinicType type,
                                    MinicCoreGlobalId *global_id) {
    char *name_copy;
    size_t index;

    if (function == NULL || name == NULL || name_length == 0U || global_id == NULL ||
        function->global_count >= (size_t)UINT32_MAX ||
        (!minic_type_is_integer(type) && !minic_type_is_pointer(type))) {
        return false;
    }
    for (index = 0U; index < function->global_count; ++index) {
        const MinicCoreGlobal *existing;

        existing = &function->globals[index];
        if (existing->name_length == name_length && memcmp(existing->name, name, name_length) == 0) {
            if (!minic_type_equal(existing->type, type)) {
                return false;
            }
            *global_id = (MinicCoreGlobalId)index;
            return true;
        }
    }
    name_copy = copy_name(name, name_length);
    if (name_copy == NULL ||
        !grow_array((void **)&function->globals,
                    &function->global_capacity,
                    function->global_count,
                    sizeof(*function->globals))) {
        free(name_copy);
        return false;
    }
    function->globals[function->global_count].name = name_copy;
    function->globals[function->global_count].name_length = name_length;
    function->globals[function->global_count].type = type;
    *global_id = (MinicCoreGlobalId)function->global_count;
    function->global_count += 1U;
    return true;
}

'''
ir = replace_once(
    ir,
    "static bool core_call_scalar_type(MinicType type) {\n",
    add_global + "static bool core_call_scalar_type(MinicType type) {\n",
    "global add implementation",
)
ir = replace_once(
    ir,
    "    case MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS: {\n        MinicType pointer_type;\n\n        if (!instruction_result_is_valid(function, instruction) ||\n            instruction->value.object_id >= function->object_count ||\n            !minic_type_pointer_to(function->objects[instruction->value.object_id].type,\n                                   &pointer_type)) {\n            return false;\n        }\n        return minic_type_equal(pointer_type, instruction->type);\n    }\n    case MINIC_CORE_INSTRUCTION_FIELD_ADDRESS: {\n",
    "    case MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS: {\n        MinicType pointer_type;\n\n        if (!instruction_result_is_valid(function, instruction) ||\n            instruction->value.object_id >= function->object_count ||\n            !minic_type_pointer_to(function->objects[instruction->value.object_id].type,\n                                   &pointer_type)) {\n            return false;\n        }\n        return minic_type_equal(pointer_type, instruction->type);\n    }\n    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS: {\n        MinicType pointer_type;\n\n        if (!instruction_result_is_valid(function, instruction) ||\n            instruction->value.global_id >= function->global_count ||\n            !minic_type_pointer_to(function->globals[instruction->value.global_id].type,\n                                   &pointer_type)) {\n            return false;\n        }\n        return minic_type_equal(pointer_type, instruction->type);\n    }\n    case MINIC_CORE_INSTRUCTION_FIELD_ADDRESS: {\n",
    "global address verifier",
)
ir = replace_once(
    ir,
    "        !storage_shape_is_valid(\n            function->callees, function->callee_count, function->callee_capacity) ||\n",
    "        !storage_shape_is_valid(\n            function->globals, function->global_count, function->global_capacity) ||\n        !storage_shape_is_valid(\n            function->callees, function->callee_count, function->callee_capacity) ||\n",
    "global storage shape",
)
ir = replace_once(
    ir,
    "    for (index = 0U; index < function->callee_count; ++index) {\n        const MinicCoreCallee *callee;\n",
    "    for (index = 0U; index < function->global_count; ++index) {\n        const MinicCoreGlobal *global;\n\n        global = &function->globals[index];\n        if (global->name == NULL || global->name_length == 0U ||\n            (!minic_type_is_integer(global->type) && !minic_type_is_pointer(global->type))) {\n            return false;\n        }\n    }\n    for (index = 0U; index < function->callee_count; ++index) {\n        const MinicCoreCallee *callee;\n",
    "verify global references",
)
ir = replace_once(
    ir,
    "    case MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS:\n        return fprintf(output,\n                       \"  %%%\" PRIu32 \" = object.addr %%o%\" PRIu32 \"\\n\",\n                       instruction->result,\n                       instruction->value.object_id) >= 0;\n    case MINIC_CORE_INSTRUCTION_FIELD_ADDRESS:\n",
    "    case MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS:\n        return fprintf(output,\n                       \"  %%%\" PRIu32 \" = object.addr %%o%\" PRIu32 \"\\n\",\n                       instruction->result,\n                       instruction->value.object_id) >= 0;\n    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS:\n        if (function == NULL || instruction->value.global_id >= function->global_count) {\n            return false;\n        }\n        return fprintf(output,\n                       \"  %%%\" PRIu32 \" = global.addr @%s\\n\",\n                       instruction->result,\n                       function->globals[instruction->value.global_id].name) >= 0;\n    case MINIC_CORE_INSTRUCTION_FIELD_ADDRESS:\n",
    "dump global address",
)
ir_path.write_text(ir)

lower = lower_path.read_text()
global_lower = r'''    if (expression->kind == MINIC_EXPRESSION_GLOBAL_OBJECT) {
        const MinicGlobalObject *global;
        MinicCoreGlobalId global_id;

        global = minic_c0_program_global_object(context->body->program,
                                                expression->value.global_object_id);
        if (global == NULL || global->name == NULL || global->name_length == 0U) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (!minic_type_equal(global->type, expression->type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (!core_memory_scalar_type(global->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        if (!minic_core_function_add_global(context->function,
                                            global->name,
                                            global->name_length,
                                            global->type,
                                            &global_id)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS;
        instruction.span = expression->span;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.global_id = global_id;
        if (!minic_type_pointer_to(expression->type, &instruction.type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, address_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
'''
lower = replace_once(
    lower,
    "    if (expression->kind == MINIC_EXPRESSION_DEREFERENCE) {\n",
    global_lower + "    if (expression->kind == MINIC_EXPRESSION_DEREFERENCE) {\n",
    "lower global address",
)
lower_path.write_text(lower)

codegen = codegen_path.read_text()
codegen = replace_once(
    codegen,
    "    case MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS:\n    case MINIC_CORE_INSTRUCTION_LOAD:\n",
    "    case MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS:\n    case MINIC_CORE_INSTRUCTION_LOAD:\n",
    "stable object/load support anchor",
)
codegen = replace_once(
    codegen,
    "    case MINIC_CORE_INSTRUCTION_CALL:\n        if (instruction->value.call.callee_id >= function->callee_count ||\n",
    "    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS:\n        return instruction->value.global_id < function->global_count &&\n               function->globals[instruction->value.global_id].name != NULL &&\n               function->globals[instruction->value.global_id].name_length != 0U;\n    case MINIC_CORE_INSTRUCTION_CALL:\n        if (instruction->value.call.callee_id >= function->callee_count ||\n",
    "global address codegen support",
)
codegen = replace_once(
    codegen,
    "    for (index = 0U; index < function->object_count; ++index) {\n        if (!core_scalar_type(function->objects[index].type)) {\n            return false;\n        }\n    }\n    for (index = 0U; index < function->value_count; ++index) {\n",
    "    for (index = 0U; index < function->object_count; ++index) {\n        if (!core_scalar_type(function->objects[index].type)) {\n            return false;\n        }\n    }\n    for (index = 0U; index < function->global_count; ++index) {\n        if (function->globals[index].name == NULL ||\n            function->globals[index].name_length == 0U ||\n            !core_scalar_type(function->globals[index].type)) {\n            return false;\n        }\n    }\n    for (index = 0U; index < function->value_count; ++index) {\n",
    "global emit eligibility",
)
codegen = replace_once(
    codegen,
    "    case MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS:\n        if (!core_object_offset(frame, instruction->value.object_id, &object_offset) ||\n            !emit_sp_address(file, \"t0\", object_offset)) {\n            return false;\n        }\n        return store_core_value(file, frame, instruction->result, \"t0\");\n    case MINIC_CORE_INSTRUCTION_LOAD:\n",
    "    case MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS:\n        if (!core_object_offset(frame, instruction->value.object_id, &object_offset) ||\n            !emit_sp_address(file, \"t0\", object_offset)) {\n            return false;\n        }\n        return store_core_value(file, frame, instruction->result, \"t0\");\n    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS:\n        if (instruction->value.global_id >= function->global_count ||\n            fprintf(file, \"  la t0, %s\\n\",\n                    function->globals[instruction->value.global_id].name) < 0) {\n            return false;\n        }\n        return store_core_value(file, frame, instruction->result, \"t0\");\n    case MINIC_CORE_INSTRUCTION_LOAD:\n",
    "emit global address",
)
codegen_path.write_text(codegen)

source_path.write_text(
    r'''extern int core_m5_global;

int core_m5_global_load(void) {
    return core_m5_global;
}

void core_m5_global_store(int value) {
    core_m5_global = value;
}
'''
)
runtime_path.write_text(
    r'''#include <stdio.h>

int core_m5_global = 11;

int core_m5_global_load(void);
void core_m5_global_store(int value);

int main(void) {
    int before;
    int after;

    before = core_m5_global_load();
    core_m5_global_store(29);
    after = core_m5_global_load();
    (void)printf("%d %d\n", before, after);
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
work="${BUILD_DIR:-$root/build/core-global-scalar-memory}"
source_file="$root/tests/compiler/c0/core_global_scalar_memory.c"
runtime_file="$root/tests/compiler/c0/core_global_scalar_memory_runtime.c"
mkdir -p "$work"

cc -E -P -std=gnu11 "$source_file" -o "$work/core_global_scalar_memory.i"
MINIC_CORE_IR=strict "$MINIC" -S "$work/core_global_scalar_memory.i" \
    -o "$work/core_global_scalar_memory-strict.s"
MINIC_CORE_CODEGEN=basic-v0 "$MINIC" -S "$work/core_global_scalar_memory.i" \
    -o "$work/core_global_scalar_memory-core.s"

grep -q '^core_m5_global_load:' "$work/core_global_scalar_memory-core.s"
grep -q '^core_m5_global_store:' "$work/core_global_scalar_memory-core.s"
grep -q 'la t0, core_m5_global' "$work/core_global_scalar_memory-core.s"

"$RISCV_CC" -static -O2 "$source_file" "$runtime_file" -o "$work/reference-rv64"
"$RISCV_CC" -static -O2 "$runtime_file" "$work/core_global_scalar_memory-core.s" \
    -o "$work/minic-rv64"
"$QEMU_RISCV64" "$work/reference-rv64" >"$work/reference.out"
"$QEMU_RISCV64" "$work/minic-rv64" >"$work/minic.out"
cmp "$work/reference.out" "$work/minic.out"
printf '%s\n' 'PASS compiler/c0/core-global-scalar-memory'
'''
)

gate = gate_path.read_text()
anchor = r'''core_scalar_assignment_implicit_void_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \
    BUILD_DIR="$root/build/ci-core-scalar-assignment-implicit-void" \
    RISCV_CC=riscv64-linux-gnu-gcc \
    QEMU_RISCV64=qemu-riscv64 \
        sh tests/compiler/c0/run-core-scalar-assignment-implicit-void.sh
}

'''
helper = anchor + r'''core_global_scalar_memory_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \
    BUILD_DIR="$root/build/ci-core-global-scalar-memory" \
    RISCV_CC=riscv64-linux-gnu-gcc \
    QEMU_RISCV64=qemu-riscv64 \
        sh tests/compiler/c0/run-core-global-scalar-memory.sh
}

'''
gate = replace_once(gate, anchor, helper, "gate helper")
gate = replace_once(
    gate,
    "start_gate core-scalar-assignment-implicit-void-focused core_scalar_assignment_implicit_void_focused\n",
    "start_gate core-scalar-assignment-implicit-void-focused core_scalar_assignment_implicit_void_focused\n"
    "start_gate core-global-scalar-memory-focused core_global_scalar_memory_focused\n",
    "gate invocation",
)
gate_path.write_text(gate)
