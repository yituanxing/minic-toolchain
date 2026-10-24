#include "target/riscv32/rv32_internal.h"

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
            if (arg->kind != MINIC_CORE_CALL_ARGUMENT_VALUE ||
                arg->value.value_id >= f->value_count ||
                words == 0U || words > 2U)
                return false;
            if ((pass == 0U && loc.stack_count == 0U) ||
                (pass == 1U && loc.register_count == 0U)) continue;
            if (words == 2U) {
                if (!rv32_pair_slot(out, fr, arg->value.value_id,
                                    reg[0], reg[1], true)) return false;
            } else if (!rv32_slot(out, fr, arg->value.value_id,
                                   reg[0], true)) return false;
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
    if (plan->stack_words > fr->outgoing_size / 4U ||
        !rv32_emit_scalar_call_arguments(out, f, fr, plan))
        return false;
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
    if (ins->result == MINIC_CORE_VALUE_INVALID) return true;
    if (!rv32_scalar_storage_words(f, ins->type, &words)) return false;
    if (words == 2U)
        return rv32_pair_slot(out, fr, ins->result, "a0", "a1", false);
    return rv32_canon(out, f, ins->type, "a0") &&
           rv32_slot(out, fr, ins->result, "a0", false);
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
