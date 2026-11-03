/* Both Core conversions and Core scalar bitcasts must ignore C descriptors.
 * Deliberately poison C operands while preserving sealed CoreType IDs. */
#include "target/common/core_scalar_conversion.h"
#include <stdio.h>

#define CHECK(expr) do { if (!(expr)) return __LINE__; } while (0)

static bool add_integer(MinicCoreTypeContext *ctx, size_t bytes,
                        bool sign, bool boolean, MinicCoreTypeId *id) {
    MinicCoreType type = {0};
    type.kind = MINIC_CORE_TYPE_INTEGER;
    type.has_layout = true;
    type.size = bytes;
    type.alignment = bytes;
    type.value.integer.bit_width = (unsigned int)(bytes * 8U);
    type.value.integer.is_signed = sign;
    type.value.integer.is_bool = boolean;
    return minic_core_type_register(ctx, &type, id);
}

static bool add_pointer(MinicCoreTypeContext *ctx, size_t bytes,
                        MinicCoreTypeId pointee, MinicCoreTypeId *id) {
    MinicCoreType type = {0};
    type.kind = MINIC_CORE_TYPE_POINTER;
    type.has_layout = true;
    type.size = bytes;
    type.alignment = bytes;
    type.value.pointer.pointee = pointee;
    return minic_core_type_register(ctx, &type, id);
}

static int run(void) {
    MinicCoreTypeContext types;
    MinicCoreTypeId signed_byte, unsigned_long, wide, boolean, invalid;
    MinicCoreTypeId pointer, pointer32, malformed_pointer;
    MinicCoreType malformed = {0};
    MinicCoreValue values[2] = {0};
    MinicCoreFunction function = {0};
    MinicCoreInstruction instruction = {0};
    MinicCoreIntegerConversionShape shape;
    MinicCoreScalarBitcastShape bitcast;
    minic_core_type_context_initialize(&types);
    CHECK(add_integer(&types, 1U, true, false, &signed_byte));
    CHECK(add_integer(&types, 8U, false, false, &unsigned_long));
    CHECK(add_integer(&types, 16U, true, false, &wide));
    CHECK(add_integer(&types, 1U, false, true, &boolean));
    malformed.kind = MINIC_CORE_TYPE_INTEGER;
    malformed.has_layout = true;
    malformed.size = 4U;
    malformed.alignment = 4U;
    malformed.value.integer.bit_width = 16U;
    CHECK(minic_core_type_register(&types, &malformed, &invalid));
    CHECK(add_pointer(&types, 8U, signed_byte, &pointer));
    CHECK(add_pointer(&types, 4U, signed_byte, &pointer32));
    CHECK(add_pointer(&types, 2U, signed_byte, &malformed_pointer));
    function.type_context = &types;
    function.values = values;
    function.value_count = 2U;
    instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION;
    instruction.value.operand = 0U;
    instruction.result = 1U;
    instruction.type = minic_type_void();
    values[0].core_type_id = signed_byte;
    values[1].core_type_id = unsigned_long;
    instruction.core_type_id = unsigned_long;
    CHECK(minic_core_integer_conversion_shape(&function, &instruction, &shape));
    CHECK(shape.source_bytes == 1U && shape.target_bytes == 8U &&
          shape.source_signed && !shape.target_signed &&
          !shape.source_bool && !shape.target_bool);
    values[0].core_type_id = wide;
    values[1].core_type_id = boolean;
    instruction.core_type_id = boolean;
    CHECK(minic_core_integer_conversion_shape(&function, &instruction, &shape));
    CHECK(shape.source_bytes == 16U && shape.target_bytes == 1U &&
          shape.target_bool);
    values[0].core_type_id = invalid;
    CHECK(!minic_core_integer_conversion_shape(&function, &instruction, &shape));
    values[0].core_type_id = wide;
    instruction.core_type_id = signed_byte;
    CHECK(!minic_core_integer_conversion_shape(&function, &instruction, &shape));
    instruction.core_type_id = boolean;
    function.type_context = NULL;
    CHECK(!minic_core_integer_conversion_shape(&function, &instruction, &shape));
    function.type_context = &types;

    instruction.kind = MINIC_CORE_INSTRUCTION_SCALAR_BITCAST;
    values[0].core_type_id = pointer;
    values[1].core_type_id = unsigned_long;
    instruction.core_type_id = unsigned_long;
    CHECK(minic_core_scalar_bitcast_shape(&function, &instruction, &bitcast));
    CHECK(bitcast.source_pointer && !bitcast.target_pointer &&
          bitcast.source_bytes == 8U && bitcast.target_bytes == 8U &&
          !bitcast.target_bool && !bitcast.target_signed);
    values[0].core_type_id = pointer32;
    CHECK(minic_core_scalar_bitcast_shape(&function, &instruction, &bitcast));
    CHECK(bitcast.source_bytes == 4U && bitcast.target_bytes == 8U);
    values[0].core_type_id = pointer;
    values[1].core_type_id = boolean;
    instruction.core_type_id = boolean;
    CHECK(minic_core_scalar_bitcast_shape(&function, &instruction, &bitcast));
    CHECK(bitcast.target_bool && bitcast.target_bytes == 1U);
    values[0].core_type_id = unsigned_long;
    values[1].core_type_id = pointer;
    instruction.core_type_id = pointer;
    CHECK(minic_core_scalar_bitcast_shape(&function, &instruction, &bitcast));
    CHECK(!bitcast.source_pointer && bitcast.target_pointer &&
          bitcast.source_bytes == 8U);
    values[1].core_type_id = pointer32;
    instruction.core_type_id = pointer32;
    CHECK(minic_core_scalar_bitcast_shape(&function, &instruction, &bitcast));
    CHECK(bitcast.source_bytes == 8U && bitcast.target_bytes == 4U);
    values[1].core_type_id = pointer;
    instruction.core_type_id = pointer;
    values[0].core_type_id = malformed_pointer;
    CHECK(!minic_core_scalar_bitcast_shape(&function, &instruction, &bitcast));
    values[0].core_type_id = unsigned_long;
    instruction.core_type_id = boolean;
    CHECK(!minic_core_scalar_bitcast_shape(&function, &instruction, &bitcast));
    instruction.core_type_id = pointer;
    values[1].core_type_id = pointer;
    function.type_context = NULL;
    CHECK(!minic_core_scalar_bitcast_shape(&function, &instruction, &bitcast));
    minic_core_type_context_destroy(&types);
    return 0;
}

int main(void) {
    const int line = run();
    if (line != 0) {
        fprintf(stderr, "CORE_SCALAR_CONVERSION_SHAPE=FAIL line=%d\n", line);
        return 1;
    }
    puts("CORE_SCALAR_CONVERSION_SHAPE=PASS C_poison integer bitcast LP64_ILP32 bool rollback=6");
    return 0;
}
