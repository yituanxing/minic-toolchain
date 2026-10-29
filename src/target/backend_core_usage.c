#include "target/backend_core_usage.h"

#include <stdio.h>

/* Exact original opt-in census semantics and stable machine-readable output.
 * Only invoked by the front-end TU driver when census mode is requested. */
static const char *minic_backend_usage_group(MinicCoreInstructionKind kind) {
    switch (kind) {
    case MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT:
    case MINIC_CORE_INSTRUCTION_INTEGER_ADD:
    case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE:
    case MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR:
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT:
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT:
    case MINIC_CORE_INSTRUCTION_INTEGER_LESS:
    case MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW:
    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION:
    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT:
    case MINIC_CORE_INSTRUCTION_INTEGER_CLZ:
    case MINIC_CORE_INSTRUCTION_INTEGER_CTZ:
        return "integer";
    case MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT:
    case MINIC_CORE_INSTRUCTION_INTEGER_TO_DOUBLE:
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_INTEGER:
    case MINIC_CORE_INSTRUCTION_DOUBLE_ADD:
    case MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_DOUBLE_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_DOUBLE_DIVIDE:
    case MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL:
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS:
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS_EQUAL:
    case MINIC_CORE_INSTRUCTION_DOUBLE_NEGATE:
    case MINIC_CORE_INSTRUCTION_FLOAT_TO_DOUBLE:
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_FLOAT:
    case MINIC_CORE_INSTRUCTION_FLOAT_ADD:
    case MINIC_CORE_INSTRUCTION_FLOAT_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_FLOAT_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_FLOAT_DIVIDE:
        return "floating";
    case MINIC_CORE_INSTRUCTION_PARAMETER:
    case MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT:
    case MINIC_CORE_INSTRUCTION_CALL:
    case MINIC_CORE_INSTRUCTION_INDIRECT_CALL:
    case MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS:
    case MINIC_CORE_INSTRUCTION_VARIADIC_NEXT_SCALAR:
    case MINIC_CORE_INSTRUCTION_CALL_FRAME_ADDRESS:
    case MINIC_CORE_INSTRUCTION_FIXED_REGISTER_READ:
    case MINIC_CORE_INSTRUCTION_FIXED_REGISTER_WRITE:
    case MINIC_CORE_INSTRUCTION_VARIADIC_COPY:
        return "abi";
    case MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS:
    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS:
    case MINIC_CORE_INSTRUCTION_FIELD_ADDRESS:
    case MINIC_CORE_INSTRUCTION_LOAD:
    case MINIC_CORE_INSTRUCTION_STORE:
    case MINIC_CORE_INSTRUCTION_RECORD_LOAD:
    case MINIC_CORE_INSTRUCTION_RECORD_COPY:
    case MINIC_CORE_INSTRUCTION_ATOMIC_STORE:
    case MINIC_CORE_INSTRUCTION_ATOMIC_LOAD:
    case MINIC_CORE_INSTRUCTION_DYNAMIC_STACK_ALLOC:
        return "memory";
    case MINIC_CORE_INSTRUCTION_POINTER_OFFSET:
    case MINIC_CORE_INSTRUCTION_POINTER_LESS:
    case MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS:
    case MINIC_CORE_INSTRUCTION_BLOCK_ADDRESS:
        return "pointer";
    default:
        return "other";
    }
}

static unsigned int minic_backend_usage_bits(const MinicC0Program *program,
                                             const MinicTargetInfo *target,
                                             const MinicCoreFunction *fn,
                                             const MinicCoreInstruction *ins) {
    MinicType type = ins->type;
    unsigned int bits = 0U;
    size_t size = 0U;
    switch (ins->kind) {
    case MINIC_CORE_INSTRUCTION_INTEGER_LESS:
    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:
    case MINIC_CORE_INSTRUCTION_POINTER_LESS:
    case MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL:
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS:
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS_EQUAL:
        if (ins->value.binary.left < fn->value_count)
            type = fn->values[ins->value.binary.left].type;
        break;
    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION:
    case MINIC_CORE_INSTRUCTION_INTEGER_TO_DOUBLE:
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_INTEGER:
    case MINIC_CORE_INSTRUCTION_SCALAR_BITCAST:
    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT:
    case MINIC_CORE_INSTRUCTION_INTEGER_CLZ:
    case MINIC_CORE_INSTRUCTION_INTEGER_CTZ:
    case MINIC_CORE_INSTRUCTION_DOUBLE_NEGATE:
    case MINIC_CORE_INSTRUCTION_FLOAT_TO_DOUBLE:
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_FLOAT:
        if (ins->value.operand < fn->value_count)
            type = fn->values[ins->value.operand].type;
        break;
    default: break;
    }
    if (minic_type_is_integer(type))
        (void)minic_target_info_integer_width(target, program, type, &bits);
    else if (minic_type_is_pointer(type) &&
             minic_target_info_sizeof_type(target, program, type, &size))
        bits = (unsigned int)(size * 8U);
    else if (minic_type_is_float(type)) bits = 32U;
    else if (minic_type_is_double(type)) bits = 64U;
    else if (minic_type_is_float128(type)) bits = 128U;
    return bits;
}

void minic_backend_core_usage_dump(const char *arch,
                                          const MinicC0Program *program,
                                          const MinicTargetInfo *target,
                                          const MinicCoreFunction *functions,
                                          size_t function_count) {
    size_t counts[MINIC_CORE_INSTRUCTION_KIND_COUNT][4] = {{0U}};
    size_t totals[MINIC_CORE_TERMINATOR_UNREACHABLE + 1U] = {0U};
    size_t i, j, kind, bucket, n_functions = 0U, n_instructions = 0U;
    const char *const widths[4] = {"unknown", "upto32", "64", "over64"};
    for (i = 0U; i < function_count; ++i) {
        const MinicFunction *decl = &program->functions[i];
        const MinicCoreFunction *fn = &functions[i];
        if (!decl->is_defined || (decl->is_internal && !decl->is_referenced))
            continue;
        ++n_functions;
        for (j = 0U; j < fn->instruction_count; ++j) {
            const MinicCoreInstruction *ins = &fn->instructions[j];
            const unsigned int bits = minic_backend_usage_bits(
                program, target, fn, ins);
            kind = (size_t)ins->kind;
            if (kind >= (size_t)MINIC_CORE_INSTRUCTION_KIND_COUNT)
                continue;
            bucket = bits == 0U ? 0U : bits <= 32U ? 1U :
                     bits == 64U ? 2U : 3U;
            ++counts[kind][bucket];
            ++n_instructions;
        }
        for (j = 0U; j < fn->block_count; ++j) {
            const MinicCoreBlock *block = &fn->blocks[j];
            kind = (size_t)block->terminator.kind;
            if (block->has_terminator &&
                kind <= (size_t)MINIC_CORE_TERMINATOR_UNREACHABLE)
                ++totals[kind];
        }
    }
    (void)fprintf(stderr,
                  "MINIC_CORE_USAGE_SUMMARY arch=%s functions=%zu instructions=%zu support_verified=0\n",
                  arch, n_functions, n_instructions);
    for (kind = 0U; kind < (size_t)MINIC_CORE_INSTRUCTION_KIND_COUNT; ++kind)
        for (bucket = 0U; bucket < 4U; ++bucket)
            if (counts[kind][bucket] != 0U)
                (void)fprintf(stderr,
                              "MINIC_CORE_USAGE arch=%s group=%s kind=%zu bits=%s count=%zu\n",
                              arch, minic_backend_usage_group((MinicCoreInstructionKind)kind),
                              kind, widths[bucket], counts[kind][bucket]);
    for (kind = 0U; kind <= (size_t)MINIC_CORE_TERMINATOR_UNREACHABLE; ++kind)
        if (totals[kind] != 0U)
            (void)fprintf(stderr,
                          "MINIC_CORE_TERMINATOR_USAGE arch=%s kind=%zu count=%zu\n",
                          arch, kind, totals[kind]);
}
