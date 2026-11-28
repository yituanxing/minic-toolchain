/*
 * The four LoongArch machine-side ASM selector families share one Core
 * scalar admission gate. Keep behavior tied to real CoreType data, including
 * forged SSA metadata. Assertions are deliberately independent of -DNDEBUG.
 */
#include "target/loongarch64/machine_scalar_type.h"

#include <stdio.h>

#define CHECK(pred) do { \
    if (!(pred)) { \
        fprintf(stderr, "Loong scalar gate failure: %s at line %d\n", \
                #pred, __LINE__); \
        return 1; \
    } \
} while (0)

int main(void) {
    MinicCoreType types[6] = {{0}};
    MinicCoreValue values[6] = {{0}};
    MinicCoreTypeContext context = {0};
    MinicCoreFunction function = {0};

    /* 0: u64, 1: i32, 2: bool8, 3: pointer-to-u64,
     * 4: aligned view of u64, 5: pointer-to-aligned-u64. */
    types[0].kind = MINIC_CORE_TYPE_INTEGER;
    types[0].has_layout = true;
    types[0].size = 8U;
    types[0].value.integer.bit_width = 64U;

    types[1].kind = MINIC_CORE_TYPE_INTEGER;
    types[1].has_layout = true;
    types[1].size = 4U;
    types[1].value.integer.bit_width = 32U;
    types[1].value.integer.is_signed = true;

    types[2].kind = MINIC_CORE_TYPE_INTEGER;
    types[2].has_layout = true;
    types[2].size = 1U;
    types[2].value.integer.bit_width = 8U;
    types[2].value.integer.is_bool = true;

    types[3].kind = MINIC_CORE_TYPE_POINTER;
    types[3].has_layout = true;
    types[3].size = 8U;
    types[3].value.pointer.pointee = 0U;

    types[4].kind = MINIC_CORE_TYPE_ALIGNED;
    types[4].has_layout = true;
    types[4].size = 8U;
    types[4].value.aligned.base = 0U;

    types[5].kind = MINIC_CORE_TYPE_POINTER;
    types[5].has_layout = true;
    types[5].size = 8U;
    types[5].value.pointer.pointee = 4U;

    context.types = types;
    context.type_count = sizeof(types) / sizeof(types[0]);
    for (size_t i = 0U; i < context.type_count; ++i) {
        values[i].core_type_id = (MinicCoreTypeId)i;
    }
    function.type_context = &context;
    function.values = values;
    function.value_count = context.type_count;

    CHECK(minic_loong_core_integer_operand(&function, 0U, false, 8U, -1));
    CHECK(minic_loong_core_integer_operand(&function, 0U, false, 8U, 0));
    CHECK(!minic_loong_core_integer_operand(&function, 0U, false, 8U, 1));
    CHECK(!minic_loong_core_integer_operand(&function, 0U, false, 4U, -1));
    CHECK(minic_loong_core_integer_operand(&function, 1U, false, 4U, 1));
    CHECK(!minic_loong_core_integer_operand(&function, 1U, false, 4U, 0));
    CHECK(!minic_loong_core_integer_operand(&function, 2U, false, 1U, -1));

    CHECK(minic_loong_core_integer_operand(&function, 3U, true, 8U, -1));
    CHECK(!minic_loong_core_integer_operand(&function, 3U, false, 8U, -1));
    CHECK(minic_loong_core_integer_operand(&function, 5U, true, 8U, -1));
    CHECK(minic_loong_core_integer_operand(&function, 4U, false, 8U, -1));
    CHECK(!minic_loong_core_integer_operand(&function, 0U, true, 8U, -1));

    CHECK(!minic_loong_core_integer_operand(NULL, 0U, false, 8U, -1));
    CHECK(!minic_loong_core_integer_operand(&function,
                                             MINIC_CORE_VALUE_INVALID,
                                             false, 8U, -1));
    CHECK(!minic_loong_core_integer_operand(&function, 6U, false, 8U, -1));

    function.values = NULL;
    CHECK(!minic_loong_core_integer_operand(&function, 0U, false, 8U, -1));
    function.values = values;
    function.type_context = NULL;
    CHECK(!minic_loong_core_integer_operand(&function, 0U, false, 8U, -1));
    function.type_context = &context;

    types[0].has_layout = false;
    CHECK(!minic_loong_core_integer_operand(&function, 0U, false, 8U, -1));
    types[0].has_layout = true;

    types[3].value.pointer.pointee = 88U;
    CHECK(!minic_loong_core_integer_operand(&function, 3U, true, 8U, -1));
    types[3].value.pointer.pointee = 0U;

    types[0].value.integer.bit_width = 32U;
    CHECK(!minic_loong_core_integer_operand(&function, 0U, false, 8U, -1));

    puts("LOONG_SCALAR_CORE_GATE=PASS: widths/signed/bool/pointee/align/invalid");
    return 0;
}
