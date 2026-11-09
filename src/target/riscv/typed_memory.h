#ifndef MINIC_TARGET_RISCV_TYPED_MEMORY_H
#define MINIC_TARGET_RISCV_TYPED_MEMORY_H

#include "core/address_alignment.h"

#include <stddef.h>

/* Core address alignment is a pure target policy. Native loads, guarded
 * bytewise fallback and register allocation are now wholly MachineIR-owned;
 * the retired FILE* memory serializers must never be called from here. */
static inline size_t minic_riscv_address_root_alignment(
    const MinicCoreFunction *function,
    const MinicCoreInstruction *root, void *opaque) {
    const MinicCoreObject *object;
    const MinicCoreType *type;
    size_t alignment;
    (void)opaque;
    if (function == NULL || root == NULL ||
        root->kind != MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS ||
        root->value.object_id >= function->object_count ||
        function->objects == NULL || function->type_context == NULL)
        return 1U;
    object = &function->objects[root->value.object_id];
    type = minic_core_type_get(function->type_context, object->core_type_id);
    if (type == NULL || !type->has_layout || type->alignment == 0U)
        return 1U;
    alignment = type->alignment;
    if (object->explicit_alignment > alignment)
        alignment = object->explicit_alignment;
    return alignment;
}

static inline size_t minic_riscv_address_alignment(
    const MinicCoreFunction *function, MinicCoreValueId address) {
    return minic_core_address_known_alignment(
        function, address, minic_riscv_address_root_alignment, NULL);
}

#endif
