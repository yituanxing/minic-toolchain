/* All 17 floating-scalar Core operations use only sealed Core identities;
 * nominal C ranks and frontend descriptors are absent from Core IR. */
#include "target/common/core_floating_shape.h"
#include <stdio.h>

#define REQUIRE(expr) do { if (!(expr)) return __LINE__; } while (0)

static bool register_float(MinicCoreTypeContext *ctx,
                           MinicCoreFloatFormat format, size_t bytes,
                           MinicCoreTypeId *id) {
    MinicCoreType type = {0};
    type.kind = MINIC_CORE_TYPE_FLOAT;
    type.has_layout = true;
    type.size = bytes;
    type.alignment = bytes;
    type.value.floating.format = format;
    return minic_core_type_register(ctx, &type, id);
}

static bool register_int(MinicCoreTypeContext *ctx, size_t bytes,
                         bool sign, MinicCoreTypeId *id) {
    MinicCoreType type = {0};
    type.kind = MINIC_CORE_TYPE_INTEGER;
    type.has_layout = true;
    type.size = bytes;
    type.alignment = bytes;
    type.value.integer.bit_width = (unsigned int)(bytes * 8U);
    type.value.integer.is_signed = sign;
    return minic_core_type_register(ctx, &type, id);
}

static int run(void) {
    MinicCoreTypeContext ctx;
    MinicCoreTypeId f32, f64, ext, i8, i32, u64, invalid_float, invalid_int;
    MinicCoreValue values[4] = {0};
    MinicCoreFunction function = {0};
    MinicCoreInstruction ins = {0};
    MinicCoreFloatingShape shape;
    MinicCoreType malformed = {0};
    const MinicCoreInstructionKind f32_ops[] = {
        MINIC_CORE_INSTRUCTION_FLOAT_ADD,
        MINIC_CORE_INSTRUCTION_FLOAT_SUBTRACT,
        MINIC_CORE_INSTRUCTION_FLOAT_MULTIPLY,
        MINIC_CORE_INSTRUCTION_FLOAT_DIVIDE,
    };
    const MinicCoreInstructionKind f64_ops[] = {
        MINIC_CORE_INSTRUCTION_DOUBLE_ADD,
        MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT,
        MINIC_CORE_INSTRUCTION_DOUBLE_MULTIPLY,
        MINIC_CORE_INSTRUCTION_DOUBLE_DIVIDE,
    };
    const MinicCoreInstructionKind comparisons[] = {
        MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL,
        MINIC_CORE_INSTRUCTION_DOUBLE_LESS,
        MINIC_CORE_INSTRUCTION_DOUBLE_LESS_EQUAL,
    };
    size_t i;
    minic_core_type_context_initialize(&ctx);
    REQUIRE(register_float(&ctx, MINIC_CORE_FLOAT_BINARY32, 4U, &f32));
    REQUIRE(register_float(&ctx, MINIC_CORE_FLOAT_BINARY64, 8U, &f64));
    REQUIRE(register_float(&ctx, MINIC_CORE_FLOAT_EXTENDED, 16U, &ext));
    REQUIRE(register_int(&ctx, 1U, true, &i8));
    REQUIRE(register_int(&ctx, 4U, true, &i32));
    REQUIRE(register_int(&ctx, 8U, false, &u64));
    REQUIRE(register_float(&ctx, MINIC_CORE_FLOAT_BINARY64, 4U,
                           &invalid_float));
    malformed.kind = MINIC_CORE_TYPE_INTEGER;
    malformed.has_layout = true;
    malformed.size = 4U;
    malformed.alignment = 4U;
    malformed.value.integer.bit_width = 16U;
    REQUIRE(minic_core_type_register(&ctx, &malformed, &invalid_int));
    function.type_context = &ctx;
    function.values = values;
    function.value_count = 4U;
    ins.result = 3U;
    ins.value.binary.left = 0U;
    ins.value.binary.right = 1U;

    ins.kind = MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT;
    values[3].core_type_id = f32;
    ins.core_type_id = f32;
    REQUIRE(minic_core_floating_shape(&function, &ins, &shape));
    values[3].core_type_id = f64;
    ins.core_type_id = f64;
    REQUIRE(minic_core_floating_shape(&function, &ins, &shape));
    values[3].core_type_id = ext;
    ins.core_type_id = ext;
    REQUIRE(minic_core_floating_shape(&function, &ins, &shape));
    values[0].core_type_id = f32;
    values[1].core_type_id = f32;
    values[3].core_type_id = f32;
    ins.core_type_id = f32;
    for (i = 0U; i < 4U; ++i) {
        ins.kind = f32_ops[i];
        REQUIRE(minic_core_floating_shape(&function, &ins, &shape));
    }
    values[0].core_type_id = f64;
    values[1].core_type_id = f64;
    values[3].core_type_id = f64;
    ins.core_type_id = f64;
    for (i = 0U; i < 4U; ++i) {
        ins.kind = f64_ops[i];
        REQUIRE(minic_core_floating_shape(&function, &ins, &shape));
    }
    values[3].core_type_id = i32;
    ins.core_type_id = i32;
    for (i = 0U; i < 3U; ++i) {
        ins.kind = comparisons[i];
        REQUIRE(minic_core_floating_shape(&function, &ins, &shape));
    }
    ins.kind = MINIC_CORE_INSTRUCTION_DOUBLE_NEGATE;
    ins.value.operand = 0U;
    values[3].core_type_id = f64;
    ins.core_type_id = f64;
    REQUIRE(minic_core_floating_shape(&function, &ins, &shape));

    ins.kind = MINIC_CORE_INSTRUCTION_INTEGER_TO_DOUBLE;
    values[0].core_type_id = i8;
    REQUIRE(minic_core_floating_shape(&function, &ins, &shape));
    REQUIRE(shape.integer_bytes == 1U && shape.integer_signed);
    values[0].core_type_id = u64;
    REQUIRE(minic_core_floating_shape(&function, &ins, &shape));
    REQUIRE(shape.integer_bytes == 8U && !shape.integer_signed);
    ins.kind = MINIC_CORE_INSTRUCTION_DOUBLE_TO_INTEGER;
    values[0].core_type_id = f64;
    values[3].core_type_id = u64;
    ins.core_type_id = u64;
    REQUIRE(minic_core_floating_shape(&function, &ins, &shape));
    REQUIRE(shape.integer_bytes == 8U && !shape.integer_signed);
    ins.kind = MINIC_CORE_INSTRUCTION_FLOAT_TO_DOUBLE;
    values[0].core_type_id = f32;
    values[3].core_type_id = f64;
    ins.core_type_id = f64;
    REQUIRE(minic_core_floating_shape(&function, &ins, &shape));
    ins.kind = MINIC_CORE_INSTRUCTION_DOUBLE_TO_FLOAT;
    values[0].core_type_id = f64;
    values[3].core_type_id = f32;
    ins.core_type_id = f32;
    REQUIRE(minic_core_floating_shape(&function, &ins, &shape));

    values[0].core_type_id = invalid_float;
    REQUIRE(!minic_core_floating_shape(&function, &ins, &shape));
    values[0].core_type_id = f64;
    ins.core_type_id = f64;
    REQUIRE(!minic_core_floating_shape(&function, &ins, &shape));
    ins.kind = MINIC_CORE_INSTRUCTION_INTEGER_TO_DOUBLE;
    values[0].core_type_id = invalid_int;
    REQUIRE(!minic_core_floating_shape(&function, &ins, &shape));
    ins.kind = MINIC_CORE_INSTRUCTION_DOUBLE_TO_FLOAT;
    values[0].core_type_id = f64;
    ins.core_type_id = f32;
    ins.result = 4U;
    REQUIRE(!minic_core_floating_shape(&function, &ins, &shape));
    ins.result = 3U;
    function.type_context = NULL;
    REQUIRE(!minic_core_floating_shape(&function, &ins, &shape));
    minic_core_type_context_destroy(&ctx);
    return 0;
}

int main(void) {
    const int line = run();
    if (line != 0) {
        fprintf(stderr, "CORE_FLOATING_SHAPE=FAIL line=%d\n", line);
        return 1;
    }
    puts("CORE_FLOATING_SHAPE=PASS family=17 C_free binary32 binary64 extended signed8 unsigned64 invalid=5");
    return 0;
}
