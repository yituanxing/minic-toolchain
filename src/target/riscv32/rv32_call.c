#include "target/riscv32/rv32_internal.h"
#include "target/riscv32/rv32_abi_cursor.h"
#include "target/common/core_call_view.h"

/* Single checked Core call identity; one psABI staging and result path for
 * direct and indirect calls. Original call stack/argument order is preserved. */
static bool rv32_emit_scalar_call_arguments(
    FILE *out, const MinicCoreFunction *f, const Rv32Frame *fr,
    const MinicCoreCallView *view) {
    size_t pass, i;
    if (view == NULL) return false;
    for (pass = 0U; pass < 2U; ++pass) {
        MinicRiscv32AbiCursor cursor = {0U, 0U, false};
        for (i = 0U; i < view->argument_count; ++i) {
            const MinicCoreCallArgument *arg = &view->arguments[i];
            MinicRiscv32AbiPlacement loc;
            const char *reg[2] = {"t0", "t1"};
            unsigned words;
            size_t word;
            if (arg->kind != MINIC_CORE_CALL_ARGUMENT_VALUE ||
                arg->value.value_id >= f->value_count ||
                !rv32_scalar_storage_words(
                    f, f->values[arg->value.value_id].type, &words) ||
                !minic_riscv32_abi_place(
                    &cursor, words,
                    view->is_variadic && i >= view->parameter_count, &loc))
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
                    const MinicCoreInstruction *ins) {
    MinicCoreCallView view;
    unsigned words;
    if (out == NULL || f == NULL || fr == NULL || ins == NULL ||
        (ins->kind != MINIC_CORE_INSTRUCTION_CALL &&
         ins->kind != MINIC_CORE_INSTRUCTION_INDIRECT_CALL) ||
        (ins->kind == MINIC_CORE_INSTRUCTION_CALL &&
         ins->value.call.result_object != MINIC_CORE_OBJECT_INVALID) ||
        !minic_core_call_view_build(f, ins, &view))
        return false;

    if (!rv32_emit_scalar_call_arguments(out, f, fr, &view)) return false;
    if (ins->kind == MINIC_CORE_INSTRUCTION_CALL) {
        if (view.callee == NULL || view.callee->name == NULL ||
            fprintf(out, "  call %s\n", view.callee->name) < 0)
            return false;
    } else {
        if (view.signature == NULL ||
            !rv32_slot(out, fr, view.function_pointer, "t0", true) ||
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
bool rv32_emit_parameter(FILE *out, const MinicCoreFunction *f,
                         const Rv32Frame *fr,
                         const MinicCoreInstruction *ins) {
    size_t idx;
    if (out == NULL || f == NULL || fr == NULL || ins == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_PARAMETER) return false;
    switch (ins->kind) {
    case MINIC_CORE_INSTRUCTION_PARAMETER: {
        size_t word = 0U;
        unsigned words;
        idx = ins->value.parameter_index;
        if (idx >= f->parameter_count ||
            !rv32_scalar_storage_words(f, ins->type, &words)) return false;
        for (size_t j = 0U; j < idx; ++j) {
            unsigned prior;
            if (!rv32_scalar_storage_words(f, f->parameter_types[j], &prior))
                return false;
            word += prior;
        }
        if (!rv32_incoming_word(out, fr, word, "t0")) return false;
        if (words == 2U)
            return rv32_incoming_word(out, fr, word + 1U, "t1") &&
                   rv32_pair_slot(out, fr, ins->result, "t0", "t1", false);
        return rv32_canon(out, f, ins->type, "t0") &&
               rv32_slot(out, fr, ins->result, "t0", false);
    }
    default:
        return false;
    }
}
