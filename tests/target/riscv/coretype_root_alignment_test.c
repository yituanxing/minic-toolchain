#include "target/riscv/typed_memory.h"

#include <assert.h>
#include <stdio.h>

/* This is a sealed Core fixture, not a source C Program or DataLayout.
 * Root alignment must depend exclusively on the CoreType and the object's
 * committed explicit storage alignment. */
int main(void) {
    MinicCoreType integer = {0};
    MinicCoreTypeContext types = {0};
    MinicCoreObject object = {0};
    MinicCoreValue values[3] = {0};
    MinicCoreInstruction instructions[3] = {0};
    MinicCoreFunction function = {0};

    integer.kind = MINIC_CORE_TYPE_INTEGER;
    integer.has_layout = true;
    integer.size = 8U;
    integer.alignment = 8U;
    integer.value.integer.bit_width = 64U;
    types.types = &integer;
    types.type_count = 1U;
    types.type_capacity = 1U;
    object.core_type_id = 0U;
    object.explicit_alignment = 16U;
    instructions[0].kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
    instructions[0].result = 0U;
    instructions[0].value.object_id = 0U;
    instructions[1].kind = MINIC_CORE_INSTRUCTION_POINTER_OFFSET;
    instructions[1].result = 1U;
    instructions[1].value.pointer_offset.base = 0U;
    instructions[1].value.pointer_offset.element_size = 4U;
    instructions[2].kind = MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS;
    instructions[2].result = 2U;
    values[0].definition = 0U;
    values[1].definition = 1U;
    values[2].definition = 2U;
    function.type_context = &types;
    function.objects = &object;
    function.object_count = 1U;
    function.values = values;
    function.value_count = 3U;
    function.instructions = instructions;
    function.instruction_count = 3U;

    assert(minic_riscv_address_alignment(&function, 0U) == 16U);
    assert(minic_riscv_address_alignment(&function, 1U) == 4U);
    assert(minic_riscv_address_alignment(&function, 2U) == 1U);

    /* The published object has no C descriptor to override Core alignment. */
    assert(minic_riscv_address_alignment(&function, 0U) == 16U);

    /* Absent/invalid Core evidence cannot authorize an aligned fast path. */
    function.type_context = NULL;
    assert(minic_riscv_address_alignment(&function, 0U) == 1U);
    function.type_context = &types;
    object.core_type_id = MINIC_CORE_TYPE_INVALID;
    assert(minic_riscv_address_alignment(&function, 0U) == 1U);
    object.core_type_id = 0U;
    integer.has_layout = false;
    assert(minic_riscv_address_alignment(&function, 0U) == 1U);
    integer.has_layout = true;
    object.explicit_alignment = 0U;
    integer.alignment = 3U;
    assert(minic_riscv_address_alignment(&function, 0U) == 1U);

    puts("RISCV_CORETYPE_ROOT_ALIGNMENT=PASS root=16 offset=4 global=1 C_free=1 missing_core=4");
    return 0;
}
