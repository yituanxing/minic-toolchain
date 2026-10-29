#include "target/riscv32/rv32_internal.h"
#include "target/riscv/record_copy_bytes.h"

/* Load one little-endian ILP32 record word without reading past the
 * actual object extent. Each record Core argument is already a separate
 * snapshot object, so large by-reference calls retain C by-value semantics. */
bool rv32_emit_object_word(FILE *out, const Rv32Frame *fr,
                           MinicCoreObjectId object_id, size_t record_size,
                           unsigned word, const char *reg) {
    size_t offset, available, chunk, byte;
    if (out == NULL || fr == NULL || object_id >= fr->object_count ||
        reg == NULL || word >= 2U || record_size == 0U)
        return false;
    offset = fr->object_offsets[object_id];
    /* word < 2 by verified ILP32 plan: multiplication cannot overflow. */
    if ((size_t)word * 4U >= record_size ||
        offset > SIZE_MAX - (size_t)word * 4U)
        return false;
    offset += (size_t)word * 4U;
    available = record_size - (size_t)word * 4U;
    chunk = available < 4U ? available : 4U;
    if (chunk == 4U)
        return rv32_fixed_stack(out, fr, "lw", reg, offset);
    if (!rv32_address(out, fr, "t2", offset) ||
        fprintf(out, "  li %s, 0\n", reg) < 0)
        return false;
    for (byte = 0U; byte < chunk; ++byte) {
        if (fprintf(out,
                    "  lbu t3, %zu(t2)\n"
                    "  slli t3, t3, %zu\n"
                    "  or %s, %s, t3\n",
                    byte, byte * 8U, reg, reg) < 0)
            return false;
    }
    return true;
}

/* Single checked Core call identity; one psABI staging and result path for
 * direct and indirect calls. Original call stack/argument order is preserved. */
static bool rv32_emit_scalar_call_arguments(
    FILE *out, const MinicCoreFunction *f, const Rv32Frame *fr,
    const Rv32CallAbiPlan *plan) {
    const MinicCoreCallView *view;
    size_t pass, i;
    if (plan == NULL || !plan->prepared) return false;
    view = &plan->view;
    for (pass = 0U; pass < 2U; ++pass) {
        for (i = 0U; i < view->argument_count; ++i) {
            const MinicCoreCallArgument *arg = &view->arguments[i];
            const Rv32AbiWordLocation *entry = &plan->arguments[i];
            const MinicRiscv32AbiPlacement loc = entry->location;
            const char *reg[2] = {"t0", "t1"};
            const unsigned words = entry->words;
            size_t word;
            if (words == 0U && entry->record_size == 0U &&
                arg->kind == MINIC_CORE_CALL_ARGUMENT_OBJECT)
                continue;
            if (words == 0U || words > 2U)
                return false;
            if ((pass == 0U && loc.stack_count == 0U) ||
                (pass == 1U && loc.register_count == 0U)) continue;
            if (arg->kind == MINIC_CORE_CALL_ARGUMENT_VALUE) {
                if (arg->value.value_id >= f->value_count ||
                    entry->record_size != 0U)
                    return false;
                if (words == 2U) {
                    if (!rv32_pair_slot(out, fr, arg->value.value_id,
                                        reg[0], reg[1], true)) return false;
                } else if (!rv32_slot(out, fr, arg->value.value_id,
                                       reg[0], true)) return false;
            } else if (arg->kind == MINIC_CORE_CALL_ARGUMENT_OBJECT &&
                       entry->record_size != 0U) {
                for (word = 0U; word < words; ++word) {
                    if (entry->indirect_record) {
                        if (word != 0U ||
                            !rv32_address(out, fr, reg[0],
                                fr->object_offsets[arg->value.object_id]))
                            return false;
                    } else if (!rv32_emit_object_word(
                                   out, fr, arg->value.object_id,
                                   entry->record_size, (unsigned)word,
                                   reg[word])) {
                        return false;
                    }
                }
            } else {
                return false;
            }
            for (word = 0U; word < words; ++word) {
                if (word < loc.register_count) {
                    if (pass == 1U &&
                        !rv32_outgoing_word(out, fr,
                                           loc.register_begin + word,
                                           reg[word])) return false;
                } else if (pass == 0U &&
                           (loc.stack_begin + word - loc.register_count >=
                                fr->outgoing_size / 4U ||
                            !rv32_stack(out, "sw", reg[word],
                                (loc.stack_begin + word - loc.register_count)
                                * 4U))) return false;
            }
        }
    }
    return true;
}
bool rv32_emit_call(FILE *out, const MinicCoreFunction *f,
                    const Rv32Frame *fr,
                    const MinicCoreInstruction *ins,
                    MinicCoreInstructionId id) {
    const Rv32CallAbiPlan *plan;
    const MinicCoreCallView *view;
    MinicCoreObjectId result_object;
    unsigned words;
    if (out == NULL || f == NULL || fr == NULL || ins == NULL ||
        (ins->kind != MINIC_CORE_INSTRUCTION_CALL &&
         ins->kind != MINIC_CORE_INSTRUCTION_INDIRECT_CALL) ||
        (size_t)id >= f->instruction_count ||
        ins != &f->instructions[id])
        return false;
    plan = rv32_function_abi_call(&fr->abi, id);
    if (plan == NULL) return false;
    view = &plan->view;
    result_object = ins->kind == MINIC_CORE_INSTRUCTION_CALL
        ? ins->value.call.result_object : ins->value.indirect_call.result_object;
    if (plan->stack_words > fr->outgoing_size / 4U ||
        !rv32_emit_scalar_call_arguments(out, f, fr, plan))
        return false;
    if (plan->indirect_return) {
        /* The receiver's Core result object is the ABI-hidden sret target.
         * Argument placement was already shifted past a0 by the same plan. */
        if (result_object >= fr->object_count ||
            !rv32_address(out, fr, "a0",
                fr->object_offsets[result_object]))
            return false;
    }
    if (ins->kind == MINIC_CORE_INSTRUCTION_CALL) {
        if (view->callee == NULL || view->callee->name == NULL ||
            fprintf(out, "  call %s\n", view->callee->name) < 0)
            return false;
    } else {
        if (view->signature == NULL ||
            !rv32_slot(out, fr, view->function_pointer, "t0", true) ||
            fprintf(out, "  jalr ra, 0(t0)\n") < 0)
            return false;
    }
    if (plan->record_return_size != 0U) {
        size_t obj, word;
        if (result_object >= fr->object_count ||
            ins->result != MINIC_CORE_VALUE_INVALID)
            return false;
        if (plan->indirect_return) return true;
        obj = result_object;
        for (word = 0U; word < (plan->record_return_size + 3U) / 4U; ++word) {
            const char *reg = word == 0U ? "a0" : "a1";
            size_t remaining = plan->record_return_size - word * 4U;
            size_t dest = fr->object_offsets[obj] + word * 4U;
            size_t byte;
            if (remaining >= 4U) {
                if (!rv32_fixed_stack(out, fr, "sw", reg, dest))
                    return false;
            } else {
                if (!rv32_address(out, fr, "t2", dest))
                    return false;
                for (byte = 0U; byte < remaining; ++byte) {
                    if (fprintf(out,
                                "  srli t1, %s, %zu\n"
                                "  sb t1, %zu(t2)\n",
                                reg, byte * 8U, byte) < 0)
                        return false;
                }
            }
        }
        return true;
    }
    if (ins->result == MINIC_CORE_VALUE_INVALID) return true;
    if (!rv32_scalar_storage_words(f, ins->type, &words)) return false;
    if (words == 2U)
        return rv32_pair_slot(out, fr, ins->result, "a0", "a1", false);
    return rv32_canon(out, f, ins->type, "a0") &&
           rv32_slot(out, fr, ins->result, "a0", false);
}

/* Core PARAMETER_OBJECT holds the callee's by-value record copy.
 * Incoming registers/stack contain up to two value words or a pointer to
 * the caller's snapshot when the record is larger than 2*XLEN. */
bool rv32_emit_parameter_object(FILE *out, const MinicCoreFunction *f,
                                const Rv32Frame *fr,
                                const MinicCoreInstruction *ins) {
    size_t idx, object_id, size, word;
    const Rv32AbiWordLocation *entry;
    const MinicRiscv32AbiPlacement *loc;
    if (out == NULL || f == NULL || fr == NULL || ins == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT ||
        fr->abi.parameters == NULL)
        return false;
    idx = ins->value.parameter_object.parameter_index;
    object_id = ins->value.parameter_object.object_id;
    if (idx >= fr->abi.parameter_count ||
        object_id >= fr->object_count || idx >= f->parameter_count ||
        !minic_type_is_record(f->parameter_types[idx]))
        return false;
    entry = &fr->abi.parameters[idx];
    loc = &entry->location;
    size = entry->record_size;
    if (size == 0U && entry->words == 0U) return true;
    if (size == 0U || entry->words < 1U || entry->words > 2U ||
        entry->words != loc->register_count + loc->stack_count)
        return false;
    if (entry->indirect_record) {
        size_t in = loc->register_count != 0U
            ? loc->register_begin : 8U + loc->stack_begin;
        return entry->words == 1U &&
               rv32_incoming_word(out, fr, in, "t1") &&
               rv32_address(out, fr, "t0", fr->object_offsets[object_id]) &&
               minic_riscv_emit_record_copy_bytes(out, "t0", "t1", size);
    }
    for (word = 0U; word < entry->words; ++word) {
        size_t in = word < loc->register_count
            ? loc->register_begin + word
            : 8U + loc->stack_begin + word - loc->register_count;
        size_t offset, chunk, byte;
        if (fr->object_offsets[object_id] > SIZE_MAX - word * 4U ||
            word * 4U >= size)
            return false;
        offset = fr->object_offsets[object_id] + word * 4U;
        chunk = size - word * 4U;
        if (chunk > 4U) chunk = 4U;
        if (!rv32_incoming_word(out, fr, in, "t0"))
            return false;
        if (chunk == 4U) {
            if (!rv32_fixed_stack(out, fr, "sw", "t0", offset)) return false;
        } else {
            if (!rv32_address(out, fr, "t2", offset)) return false;
            for (byte = 0U; byte < chunk; ++byte) {
                if (fprintf(out,
                            "  srli t1, t0, %zu\n"
                            "  sb t1, %zu(t2)\n",
                            byte * 8U, byte) < 0)
                    return false;
            }
        }
    }
    return true;
}

/* Record return ABI transport lives next to argument transport, not
 * in the top-level machine-program orchestrator. The hidden sret pointer
 * is saved by the frame prologue before any nested calls can clobber a0. */
bool rv32_emit_record_return(FILE *out, const Rv32Frame *fr,
                             const MinicCoreTerminator *term,
                             const char *symbol) {
    MinicCoreObjectId id;
    size_t word;
    if (out == NULL || fr == NULL || term == NULL || symbol == NULL ||
        fr->abi.record_return_size == 0U ||
        term->kind != MINIC_CORE_TERMINATOR_RETURN)
        return false;
    id = term->return_object;
    if (id >= fr->object_count) return false;
    if (fr->abi.indirect_return) {
        return rv32_fixed_stack(out, fr, "lw", "t1", fr->sret_offset) &&
               rv32_address(out, fr, "t0", fr->object_offsets[id]) &&
               minic_riscv_emit_record_copy_bytes(
                   out, "t1", "t0", fr->abi.record_return_size) &&
               rv32_fixed_stack(out, fr, "lw", "a0", fr->sret_offset) &&
               fprintf(out, "  lla t6, .L%s_rv32_return\n"
                            "  jalr zero, 0(t6)\n", symbol) >= 0;
    }
    for (word = 0U; word < (fr->abi.record_return_size + 3U) / 4U; ++word) {
        if (!rv32_emit_object_word(
                out, fr, id, fr->abi.record_return_size, (unsigned)word,
                word == 0U ? "a0" : "a1"))
            return false;
    }
    return fprintf(out,
                   "  lla t6, .L%s_rv32_return\n"
                   "  jalr zero, 0(t6)\n", symbol) >= 0;
}

/* Named ILP32 parameters use GPRs then XLEN-sized incoming stack words.
 * Variadic definitions remain fail-closed until the ILP32 va_list ABI
 * receiver has independent GCC differential coverage. */
/* The incoming named-argument plan is shared with frame planning and
 * direct/indirect outgoing calls: no per-PARAMETER prefix re-scan. */
bool rv32_emit_parameter(FILE *out, const MinicCoreFunction *f,
                         const Rv32Frame *fr,
                         const MinicCoreInstruction *ins) {
    const Rv32AbiWordLocation *entry;
    const MinicRiscv32AbiPlacement *loc;
    const char *reg[2] = {"t0", "t1"};
    size_t idx, word;
    if (out == NULL || f == NULL || fr == NULL || ins == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_PARAMETER)
        return false;
    idx = ins->value.parameter_index;
    if (idx >= f->parameter_count || idx >= fr->abi.parameter_count ||
        fr->abi.parameters == NULL)
        return false;
    entry = &fr->abi.parameters[idx];
    loc = &entry->location;
    if (entry->words == 0U || entry->words > 2U ||
        loc->register_count + loc->stack_count != entry->words)
        return false;
    for (word = 0U; word < entry->words; ++word) {
        size_t incoming;
        if (word < loc->register_count) {
            incoming = loc->register_begin + word;
        } else {
            size_t slot = loc->stack_begin + word - loc->register_count;
            if (slot > SIZE_MAX - 8U) return false;
            incoming = 8U + slot;
        }
        if (!rv32_incoming_word(out, fr, incoming, reg[word]))
            return false;
    }
    if (entry->words == 2U)
        return rv32_pair_slot(out, fr, ins->result,
                              reg[0], reg[1], false);
    return rv32_canon(out, f, ins->type, reg[0]) &&
           rv32_slot(out, fr, ins->result, reg[0], false);
}
