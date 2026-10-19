#include "target/loongarch64/codegen.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* M1: prove correct *actual* GNU ELF/ABI assembly, not just target naming.
 * Each signed 32-bit Core SSA value gets an 8-byte private stack slot.
 * Only int scalar parameters and integer operations are admitted today.
 * No implicit ABI equivalence with RISC-V64/x86-64 is assumed. */
static bool fail(MinicDiagnostic *diag, const char *path, const char *why) {
    if (diag != NULL) {
        diag->path = path;
        diag->line = 1U;
        diag->column = 1U;
        (void)snprintf(diag->message, sizeof(diag->message), "%s", why);
    }
    return false;
}

static bool valid_int(MinicType type) {
    return minic_type_equal(type, minic_type_int());
}

static bool emit_function(FILE *out, const MinicFunction *decl,
                          const MinicCoreFunction *fn, size_t id,
                          bool function_sections) {
    const char *name = minic_c0_function_symbol_name(decl);
    const MinicCoreBlock *block;
    size_t frame, i;
    if (name == NULL || name[0] == '\0' ||
        !minic_core_function_verify(fn) ||
        decl->is_constructor || decl->is_weak ||
        decl->visibility != MINIC_SYMBOL_VISIBILITY_DEFAULT ||
        decl->section_name != NULL || decl->alias_target != MINIC_FUNCTION_INVALID ||
        !valid_int(fn->return_type) ||
        fn->block_count != 1U || fn->entry_block != 0U ||
        fn->object_count != 0U || fn->inline_asm_count != 0U ||
        fn->parameter_count > 8U ||
        fn->value_count > 240U || fn->value_count > (SIZE_MAX - 15U) / 8U)
        return false;
    for (i = 0U; i < fn->parameter_count; ++i)
        if (!valid_int(fn->parameter_types[i])) return false;
    block = &fn->blocks[0];
    if (!block->has_terminator ||
        block->terminator.kind != MINIC_CORE_TERMINATOR_RETURN ||
        block->terminator.return_object != MINIC_CORE_OBJECT_INVALID ||
        block->terminator.return_value == MINIC_CORE_VALUE_INVALID ||
        block->terminator.return_value >= fn->value_count)
        return false;
    frame = (fn->value_count * 8U + 15U) & ~(size_t)15U;
    if (frame > 2016U) return false;
    if (function_sections) {
        if (fprintf(out, ".section .text.%s,\"ax\",@progbits\n", name) < 0)
            return false;
    } else if (fprintf(out, ".text\n") < 0) return false;
    if (!decl->is_internal && fprintf(out, ".globl %s\n", name) < 0)
        return false;
    if (fprintf(out, ".p2align 2\n.type %s,@function\n%s:\n", name, name) < 0)
        return false;
    if (frame != 0U && fprintf(out, "  addi.d $sp, $sp, -%zu\n", frame) < 0) return false;
    for (i = 0U; i < block->instruction_count; ++i) {
        MinicCoreInstructionId ins_id = block->instructions[i];
        const MinicCoreInstruction *ins;
        size_t offset;
        if (ins_id >= fn->instruction_count) return false;
        ins = &fn->instructions[ins_id];
        if (ins->result == MINIC_CORE_VALUE_INVALID ||
            ins->result >= fn->value_count || !valid_int(ins->type))
            return false;
        offset = (size_t)ins->result * 8U;
        switch (ins->kind) {
        case MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT:
            if (ins->value.integer_value < INT32_MIN ||
                ins->value.integer_value > INT32_MAX) return false;
            if (fprintf(out, "  li.w $t0, %" PRId64 "\n", ins->value.integer_value) < 0) return false;
            break;
        case MINIC_CORE_INSTRUCTION_PARAMETER:
            if (ins->value.parameter_index >= fn->parameter_count ||
                fprintf(out, "  st.w $a%zu, $sp, %zu\n",
                        ins->value.parameter_index, offset) < 0) return false;
            continue;
        case MINIC_CORE_INSTRUCTION_INTEGER_ADD:
        case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT:
        case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY: {
            MinicCoreValueId lhs = ins->value.binary.left;
            MinicCoreValueId rhs = ins->value.binary.right;
            const char *opcode = ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_ADD
                ? "  add.w $t0, $t0, $t1\n" :
                ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT
                    ? "  sub.w $t0, $t0, $t1\n" : "  mul.w $t0, $t0, $t1\n";
            if (lhs >= fn->value_count || rhs >= fn->value_count ||
                !valid_int(fn->values[lhs].type) ||
                !valid_int(fn->values[rhs].type) ||
                fprintf(out, "  ld.w $t0, $sp, %zu\n", (size_t)lhs * 8U) < 0 ||
                fprintf(out, "  ld.w $t1, $sp, %zu\n", (size_t)rhs * 8U) < 0 ||
                fputs(opcode, out) == EOF) return false;
            break;
        }
        case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION: {
            MinicCoreValueId src = ins->value.operand;
            if (src >= fn->value_count || !valid_int(fn->values[src].type) ||
                fprintf(out, "  ld.w $t0, $sp, %zu\n", (size_t)src * 8U) < 0)
                return false;
            break;
        }
        default:
            return false;
        }
        if (fprintf(out, "  st.w $t0, $sp, %zu\n", offset) < 0) return false;
    }
    if (fprintf(out, "  ld.w $a0, $sp, %zu\n",
                (size_t)block->terminator.return_value * 8U) < 0 ||
        (frame != 0U && fprintf(out, "  addi.d $sp, $sp, %zu\n", frame) < 0) ||
        fputs("  ret\n", out) == EOF ||
        fprintf(out, ".size %s,.-%s\n", name, name) < 0)
        return false;
    (void)id; /* Reserved function-scoped label namespace for later M2 CFG. */
    return true;
}

bool minic_loongarch64_write_c0_program_with_core_functions(
    const char *path, const MinicC0Program *program,
    const MinicTargetInfo *target, const MinicCoreFunction *core_functions,
    size_t core_function_count, bool function_sections,
    MinicDiagnostic *diagnostic) {
    size_t i;
    FILE *out;
    bool success = true;
    if (path == NULL || program == NULL || target == NULL ||
        target->architecture != MINIC_TARGET_ARCH_LOONGARCH64 ||
        (core_function_count > 0U && core_functions == NULL) ||
        core_function_count != program->function_count)
        return fail(diagnostic, path, "LoongArch64: invalid Core or target configuration");
    if (program->global_object_count != 0U || program->file_asm_count != 0U)
        return fail(diagnostic, path, "LoongArch64: global objects / file asm not yet supported");
    out = fopen(path, "wb");
    if (out == NULL)
        return fail(diagnostic, path, "LoongArch64: cannot open GNU assembly output");
    for (i = 0U; i < program->function_count && success; ++i) {
        const MinicFunction *decl = &program->functions[i];
        if (decl->is_defined && (!decl->is_internal || decl->is_referenced))
            success = emit_function(out, decl, &core_functions[i], i, function_sections);
    }
    if (success && fprintf(out, ".section .note.GNU-stack,\"\",@progbits\n") < 0)
        success = false;
    if (fclose(out) != 0) success = false;
    if (!success) {
        (void)remove(path);
        return fail(diagnostic, path,
                    "LoongArch64: Core instruction/ABI not yet supported by M1 emitter");
    }
    return true;
}
