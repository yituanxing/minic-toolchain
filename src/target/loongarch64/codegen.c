#include "target/loongarch64/codegen.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* M1/M2: prove correct *actual* GNU ELF/ABI assembly, not just target naming.
 * Each signed 32-bit Core SSA value gets an 8-byte private stack slot.
 * Scalar int parameters, arithmetic, and basic block branches are admitted.
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

/* Comparisons are Core booleans, not 32-bit C int. Both still fit our
 * private 8-byte SSA slots, with canonical 0/1 in the low 32 bits. */
static bool valid_int_or_bool(MinicType type) {
    return valid_int(type) || minic_type_equal(type, minic_type_bool());
}

static bool emit_function(FILE *out, const MinicFunction *decl,
                          const MinicCoreFunction *fn, size_t id,
                          bool function_sections) {
    const char *name = minic_c0_function_symbol_name(decl);
    const MinicCoreBlock *block;
    size_t frame, i, values_bytes;
    if (name == NULL || name[0] == '\0' ||
        !minic_core_function_verify(fn) ||
        decl->is_constructor || decl->is_weak ||
        decl->visibility != MINIC_SYMBOL_VISIBILITY_DEFAULT ||
        decl->section_name != NULL || decl->alias_target != MINIC_FUNCTION_INVALID ||
        !valid_int(fn->return_type) ||
        fn->block_count == 0U || fn->block_count > 128U || fn->entry_block != 0U ||
        fn->inline_asm_count != 0U || fn->parameter_count > 8U ||
        fn->value_count > 240U || fn->object_count > 240U ||
        fn->value_count > 240U - fn->object_count)
        return false;
    for (i = 0U; i < fn->parameter_count; ++i)
        if (!valid_int(fn->parameter_types[i])) return false;
    /* M2: verify every CFG successor and return before emitting anything.
     * A function's SSA values and local objects retain unique stack slots
     * across blocks, so no architecture-independent register allocator is
     * needed to implement basic control-flow correctly. */
    for (i = 0U; i < fn->block_count; ++i) {
        const MinicCoreTerminator *term = &fn->blocks[i].terminator;
        if (!fn->blocks[i].has_terminator) return false;
        switch (term->kind) {
        case MINIC_CORE_TERMINATOR_RETURN:
            if (term->return_object != MINIC_CORE_OBJECT_INVALID ||
                term->return_value == MINIC_CORE_VALUE_INVALID ||
                term->return_value >= fn->value_count ||
                !valid_int(fn->values[term->return_value].type)) return false;
            break;
        case MINIC_CORE_TERMINATOR_BRANCH:
            if (term->branch_target >= fn->block_count) return false;
            break;
        case MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH:
            if (term->conditional.when_true >= fn->block_count ||
                term->conditional.when_false >= fn->block_count ||
                term->conditional.condition >= fn->value_count ||
                !valid_int_or_bool(fn->values[term->conditional.condition].type))
                return false;
            break;
        default:
            return false;
        }
    }
    for (i = 0U; i < fn->object_count; ++i) {
        const MinicCoreObject *obj = &fn->objects[i];
        if (!valid_int(obj->type) || obj->element_count != 1U ||
            obj->explicit_alignment > 8U) return false;
    }
    values_bytes = fn->value_count * 8U;
    frame = ((fn->value_count + fn->object_count) * 8U + 15U) & ~(size_t)15U;
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
    for (size_t block_id = 0U; block_id < fn->block_count; ++block_id) {
        block = &fn->blocks[block_id];
        if (fprintf(out, ".Lmini_loong_f%zu_b%zu:\n", id, block_id) < 0)
            return false;
        for (i = 0U; i < block->instruction_count; ++i) {
        MinicCoreInstructionId ins_id = block->instructions[i];
        const MinicCoreInstruction *ins;
        size_t offset;
        if (ins_id >= fn->instruction_count) return false;
        ins = &fn->instructions[ins_id];
        if (ins->kind == MINIC_CORE_INSTRUCTION_STORE) {
            const MinicCoreValueId addr = ins->value.store.address;
            const MinicCoreValueId value = ins->value.store.stored_value;
            if (ins->result != MINIC_CORE_VALUE_INVALID ||
                addr >= fn->value_count || value >= fn->value_count ||
                !minic_type_is_pointer(fn->values[addr].type) ||
                !valid_int(fn->values[value].type)) return false;
            if (fprintf(out, "  ld.d $t1, $sp, %zu\n", (size_t)addr * 8U) < 0 ||
                fprintf(out, "  ld.w $t0, $sp, %zu\n", (size_t)value * 8U) < 0 ||
                fputs("  st.w $t0, $t1, 0\n", out) == EOF) return false;
            continue;
        }
        if (ins->result == MINIC_CORE_VALUE_INVALID ||
            ins->result >= fn->value_count ||
            (ins->kind == MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS
                ? !minic_type_is_pointer(ins->type)
                : !valid_int_or_bool(ins->type)))
            return false;
        offset = (size_t)ins->result * 8U;
        switch (ins->kind) {
        case MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT:
            if (ins->value.integer_value < INT32_MIN ||
                ins->value.integer_value > INT32_MAX) return false;
            if (fprintf(out, "  li.w $t0, %" PRId64 "\n", ins->value.integer_value) < 0) return false;
            break;
        case MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS:
            if (ins->value.object_id >= fn->object_count ||
                fprintf(out, "  addi.d $t0, $sp, %zu\n",
                        values_bytes + (size_t)ins->value.object_id * 8U) < 0 ||
                fprintf(out, "  st.d $t0, $sp, %zu\n", offset) < 0)
                return false;
            continue;
        case MINIC_CORE_INSTRUCTION_LOAD: {
            const MinicCoreValueId addr = ins->value.load.address;
            if (addr >= fn->value_count ||
                !minic_type_is_pointer(fn->values[addr].type) ||
                fprintf(out, "  ld.d $t1, $sp, %zu\n", (size_t)addr * 8U) < 0 ||
                fputs("  ld.w $t0, $t1, 0\n", out) == EOF)
                return false;
            break;
        }
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
        case MINIC_CORE_INSTRUCTION_INTEGER_LESS:
        case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL: {
            const MinicCoreValueId lhs = ins->value.binary.left;
            const MinicCoreValueId rhs = ins->value.binary.right;
            if (lhs >= fn->value_count || rhs >= fn->value_count ||
                !valid_int(fn->values[lhs].type) ||
                !valid_int(fn->values[rhs].type) ||
                fprintf(out, "  ld.w $t0, $sp, %zu\n", (size_t)lhs * 8U) < 0 ||
                fprintf(out, "  ld.w $t1, $sp, %zu\n", (size_t)rhs * 8U) < 0 ||
                fputs(ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_LESS
                          ? "  slt $t0, $t0, $t1\n"
                          : "  xor $t0, $t0, $t1\n  sltui $t0, $t0, 1\n", out) == EOF)
                return false;
            break;
        }
        case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO: {
            const MinicCoreValueId v = ins->value.operand;
            if (v >= fn->value_count ||
                !valid_int_or_bool(fn->values[v].type) ||
                fprintf(out, "  ld.w $t0, $sp, %zu\n", (size_t)v * 8U) < 0 ||
                fputs("  sltui $t0, $t0, 1\n", out) == EOF)
                return false;
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
        switch (block->terminator.kind) {
        case MINIC_CORE_TERMINATOR_RETURN:
            if (fprintf(out, "  ld.w $a0, $sp, %zu\n",
                        (size_t)block->terminator.return_value * 8U) < 0 ||
                (frame != 0U && fprintf(out, "  addi.d $sp, $sp, %zu\n", frame) < 0) ||
                fputs("  ret\n", out) == EOF) return false;
            break;
        case MINIC_CORE_TERMINATOR_BRANCH:
            if (fprintf(out, "  b .Lmini_loong_f%zu_b%u\n",
                        id, (unsigned int)block->terminator.branch_target) < 0)
                return false;
            break;
        case MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH:
            if (fprintf(out,
                        "  ld.w $t0, $sp, %zu\n"
                        "  bnez $t0, .Lmini_loong_f%zu_b%u\n"
                        "  b .Lmini_loong_f%zu_b%u\n",
                        (size_t)block->terminator.conditional.condition * 8U,
                        id, (unsigned int)block->terminator.conditional.when_true,
                        id, (unsigned int)block->terminator.conditional.when_false) < 0)
                return false;
            break;
        default: return false;
        }
    }
    if (fprintf(out, ".size %s,.-%s\n", name, name) < 0) return false;
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
        if (decl->is_defined && (!decl->is_internal || decl->is_referenced)) {
            success = emit_function(out, decl, &core_functions[i], i, function_sections);
            if (!success && getenv("MINIC_M1_TRACE") != NULL) {
                const MinicCoreFunction *fn = &core_functions[i];
                (void)fprintf(stderr,
                              "MINIC_M1_FRONTIER arch=loongarch64 function=%s "
                              "core_blocks=%zu core_objects=%zu parameters=%zu "
                              "values=%zu instructions=%zu\n",
                              fn->name != NULL ? fn->name : "<unnamed>",
                              fn->block_count, fn->object_count,
                              fn->parameter_count, fn->value_count,
                              fn->instruction_count);
                (void)minic_core_function_dump(stderr, fn);
            }
        }
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
