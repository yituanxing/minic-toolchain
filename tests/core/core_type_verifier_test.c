/* Deliberately corrupted, otherwise well-formed Core function fixtures.
 * The type verifier must inspect semantic Core types, not C descriptors. */
#include "core/core_ir_source.h"

#include <stdio.h>
#include <string.h>

typedef struct Types {
    MinicCoreTypeId i32, u32, ptr_i32, ptr_u32;
    MinicType source_ptr_i32;
} Types;

#define REQUIRE(expr) do { \
    if (!(expr)) { \
        (void)fprintf(stderr, "CORE_TYPE_VERIFIER=FAIL line=%d check=%s\n", \
                      __LINE__, #expr); \
        return 1; \
    } \
} while (0)

static bool register_types(MinicCoreTypeContext *context, Types *types) {
    MinicCoreType type = {0};
    minic_core_type_context_initialize(context);
    type.kind = MINIC_CORE_TYPE_INTEGER;
    type.has_layout = true;
    type.size = 4U;
    type.alignment = 4U;
    type.value.integer.bit_width = 32U;
    type.value.integer.is_signed = true;
    if (!minic_core_type_register(context, &type, &types->i32)) return false;
    type.value.integer.is_signed = false;
    if (!minic_core_type_register(context, &type, &types->u32)) return false;
    (void)memset(&type, 0, sizeof(type));
    type.kind = MINIC_CORE_TYPE_POINTER;
    type.has_layout = true;
    type.size = 8U;
    type.alignment = 8U;
    type.value.pointer.pointee = types->i32;
    if (!minic_core_type_register(context, &type, &types->ptr_i32)) return false;
    type.value.pointer.pointee = types->u32;
    return minic_core_type_register(context, &type, &types->ptr_u32) &&
           minic_type_pointer_to(minic_type_int(), &types->source_ptr_i32);
}

static bool resolve_type(void *data, MinicType source, MinicCoreTypeId *result) {
    const Types *types = data;
    if (minic_type_equal(source, minic_type_int())) *result = types->i32;
    else if (minic_type_equal(source, types->source_ptr_i32)) *result = types->ptr_i32;
    else return false;
    return true;
}

/* Value IDs: 0=int, 1=int, 2=int*, 3=add, 4=load, 5=bitcast, 6=add. */
static bool build_function(MinicCoreFunction *fn, MinicCoreTypeContext *context,
                           const Types *types) {
    MinicType params[3];
    MinicCoreInstruction op = {0};
    MinicCoreTerminator term = {0};
    MinicCoreBlockId block;
    MinicCoreValueId id;
    size_t index;

    minic_core_function_initialize(fn);
    params[0] = minic_type_int();
    params[1] = minic_type_int();
    params[2] = types->source_ptr_i32;
    if (!minic_core_function_set_signature(fn, "typed", 5U,
                                           minic_type_int(), params, 3U) ||
        !minic_core_function_add_block(fn, &block)) return false;
    for (index = 0U; index < 3U; ++index) {
        (void)memset(&op, 0, sizeof(op));
        op.kind = MINIC_CORE_INSTRUCTION_PARAMETER;
        op.value.parameter_index = index;
        if (!minic_core_function_append_source_value_instruction(
                fn, block, &op, params[index], &id) ||
            id != index) return false;
    }
    (void)memset(&op, 0, sizeof(op));
    op.kind = MINIC_CORE_INSTRUCTION_INTEGER_ADD;
    op.value.binary.left = 0U;
    op.value.binary.right = 1U;
    if (!minic_core_function_append_source_value_instruction(
            fn, block, &op, minic_type_int(), &id) ||
        id != 3U) return false;
    (void)memset(&op, 0, sizeof(op));
    op.kind = MINIC_CORE_INSTRUCTION_LOAD;
    op.value.load.address = 2U;
    if (!minic_core_function_append_source_value_instruction(
            fn, block, &op, minic_type_int(), &id) ||
        id != 4U) return false;
    (void)memset(&op, 0, sizeof(op));
    op.kind = MINIC_CORE_INSTRUCTION_SCALAR_BITCAST;
    op.value.operand = 2U;
    if (!minic_core_function_append_source_value_instruction(
            fn, block, &op, minic_type_int(), &id) ||
        id != 5U) return false;
    (void)memset(&op, 0, sizeof(op));
    op.kind = MINIC_CORE_INSTRUCTION_INTEGER_ADD;
    op.value.binary.left = 3U;
    op.value.binary.right = 4U;
    if (!minic_core_function_append_source_value_instruction(
            fn, block, &op, minic_type_int(), &id) ||
        id != 6U) return false;
    term.kind = MINIC_CORE_TERMINATOR_RETURN;
    term.return_value = 6U;
    term.return_object = MINIC_CORE_OBJECT_INVALID;
    if (!minic_core_function_set_terminator(fn, block, &term)) return false;
    return minic_core_function_snapshot_types(fn, context, resolve_type, (void *)types);
}

static int check_invalid_mutations(MinicCoreFunction *fn, const Types *types) {
    MinicCoreTypeId saved_type;
    MinicCoreValueId saved_value;
    size_t saved_index;

    REQUIRE(minic_core_function_verify(fn));
    saved_type = fn->values[0].core_type_id;
    fn->values[0].core_type_id = types->u32;
    REQUIRE(!minic_core_function_verify(fn));
    fn->values[0].core_type_id = saved_type;

    saved_value = fn->instructions[3].value.binary.right;
    fn->instructions[3].value.binary.right = 2U; /* pointer is not integer */
    REQUIRE(!minic_core_function_verify(fn));
    fn->instructions[3].value.binary.right = 6U; /* forward SSA use */
    REQUIRE(!minic_core_function_verify(fn));
    fn->instructions[3].value.binary.right = saved_value;

    saved_value = fn->instructions[4].value.load.address;
    fn->instructions[4].value.load.address = 1U; /* scalar address */
    REQUIRE(!minic_core_function_verify(fn));
    fn->instructions[4].value.load.address = saved_value;

    fn->instructions[4].core_type_id = types->u32;
    fn->values[4].core_type_id = types->u32;
    REQUIRE(!minic_core_function_verify(fn)); /* different pointee type */
    fn->instructions[4].core_type_id = types->i32;
    fn->values[4].core_type_id = types->i32;

    saved_value = fn->instructions[5].value.operand;
    fn->instructions[5].value.operand = 1U; /* int-to-int is not bitcast */
    REQUIRE(!minic_core_function_verify(fn));
    fn->instructions[5].value.operand = saved_value;

    saved_index = fn->instructions[1].value.parameter_index;
    fn->instructions[1].value.parameter_index = 3U;
    REQUIRE(!minic_core_function_verify(fn));
    fn->instructions[1].value.parameter_index = saved_index;

    saved_value = fn->blocks[0].terminator.return_value;
    fn->blocks[0].terminator.return_value = 2U;
    REQUIRE(!minic_core_function_verify(fn));
    fn->blocks[0].terminator.return_value = saved_value;

    fn->core_return_type_id = types->u32;
    REQUIRE(!minic_core_function_verify(fn));
    fn->core_return_type_id = types->i32;

    fn->values[2].core_type_id = types->ptr_u32;
    REQUIRE(!minic_core_function_verify(fn));
    fn->values[2].core_type_id = types->ptr_i32;
    REQUIRE(minic_core_function_verify(fn));
    return 0;
}

int main(void) {
    MinicCoreTypeContext context;
    MinicCoreFunction fn;
    Types types = {0};
    int result;
    REQUIRE(register_types(&context, &types));
    REQUIRE(build_function(&fn, &context, &types));
    result = check_invalid_mutations(&fn, &types);
    minic_core_function_destroy(&fn);
    minic_core_type_context_destroy(&context);
    if (result != 0) return result;
    (void)puts("CORE_TYPE_VERIFIER=PASS baseline=1 malformed=10 operands_ssa_pointer_load_bitcast_parameter_return");
    return 0;
}
