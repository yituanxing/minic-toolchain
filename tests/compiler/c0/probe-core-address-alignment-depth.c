#include "core/address_alignment.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* The former Loong implementation, retained only as an independent unit
 * oracle during migration. Actual object/global DataLayout root decisions
 * remain in the machine callback; this verifies Common's exact propagation
 * and historical recursion budget, not a guessed target ABI. */
static size_t legacy_loong_alignment(
    const MinicCoreFunction *fn, MinicCoreValueId value_id,
    MinicCoreAddressRootAlignment root, void *context, unsigned depth) {
    const MinicCoreInstruction *origin;
    size_t base, offset;
    if (fn == NULL || value_id >= fn->value_count || depth > 16U ||
        fn->values == NULL || fn->instructions == NULL ||
        fn->values[value_id].definition >= fn->instruction_count)
        return 1U;
    origin = &fn->instructions[fn->values[value_id].definition];
    if (origin->result != value_id) return 1U;
    switch (origin->kind) {
    case MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS:
    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS:
        return root(fn, origin, context);
    case MINIC_CORE_INSTRUCTION_FIELD_ADDRESS:
        base = legacy_loong_alignment(
            fn, origin->value.field_address.base, root, context, depth + 1U);
        offset = origin->value.field_address.byte_offset;
        break;
    case MINIC_CORE_INSTRUCTION_POINTER_OFFSET:
        base = legacy_loong_alignment(
            fn, origin->value.pointer_offset.base, root, context, depth + 1U);
        offset = origin->value.pointer_offset.element_size;
        break;
    default:
        return 1U;
    }
    while (offset != 0U) {
        const size_t remainder = base % offset;
        base = offset;
        offset = remainder;
    }
    return base == 0U ? 1U : base;
}

static size_t oracle_root(const MinicCoreFunction *fn,
                          const MinicCoreInstruction *origin, void *context) {
    (void)fn;
    (void)context;
    switch (origin->kind) {
    case MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS: return 16U;
    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS: return 8U;
    default: return 1U;
    }
}

int main(void) {
    MinicCoreFunction fn = {0};
    MinicCoreValue values[68];
    MinicCoreInstruction instructions[68];
    size_t depth;
    unsigned root;
    memset(values, 0, sizeof(values));
    memset(instructions, 0, sizeof(instructions));
    fn.values = values;
    fn.instructions = instructions;
    for (depth = 0U; depth < 68U; ++depth) {
        values[depth].definition = (MinicCoreInstructionId)depth;
        instructions[depth].result = (MinicCoreValueId)depth;
    }
    for (root = 0U; root < 2U; ++root) {
        instructions[0].kind = root == 0U
            ? MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS
            : MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS;
        instructions[0].value.object_id = 0U;
        for (depth = 0U; depth <= 67U; ++depth) {
            size_t before, after, default_depth, capped_depth;
            size_t i;
            fn.value_count = depth + 1U;
            fn.instruction_count = depth + 1U;
            for (i = 1U; i <= depth; ++i) {
                instructions[i].kind = (i & 1U)
                    ? MINIC_CORE_INSTRUCTION_FIELD_ADDRESS
                    : MINIC_CORE_INSTRUCTION_POINTER_OFFSET;
                if (i & 1U) {
                    instructions[i].value.field_address.base =
                        (MinicCoreValueId)(i - 1U);
                    instructions[i].value.field_address.byte_offset = 8U;
                } else {
                    instructions[i].value.pointer_offset.base =
                        (MinicCoreValueId)(i - 1U);
                    instructions[i].value.pointer_offset.element_size = 8U;
                }
            }
            before = legacy_loong_alignment(
                &fn, (MinicCoreValueId)depth, oracle_root, NULL, 0U);
            after = minic_core_address_known_alignment_bounded(
                &fn, (MinicCoreValueId)depth, oracle_root, NULL, 16U);
            default_depth = minic_core_address_known_alignment(
                &fn, (MinicCoreValueId)depth, oracle_root, NULL);
            capped_depth = minic_core_address_known_alignment_bounded(
                &fn, (MinicCoreValueId)depth, oracle_root, NULL, 999U);
            if (before != after ||
                capped_depth != default_depth ||
                (depth <= 16U && default_depth != before) ||
                (depth == 17U && (after != 1U || default_depth != 8U)) ||
                (depth >= 65U && default_depth != 1U)) {
                fprintf(stderr,
                    "CORE_ALIGNMENT_DIFFERENTIAL_FAIL root=%u depth=%zu "
                    "legacy=%zu bounded=%zu default=%zu\n",
                    root, depth, before, after, default_depth);
                return 1;
            }
        }
    }

    /* Invalid definitions and non-address producers never infer alignment. */
    fn.value_count = 1U;
    fn.instruction_count = 1U;
    instructions[0].kind = MINIC_CORE_INSTRUCTION_PARAMETER;
    if (minic_core_address_known_alignment_bounded(
            &fn, 0U, oracle_root, NULL, 16U) != 1U)
        return 2;
    instructions[0].kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
    instructions[0].result = MINIC_CORE_VALUE_INVALID;
    if (minic_core_address_known_alignment_bounded(
            &fn, 0U, oracle_root, NULL, 16U) != 1U)
        return 3;
    instructions[0].result = 0U;
    values[0].definition = 1U;
    if (minic_core_address_known_alignment_bounded(
            &fn, 0U, oracle_root, NULL, 16U) != 1U)
        return 4;

    puts("MINIC_CORE_ADDRESS_ALIGNMENT_DEPTH=PASS "
         "loong-legacy=bounded16 default64 roots=object,global depths=0..67");
    return 0;
}
