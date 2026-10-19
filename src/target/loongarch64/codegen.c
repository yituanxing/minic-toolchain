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
static bool valid_int_or_pointer(MinicType type) {
    return valid_int(type) || minic_type_is_pointer(type);
}

static bool valid_return_type(MinicType type) {
    return valid_int_or_pointer(type) || minic_type_is_void(type);
}

static bool valid_int_or_bool(MinicType type) {
    return valid_int(type) || minic_type_equal(type, minic_type_bool());
}


/* Source-driven tinyAES M4 frontier. Reuse existing target DataLayout to
 * size ordinary integer globals and flat arrays, but keep ELF emission
 * in the ISA backend. Unsupported aggregate/relocation cases must fail
 * closed, rather than silently manufacture incorrect object bytes. */
static bool emit_simple_global(FILE *out, const MinicC0Program *program,
                               const MinicTargetInfo *target,
                               const MinicGlobalObject *obj) {
    MinicType scalar;
    size_t storage_size, align, width, scalar_align, i;
    const char *directive;
    if (obj == NULL || obj->name == NULL || !obj->name[0] ||
        obj->is_weak || obj->visibility != MINIC_SYMBOL_VISIBILITY_DEFAULT ||
        obj->section_name != NULL ||
        obj->alias_target != MINIC_GLOBAL_OBJECT_INVALID ||
        obj->relocation_count != 0U || obj->union_selection_count != 0U ||
        obj->flexible_array_initializer_count != 0U ||
        !minic_data_layout_global_object(target->data_layout, program, obj,
                                         &storage_size, &align)) return false;
    scalar = obj->type;
    while (minic_type_is_array(scalar)) {
        const MinicArrayType *array =
            minic_c0_program_array_type(program, scalar.array_type_id);
        if (array == NULL || array->element_count == 0U) return false;
        scalar = array->element_type;
    }
    if (!minic_type_is_integer(scalar) ||
        !minic_data_layout_type(target->data_layout, program, scalar,
                                &width, &scalar_align) ||
        (width != 1U && width != 2U && width != 4U && width != 8U) ||
        width > storage_size ||
        obj->initializer_count > storage_size / width ||
        (obj->initializer_count > 0U && obj->initializer_values == NULL))
        return false;
    (void)scalar_align;
    directive = width == 1U ? ".byte" : width == 2U ? ".2byte" :
                width == 4U ? ".4byte" : ".8byte";
    if (fprintf(out, "%s\n", obj->is_read_only ? ".section .rodata" :
                ((obj->is_zero_initialized || obj->is_tentative) &&
                 obj->initializer_count == 0U ? ".bss" : ".data")) < 0)
        return false;
    if (!obj->is_internal && fprintf(out, ".globl %s\n", obj->name) < 0)
        return false;
    if (fprintf(out, "  .balign %zu\n.type %s,@object\n%s:\n",
                align, obj->name, obj->name) < 0) return false;
    if (obj->initializer_count == 0U) {
        if (fprintf(out, "  .zero %zu\n", storage_size) < 0) return false;
    } else {
        for (i = 0U; i < obj->initializer_count; ++i) {
            uint64_t bits = obj->initializer_values[i];
            if (width < 8U)
                bits &= (UINT64_C(1) << (8U * width)) - UINT64_C(1);
            if (fprintf(out, "  %s %" PRIu64 "\n", directive, bits) < 0)
                return false;
        }
        if (storage_size > obj->initializer_count * width &&
            fprintf(out, "  .zero %zu\n",
                    storage_size - obj->initializer_count * width) < 0)
            return false;
    }
    return fprintf(out, ".size %s,%zu\n", obj->name, storage_size) >= 0;
}

static bool emit_function(FILE *out, const MinicFunction *decl,
                          const MinicCoreFunction *fn, size_t id,
                          bool function_sections) {
    const char *name = minic_c0_function_symbol_name(decl);
    const MinicCoreBlock *block;
    size_t frame, i, values_bytes;
    bool has_direct_calls = false;
    if (name == NULL || name[0] == '\0' ||
        !minic_core_function_verify(fn) ||
        decl->is_constructor || decl->is_weak ||
        decl->visibility != MINIC_SYMBOL_VISIBILITY_DEFAULT ||
        decl->section_name != NULL || decl->alias_target != MINIC_FUNCTION_INVALID ||
        !valid_return_type(fn->return_type) ||
        fn->block_count == 0U || fn->block_count > 128U || fn->entry_block != 0U ||
        fn->inline_asm_count != 0U || fn->parameter_count > 10U ||
        fn->value_count > 240U || fn->object_count > 240U ||
        fn->value_count > 240U - fn->object_count)
        return false;
    for (i = 0U; i < fn->parameter_count; ++i)
        if (!valid_int_or_pointer(fn->parameter_types[i])) return false;
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
                (minic_type_is_void(fn->return_type)
                 ? term->return_value != MINIC_CORE_VALUE_INVALID
                 : term->return_value == MINIC_CORE_VALUE_INVALID ||
                   term->return_value >= fn->value_count ||
                   !minic_type_equal(fn->values[term->return_value].type,
                                     fn->return_type))) return false;
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
        if (!valid_int_or_pointer(obj->type) || obj->element_count != 1U ||
            obj->explicit_alignment > 8U) return false;
    }
    /* Keep this first direct-call ABI step narrow: signed int values in
     * the first eight argument registers, no variadics or indirect calls.
     * Returning through a nested call requires preserving the ISA link
     * register in a dedicated frame slot. */
    for (i = 0U; i < fn->instruction_count; ++i) {
        const MinicCoreInstruction *ins = &fn->instructions[i];
        size_t j, begin, count;
        const MinicCoreCallee *callee;
        if (ins->kind != MINIC_CORE_INSTRUCTION_CALL) continue;
        has_direct_calls = true;
        if (ins->value.call.callee_id >= fn->callee_count ||
            ins->value.call.result_object != MINIC_CORE_OBJECT_INVALID ||
            (minic_type_is_void(ins->type)
             ? ins->result != MINIC_CORE_VALUE_INVALID
             : ins->result == MINIC_CORE_VALUE_INVALID ||
               ins->result >= fn->value_count ||
               !minic_type_equal(fn->values[ins->result].type, ins->type)) ||
            !valid_return_type(ins->type))
            return false;
        callee = &fn->callees[ins->value.call.callee_id];
        begin = ins->value.call.argument_begin;
        count = ins->value.call.argument_count;
        if (callee->name == NULL || callee->name[0] == '\0' ||
            callee->is_variadic || !valid_return_type(callee->return_type) ||
            count > 8U || callee->parameter_count != count ||
            begin > fn->call_argument_count ||
            count > fn->call_argument_count - begin)
            return false;
        for (j = 0U; j < count; ++j) {
            const MinicCoreCallArgument *arg = &fn->call_arguments[begin + j];
            if (!valid_int_or_pointer(callee->parameter_types[j]) ||
                arg->kind != MINIC_CORE_CALL_ARGUMENT_VALUE ||
                arg->value.value_id >= fn->value_count ||
                !valid_int_or_pointer(fn->values[arg->value.value_id].type) ||
                !minic_type_equal(callee->parameter_types[j],
                                  fn->values[arg->value.value_id].type))
                return false;
        }
    }
    values_bytes = fn->value_count * 8U;
    frame = ((fn->value_count + fn->object_count) * 8U +
             (has_direct_calls ? 8U : 0U) + 15U) & ~(size_t)15U;
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
    if (has_direct_calls &&
        fprintf(out, "  st.d $ra, $sp, %zu\n",
                values_bytes + fn->object_count * 8U) < 0) return false;
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
                !valid_int_or_pointer(fn->values[value].type)) return false;
            if (fprintf(out, "  ld.d $t1, $sp, %zu\n", (size_t)addr * 8U) < 0 ||
                fprintf(out, minic_type_is_pointer(fn->values[value].type)
                             ? "  ld.d $t0, $sp, %zu\n"
                             : "  ld.w $t0, $sp, %zu\n", (size_t)value * 8U) < 0 ||
                fputs(minic_type_is_pointer(fn->values[value].type)
                          ? "  st.d $t0, $t1, 0\n"
                          : "  st.w $t0, $t1, 0\n", out) == EOF) return false;
            continue;
        }
        if (ins->kind == MINIC_CORE_INSTRUCTION_CALL &&
            minic_type_is_void(ins->type)) {
            /* Verified in the call preflight: an effect-only void call
             * has no Core result slot. Fall through into the call case. */
            offset = 0U;
        } else {
            if (ins->result == MINIC_CORE_VALUE_INVALID ||
                ins->result >= fn->value_count ||
                ((ins->kind == MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS ||
                   ins->kind == MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS)
                    ? !minic_type_is_pointer(ins->type)
                    : !valid_int_or_bool(ins->type) &&
                      !minic_type_is_pointer(ins->type)))
                return false;
            offset = (size_t)ins->result * 8U;
        }
        switch (ins->kind) {
        case MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT:
            if (ins->value.integer_value < INT32_MIN ||
                ins->value.integer_value > INT32_MAX) return false;
            if (fprintf(out, "  li.w $t0, %" PRId64 "\n", ins->value.integer_value) < 0) return false;
            break;
        case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS:
            if (ins->value.global_id >= fn->global_count ||
                fn->globals[ins->value.global_id].name == NULL ||
                fprintf(out, "  la.global $t0, %s\n"
                             "  st.d $t0, $sp, %zu\n",
                        fn->globals[ins->value.global_id].name, offset) < 0)
                return false;
            continue;
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
                fputs(minic_type_is_pointer(ins->type)
                          ? "  ld.d $t0, $t1, 0\n"
                          : "  ld.w $t0, $t1, 0\n", out) == EOF)
                return false;
            break;
        }
        case MINIC_CORE_INSTRUCTION_CALL: {
            const MinicCoreCallee *callee = &fn->callees[ins->value.call.callee_id];
            size_t j;
            for (j = 0U; j < ins->value.call.argument_count; ++j) {
                const MinicCoreCallArgument *arg =
                    &fn->call_arguments[ins->value.call.argument_begin + j];
                if (fprintf(out,
                            minic_type_is_pointer(callee->parameter_types[j])
                                ? "  ld.d $a%zu, $sp, %zu\n"
                                : "  ld.w $a%zu, $sp, %zu\n",
                            j, (size_t)arg->value.value_id * 8U) < 0)
                    return false;
            }
            if (fprintf(out, "  bl %s\n", callee->name) < 0)
                return false;
            if (minic_type_is_void(ins->type)) continue;
            if (fputs("  or $t0, $a0, $zero\n", out) == EOF)
                return false;
            break;
        }
        case MINIC_CORE_INSTRUCTION_PARAMETER: {
            size_t index = (size_t)ins->value.parameter_index;
            if (index >= fn->parameter_count) return false;
            if (index < 8U) {
                if (fprintf(out, minic_type_is_pointer(ins->type)
                                 ? "  st.d $a%zu, $sp, %zu\n"
                                 : "  st.w $a%zu, $sp, %zu\n",
                            index, offset) < 0)
                    return false;
            } else {
                /* LoongArch lp64d: eight integer argument registers; the
                 * ninth and tenth int use 8-byte caller stack slots.
                 * Stack slots are addressed after allocating our frame.
                 * The largest offset is 2016+8, within signed imm12. */
                size_t incoming = frame + (index - 8U) * 8U;
                if (incoming > 2047U ||
                    fprintf(out, minic_type_is_pointer(ins->type)
                                 ? "  ld.d $t0, $sp, %zu\n  st.d $t0, $sp, %zu\n"
                                 : "  ld.w $t0, $sp, %zu\n  st.w $t0, $sp, %zu\n",
                            incoming, offset) < 0)
                    return false;
            }
            continue;
        }
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
        if (fprintf(out, minic_type_is_pointer(ins->type)
                             ? "  st.d $t0, $sp, %zu\n"
                             : "  st.w $t0, $sp, %zu\n", offset) < 0) return false;
    }
        switch (block->terminator.kind) {
        case MINIC_CORE_TERMINATOR_RETURN:
            if ((!minic_type_is_void(fn->return_type) &&
                 fprintf(out, minic_type_is_pointer(fn->return_type)
                                  ? "  ld.d $a0, $sp, %zu\n"
                                  : "  ld.w $a0, $sp, %zu\n",
                         (size_t)block->terminator.return_value * 8U) < 0) ||
                (has_direct_calls && fprintf(out, "  ld.d $ra, $sp, %zu\n",
                                             values_bytes + fn->object_count * 8U) < 0) ||
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
    if (program->file_asm_count != 0U)
        return fail(diagnostic, path, "LoongArch64: file asm not supported");
    out = fopen(path, "wb");
    if (out == NULL)
        return fail(diagnostic, path, "LoongArch64: cannot open GNU assembly output");
    for (i = 0U; i < program->global_object_count && success; ++i) {
        const MinicGlobalObject *obj = &program->global_objects[i];
        if (!obj->is_extern)
            success = emit_simple_global(out, program, target, obj);
        else if (obj->alias_target != MINIC_GLOBAL_OBJECT_INVALID)
            success = false;
        if (!success && getenv("MINIC_M1_TRACE") != NULL)
            (void)fprintf(stderr, "MINIC_M4_GLOBAL_FRONTIER arch=loongarch64 name=%s init=%zu reloc=%zu\n",
                          obj->name != NULL ? obj->name : "<unnamed>",
                          obj->initializer_count, obj->relocation_count);
    }
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
