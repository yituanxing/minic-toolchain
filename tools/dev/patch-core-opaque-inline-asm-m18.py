#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 match, found {count}")
    target.write_text(text.replace(old, new, 1))


def insert_before_last(path: str, marker: str, insertion: str, label: str) -> None:
    target = Path(path)
    text = target.read_text()
    index = text.rfind(marker)
    if index < 0:
        raise SystemExit(f"{label}: marker not found")
    target.write_text(text[:index] + insertion + text[index:])


def write_new(path: str, content: str) -> None:
    target = Path(path)
    if target.exists():
        raise SystemExit(f"{path}: already exists")
    target.write_text(content)


replace_once(
    "src/core/core_ir.h",
    "typedef uint32_t MinicCoreCalleeId;\n",
    "typedef uint32_t MinicCoreCalleeId;\n"
    "typedef uint32_t MinicCoreInlineAsmId;\n",
    "Core inline-asm id typedef",
)
replace_once(
    "src/core/core_ir.h",
    "#define MINIC_CORE_CALLEE_INVALID UINT32_MAX\n",
    "#define MINIC_CORE_CALLEE_INVALID UINT32_MAX\n"
    "#define MINIC_CORE_INLINE_ASM_INVALID UINT32_MAX\n",
    "Core inline-asm invalid id",
)
replace_once(
    "src/core/core_ir.h",
    "    MINIC_CORE_INSTRUCTION_STORE,\n    MINIC_CORE_INSTRUCTION_CALL\n",
    "    MINIC_CORE_INSTRUCTION_STORE,\n"
    "    MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM,\n"
    "    MINIC_CORE_INSTRUCTION_CALL\n",
    "Core opaque inline-asm opcode",
)
replace_once(
    "src/core/core_ir.h",
    '''typedef struct MinicCoreCallee {
    char *name;
    size_t name_length;
    MinicType return_type;
    MinicType *parameter_types;
    size_t parameter_count;
} MinicCoreCallee;
''',
    '''typedef struct MinicCoreCallee {
    char *name;
    size_t name_length;
    MinicType return_type;
    MinicType *parameter_types;
    size_t parameter_count;
} MinicCoreCallee;

typedef struct MinicCoreInlineAsm {
    char *template_text;
    size_t template_length;
    bool is_volatile;
    bool has_memory_clobber;
} MinicCoreInlineAsm;
''',
    "Core opaque inline-asm payload type",
)
replace_once(
    "src/core/core_ir.h",
    '''        struct {
            MinicCoreCalleeId callee_id;
            size_t argument_begin;
            size_t argument_count;
        } call;
''',
    '''        MinicCoreInlineAsmId inline_asm_id;
        struct {
            MinicCoreCalleeId callee_id;
            size_t argument_begin;
            size_t argument_count;
        } call;
''',
    "Core instruction inline-asm payload",
)
replace_once(
    "src/core/core_ir.h",
    '''    MinicCoreCallee *callees;
    size_t callee_count;
    size_t callee_capacity;
    MinicCoreValueId *call_arguments;
''',
    '''    MinicCoreCallee *callees;
    size_t callee_count;
    size_t callee_capacity;
    MinicCoreInlineAsm *inline_asms;
    size_t inline_asm_count;
    size_t inline_asm_capacity;
    MinicCoreValueId *call_arguments;
''',
    "Core function inline-asm arena",
)
replace_once(
    "src/core/core_ir.h",
    '''bool minic_core_function_append_call_arguments(MinicCoreFunction *function,
''',
    '''bool minic_core_function_add_opaque_inline_asm(MinicCoreFunction *function,
                                                      const char *template_text,
                                                      size_t template_length,
                                                      bool is_volatile,
                                                      bool has_memory_clobber,
                                                      MinicCoreInlineAsmId *inline_asm_id);
bool minic_core_function_append_call_arguments(MinicCoreFunction *function,
''',
    "Core opaque inline-asm arena API",
)

replace_once(
    "src/core/core_ir.c",
    '''    size_t block_index;
    size_t callee_index;
    size_t global_index;
''',
    '''    size_t block_index;
    size_t callee_index;
    size_t global_index;
    size_t inline_asm_index;
''',
    "Core destroy inline-asm index",
)
replace_once(
    "src/core/core_ir.c",
    '''    for (global_index = 0U; global_index < function->global_count; ++global_index) {
        free(function->globals[global_index].name);
    }
    free(function->name);
''',
    '''    for (global_index = 0U; global_index < function->global_count; ++global_index) {
        free(function->globals[global_index].name);
    }
    for (inline_asm_index = 0U; inline_asm_index < function->inline_asm_count;
         ++inline_asm_index) {
        free(function->inline_asms[inline_asm_index].template_text);
    }
    free(function->name);
''',
    "Core destroy inline-asm text",
)
replace_once(
    "src/core/core_ir.c",
    '''    free(function->globals);
    free(function->callees);
    free(function->call_arguments);
''',
    '''    free(function->globals);
    free(function->callees);
    free(function->inline_asms);
    free(function->call_arguments);
''',
    "Core destroy inline-asm arena",
)

opaque_add = r'''bool minic_core_function_add_opaque_inline_asm(MinicCoreFunction *function,
                                                      const char *template_text,
                                                      size_t template_length,
                                                      bool is_volatile,
                                                      bool has_memory_clobber,
                                                      MinicCoreInlineAsmId *inline_asm_id) {
    MinicCoreInlineAsm stored;

    if (function == NULL || template_text == NULL || template_length == 0U ||
        template_length == SIZE_MAX || inline_asm_id == NULL || !is_volatile ||
        function->inline_asm_count >= (size_t)UINT32_MAX) {
        return false;
    }
    (void)memset(&stored, 0, sizeof(stored));
    stored.template_text = copy_name(template_text, template_length);
    if (stored.template_text == NULL ||
        !grow_array((void **)&function->inline_asms,
                    &function->inline_asm_capacity,
                    function->inline_asm_count,
                    sizeof(*function->inline_asms))) {
        free(stored.template_text);
        return false;
    }
    stored.template_length = template_length;
    stored.is_volatile = is_volatile;
    stored.has_memory_clobber = has_memory_clobber;
    function->inline_asms[function->inline_asm_count] = stored;
    *inline_asm_id = (MinicCoreInlineAsmId)function->inline_asm_count;
    function->inline_asm_count += 1U;
    return true;
}

'''
replace_once(
    "src/core/core_ir.c",
    '''bool minic_core_function_append_call_arguments(MinicCoreFunction *function,
''',
    opaque_add + '''bool minic_core_function_append_call_arguments(MinicCoreFunction *function,
''',
    "Core opaque inline-asm arena implementation",
)

replace_once(
    "src/core/core_ir.c",
    '''    case MINIC_CORE_INSTRUCTION_CALL: {
''',
    '''    case MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM: {
        const MinicCoreInlineAsm *inline_asm;

        if (instruction->result != MINIC_CORE_VALUE_INVALID ||
            !minic_type_is_void(instruction->type) ||
            instruction->value.inline_asm_id >= function->inline_asm_count) {
            return false;
        }
        inline_asm = &function->inline_asms[instruction->value.inline_asm_id];
        return inline_asm->template_text != NULL && inline_asm->template_length != 0U &&
               inline_asm->is_volatile;
    }
    case MINIC_CORE_INSTRUCTION_CALL: {
''',
    "Core verifier opaque inline-asm instruction",
)
replace_once(
    "src/core/core_ir.c",
    '''        !storage_shape_is_valid(
            function->callees, function->callee_count, function->callee_capacity) ||
        !storage_shape_is_valid(function->call_arguments,
''',
    '''        !storage_shape_is_valid(
            function->callees, function->callee_count, function->callee_capacity) ||
        !storage_shape_is_valid(
            function->inline_asms, function->inline_asm_count, function->inline_asm_capacity) ||
        !storage_shape_is_valid(function->call_arguments,
''',
    "Core verifier inline-asm arena shape",
)
replace_once(
    "src/core/core_ir.c",
    '''    for (index = 0U; index < function->callee_count; ++index) {
''',
    '''    for (index = 0U; index < function->inline_asm_count; ++index) {
        const MinicCoreInlineAsm *inline_asm;

        inline_asm = &function->inline_asms[index];
        if (inline_asm->template_text == NULL || inline_asm->template_length == 0U ||
            !inline_asm->is_volatile) {
            return false;
        }
    }
    for (index = 0U; index < function->callee_count; ++index) {
''',
    "Core verifier inline-asm arena entries",
)
replace_once(
    "src/core/core_ir.c",
    '''    case MINIC_CORE_INSTRUCTION_CALL: {
        const MinicCoreCallee *callee;
''',
    '''    case MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM: {
        const MinicCoreInlineAsm *inline_asm;

        if (function == NULL || instruction->value.inline_asm_id >= function->inline_asm_count) {
            return false;
        }
        inline_asm = &function->inline_asms[instruction->value.inline_asm_id];
        return fprintf(output,
                       "  asm.opaque id=%" PRIu32 "%s%s\n",
                       instruction->value.inline_asm_id,
                       inline_asm->is_volatile ? " volatile" : "",
                       inline_asm->has_memory_clobber ? " memory" : "") >= 0;
    }
    case MINIC_CORE_INSTRUCTION_CALL: {
        const MinicCoreCallee *callee;
''',
    "Core dump opaque inline-asm instruction",
)

opaque_lower = r'''static MinicCoreLowerStatus lower_opaque_inline_asm(MinicCoreLowerContext *context,
                                                         const MinicStatement *statement) {
    const MinicInlineAsm *source;
    MinicCoreInlineAsmId inline_asm_id;
    MinicCoreInstruction instruction;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || statement == NULL ||
        statement->inline_asm_id == MINIC_INLINE_ASM_INVALID) {
        return MINIC_CORE_LOWER_ERROR;
    }
    source = minic_c0_program_inline_asm(context->body->program, statement->inline_asm_id);
    if (source == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    if (!source->is_volatile || source->is_goto || source->template_text == NULL ||
        source->template_length == 0U || source->output_count != 0U || source->input_count != 0U ||
        source->label_count != 0U || source->register_clobber_count != 0U) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    if (!minic_core_function_add_opaque_inline_asm(context->function,
                                                   source->template_text,
                                                   source->template_length,
                                                   source->is_volatile,
                                                   source->has_memory_clobber,
                                                   &inline_asm_id)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM;
    instruction.span = statement->span;
    instruction.type = minic_type_void();
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.inline_asm_id = inline_asm_id;
    return minic_core_function_append_effect_instruction(
               context->function, context->block_id, &instruction)
               ? MINIC_CORE_LOWER_OK
               : MINIC_CORE_LOWER_ERROR;
}

'''
insert_before_last(
    "src/core/core_lower.c",
    "static MinicCoreLowerStatus\nlower_block(MinicCoreLowerContext *context, const MinicBlock *source_block, bool *terminated) {\n",
    opaque_lower,
    "Core opaque inline-asm lowering helper",
)
replace_once(
    "src/core/core_lower.c",
    '''            case MINIC_STATEMENT_EXPRESSION:
                status = lower_expression_statement(context, statement);
                break;
            case MINIC_STATEMENT_RETURN:
''',
    '''            case MINIC_STATEMENT_EXPRESSION:
                status = lower_expression_statement(context, statement);
                break;
            case MINIC_STATEMENT_INLINE_ASM:
                status = lower_opaque_inline_asm(context, statement);
                break;
            case MINIC_STATEMENT_RETURN:
''',
    "Core block opaque inline-asm statement route",
)

opaque_supported = r'''static bool core_opaque_inline_asm_supported(const MinicCoreFunction *function,
                                                   const MinicCoreInstruction *instruction) {
    const MinicCoreInlineAsm *inline_asm;

    if (function == NULL || instruction == NULL ||
        instruction->kind != MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM ||
        instruction->value.inline_asm_id >= function->inline_asm_count) {
        return false;
    }
    inline_asm = &function->inline_asms[instruction->value.inline_asm_id];
    return inline_asm->template_text != NULL && inline_asm->template_length != 0U &&
           inline_asm->is_volatile;
}

'''
replace_once(
    "src/target/riscv64/core_codegen.c",
    '''static bool core_instruction_supported(const MinicC0Program *program,
''',
    opaque_supported + '''static bool core_instruction_supported(const MinicC0Program *program,
''',
    "RV64 Core opaque inline-asm support query",
)
replace_once(
    "src/target/riscv64/core_codegen.c",
    '''    case MINIC_CORE_INSTRUCTION_CALL:
        if (instruction->value.call.callee_id >= function->callee_count ||
''',
    '''    case MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM:
        return core_opaque_inline_asm_supported(function, instruction);
    case MINIC_CORE_INSTRUCTION_CALL:
        if (instruction->value.call.callee_id >= function->callee_count ||
''',
    "RV64 Core opaque inline-asm support switch",
)

opaque_emit = r'''static bool emit_opaque_inline_asm(FILE *file,
                                   const MinicCoreFunction *function,
                                   const MinicCoreInstruction *instruction) {
    const MinicCoreInlineAsm *inline_asm;
    size_t index;

    if (file == NULL || !core_opaque_inline_asm_supported(function, instruction)) {
        return false;
    }
    inline_asm = &function->inline_asms[instruction->value.inline_asm_id];
    if (fprintf(file, "  ") < 0) {
        return false;
    }
    for (index = 0U; index < inline_asm->template_length; ++index) {
        if (inline_asm->template_text[index] != '%') {
            if (fputc((unsigned char)inline_asm->template_text[index], file) == EOF) {
                return false;
            }
            continue;
        }
        if (index + 1U >= inline_asm->template_length ||
            inline_asm->template_text[index + 1U] != '%') {
            return false;
        }
        if (fputc('%', file) == EOF) {
            return false;
        }
        index += 1U;
    }
    return fputc('\n', file) != EOF;
}

'''
replace_once(
    "src/target/riscv64/core_codegen.c",
    '''static bool emit_instruction(FILE *file,
''',
    opaque_emit + '''static bool emit_instruction(FILE *file,
''',
    "RV64 Core opaque inline-asm emitter",
)
replace_once(
    "src/target/riscv64/core_codegen.c",
    '''    case MINIC_CORE_INSTRUCTION_CALL:
        return emit_call(file, program, function, frame, instruction);
''',
    '''    case MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM:
        return emit_opaque_inline_asm(file, function, instruction);
    case MINIC_CORE_INSTRUCTION_CALL:
        return emit_call(file, program, function, frame, instruction);
''',
    "RV64 Core opaque inline-asm emit switch",
)

write_new(
    "tests/compiler/c0/core_opaque_inline_asm.c",
    r'''struct core_m18_node {
    struct core_m18_node *next;
    struct core_m18_node *prev;
};

void core_m18_fence_only(void) {
    __asm__ __volatile__("fence rw,w" : : : "memory");
}

void core_m18_release_shape(struct core_m18_node *entry) {
    (*(struct core_m18_node * volatile *)&(entry->prev)) = entry;
    __asm__ __volatile__("fence rw,w" : : : "memory");
    (*(struct core_m18_node * volatile *)&(entry->next)) = entry;
}

void core_m18_two_fences(void) {
    __asm__ __volatile__("fence rw,w" : : : "memory");
    __asm__ __volatile__("fence r,rw" : : : "memory");
}
''',
)
write_new(
    "tests/compiler/c0/core_opaque_inline_asm_runtime.c",
    r'''#include <stdio.h>

struct core_m18_node {
    struct core_m18_node *next;
    struct core_m18_node *prev;
};

void core_m18_fence_only(void);
void core_m18_release_shape(struct core_m18_node *entry);
void core_m18_two_fences(void);

int main(void) {
    struct core_m18_node node;

    node.next = 0;
    node.prev = 0;
    core_m18_fence_only();
    core_m18_release_shape(&node);
    core_m18_two_fences();
    printf("%d %d\n", node.next == &node, node.prev == &node);
    return 0;
}
''',
)
write_new(
    "tests/compiler/c0/run-core-opaque-inline-asm.sh",
    r'''#!/bin/sh
set -eu
: "${MINIC:?MINIC must point to the compiler binary}"
: "${RISCV_CC:=riscv64-linux-gnu-gcc}"
: "${QEMU_RISCV64:=qemu-riscv64}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work="${BUILD_DIR:-$root/build/core-opaque-inline-asm}"
source_file="$root/tests/compiler/c0/core_opaque_inline_asm.c"
runtime_file="$root/tests/compiler/c0/core_opaque_inline_asm_runtime.c"
mkdir -p "$work"
cc -E -P -std=gnu11 "$source_file" -o "$work/input.i"
MINIC_CORE_IR=strict "$MINIC" -S "$work/input.i" -o "$work/strict.s"
MINIC_CORE_CODEGEN=basic-v0 "$MINIC" -S "$work/input.i" -o "$work/core.s"
for symbol in core_m18_fence_only core_m18_release_shape core_m18_two_fences; do
    grep -q "^${symbol}:" "$work/core.s"
done
test "$(grep -c 'fence rw,w' "$work/core.s")" -ge 3
grep -F 'fence r,rw' "$work/core.s" >/dev/null
"$RISCV_CC" -static -O2 "$source_file" "$runtime_file" -o "$work/reference-rv64"
"$RISCV_CC" -static -O2 "$runtime_file" "$work/core.s" -o "$work/minic-rv64"
"$QEMU_RISCV64" "$work/reference-rv64" >"$work/reference.out"
"$QEMU_RISCV64" "$work/minic-rv64" >"$work/minic.out"
cmp "$work/reference.out" "$work/minic.out"
printf '%s\n' 'PASS compiler/c0/core-opaque-inline-asm'
''',
)

replace_once(
    ".github/scripts/compiler-c0-full-gate.sh",
    '''core_statement_expression_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \\
    BUILD_DIR="$root/build/ci-core-statement-expression" \\
    RISCV_CC=riscv64-linux-gnu-gcc \\
    QEMU_RISCV64=qemu-riscv64 \\
        sh tests/compiler/c0/run-core-statement-expression.sh
}

''',
    '''core_statement_expression_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \\
    BUILD_DIR="$root/build/ci-core-statement-expression" \\
    RISCV_CC=riscv64-linux-gnu-gcc \\
    QEMU_RISCV64=qemu-riscv64 \\
        sh tests/compiler/c0/run-core-statement-expression.sh
}

core_opaque_inline_asm_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \\
    BUILD_DIR="$root/build/ci-core-opaque-inline-asm" \\
    RISCV_CC=riscv64-linux-gnu-gcc \\
    QEMU_RISCV64=qemu-riscv64 \\
        sh tests/compiler/c0/run-core-opaque-inline-asm.sh
}

''',
    "C0 Core opaque inline-asm focused function registration",
)
replace_once(
    ".github/scripts/compiler-c0-full-gate.sh",
    "start_gate core-statement-expression-focused core_statement_expression_focused\n",
    "start_gate core-statement-expression-focused core_statement_expression_focused\n"
    "start_gate core-opaque-inline-asm-focused core_opaque_inline_asm_focused\n",
    "C0 Core opaque inline-asm focused gate registration",
)

print("staged M18 zero-operand opaque volatile inline asm in Core")
