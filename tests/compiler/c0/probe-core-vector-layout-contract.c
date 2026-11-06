/* Source-free Core type registration must not accept fake vector extents.
 * The target selector is allowed to trust a complete imported CoreType. */
#include "core/core_type.h"
#include <stdio.h>
#include <stdint.h>

int main(void) {
    MinicCoreTypeContext ctx = {0};
    MinicCoreType scalar = {0};
    MinicCoreType pointer = {0};
    MinicCoreType boolean = {0};
    MinicCoreType vector = {0};
    MinicCoreTypeId u8, vec, ptr_id, bool_id;
    minic_core_type_context_initialize(&ctx);
    scalar.kind = MINIC_CORE_TYPE_INTEGER;
    scalar.has_layout = true;
    scalar.size = 1U;
    scalar.alignment = 1U;
    scalar.value.integer.bit_width = 8U;
    if (!minic_core_type_register(&ctx, &scalar, &u8)) return 1;

    vector.kind = MINIC_CORE_TYPE_VECTOR;
    vector.has_layout = true;
    vector.size = 16U;
    vector.alignment = 16U;
    vector.value.vector.element = u8;
    vector.value.vector.element_count = 16U;
    vector.value.vector.representation = MINIC_CORE_VECTOR_REGISTER_VALUE;
    if (!minic_core_type_register(&ctx, &vector, &vec)) return 2;
    if (minic_core_type_get(&ctx, vec)->size != 16U) return 3;

    vector.value.vector.element_count = 8U;
    if (minic_core_type_register(&ctx, &vector, &vec)) return 4;
    vector.value.vector.element_count = 32U;
    if (minic_core_type_register(&ctx, &vector, &vec)) return 5;
    vector.value.vector.element_count = 16U;
    vector.has_layout = false;
    if (minic_core_type_register(&ctx, &vector, &vec)) return 6;
    vector.has_layout = true;
    vector.value.vector.element = MINIC_CORE_TYPE_INVALID;
    if (minic_core_type_register(&ctx, &vector, &vec)) return 7;
    vector.value.vector.element = u8;
    vector.alignment = 0U;
    if (minic_core_type_register(&ctx, &vector, &vec)) return 8;
    vector.alignment = 16U;
    vector.value.vector.element_count = SIZE_MAX;
    if (minic_core_type_register(&ctx, &vector, &vec)) return 9;

    vector.value.vector.element_count = 16U;
    vector.value.vector.representation = MINIC_CORE_VECTOR_STORAGE_ONLY;
    if (!minic_core_type_register(&ctx, &vector, &vec)) return 10;

    /* Pure layout equality is insufficient: vector elements must be
     * numerical scalars, never object pointers, bool or nested vectors. */
    pointer.kind = MINIC_CORE_TYPE_POINTER;
    pointer.has_layout = true;
    pointer.size = 8U;
    pointer.alignment = 8U;
    pointer.value.pointer.pointee = u8;
    if (!minic_core_type_register(&ctx, &pointer, &ptr_id)) return 11;
    vector.value.vector.element = ptr_id;
    vector.value.vector.element_count = 2U;
    vector.value.vector.representation = MINIC_CORE_VECTOR_REGISTER_VALUE;
    if (minic_core_type_register(&ctx, &vector, &vec)) return 12;
    boolean = scalar;
    boolean.value.integer.is_bool = true;
    if (!minic_core_type_register(&ctx, &boolean, &bool_id)) return 13;
    vector.value.vector.element = bool_id;
    vector.value.vector.element_count = 16U;
    if (minic_core_type_register(&ctx, &vector, &vec)) return 14;
    vector.value.vector.element = u8;
    vector.alignment = 3U;
    if (minic_core_type_register(&ctx, &vector, &vec)) return 15;
    vector.alignment = 16U;
    vector.value.vector.element = vec; /* a previously accepted vector */
    vector.value.vector.element_count = 1U;
    if (minic_core_type_register(&ctx, &vector, &vec)) return 16;

    minic_core_type_context_destroy(&ctx);
    puts("MINIC_CORE_VECTOR_LAYOUT=PASS register_and_storage=1 malformed=10");
    return 0;
}
