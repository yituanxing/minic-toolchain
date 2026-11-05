/* Native proof of Core's qualified 128-bit vector admission shape.
 * Typed MachineIR physical emission and GNU serialization are exercised by
 * run-aarch64-neon-core-vector128.sh; there is no parallel FILE* emitter. */
#include "target/aarch64/neon_emit.h"

#include <stdio.h>

static bool add_integer(MinicCoreTypeContext *types, unsigned int bits,
                        bool sign, MinicCoreTypeId *result) {
    MinicCoreType type = {0};
    type.kind = MINIC_CORE_TYPE_INTEGER;
    type.has_layout = true;
    type.size = (size_t)bits / 8U;
    type.alignment = type.size;
    type.value.integer.bit_width = bits;
    type.value.integer.is_signed = sign;
    return minic_core_type_register(types, &type, result);
}

static bool add_vector(MinicCoreTypeContext *types, MinicCoreTypeId element,
                       size_t count, bool builtin, size_t alignment,
                       MinicCoreTypeId *result) {
    MinicCoreType type = {0};
    type.kind = MINIC_CORE_TYPE_VECTOR;
    type.has_layout = true;
    type.size = 16U;
    type.alignment = alignment;
    type.value.vector.element = element;
    type.value.vector.element_count = count;
    type.value.vector.representation = builtin
        ? MINIC_CORE_VECTOR_REGISTER_VALUE
        : MINIC_CORE_VECTOR_STORAGE_ONLY;
    return minic_core_type_register(types, &type, result);
}

static int run(void) {
    MinicCoreTypeContext types;
    MinicCoreTypeId u64, u8, s8, words, bytes, signed_bytes;
    MinicCoreTypeId nonbuiltin, misaligned, wrong_count, pointer, qualified;
    MinicCoreType qualifier = {0}, pointer_type = {0};
    minic_core_type_context_initialize(&types);
    if (!add_integer(&types, 64U, false, &u64) ||
        !add_integer(&types, 8U, false, &u8) ||
        !add_integer(&types, 8U, true, &s8) ||
        !add_vector(&types, u64, 2U, true, 16U, &words) ||
        !add_vector(&types, u8, 16U, true, 16U, &bytes) ||
        !add_vector(&types, s8, 16U, true, 16U, &signed_bytes) ||
        !add_vector(&types, u8, 16U, false, 16U, &nonbuiltin) ||
        !add_vector(&types, u8, 16U, true, 8U, &misaligned) ||
        !add_vector(&types, u8, 8U, true, 16U, &wrong_count))
        return 1;
    pointer_type.kind = MINIC_CORE_TYPE_POINTER;
    pointer_type.has_layout = true;
    pointer_type.size = 8U;
    pointer_type.alignment = 8U;
    pointer_type.value.pointer.pointee = words;
    qualifier.kind = MINIC_CORE_TYPE_ALIGNED;
    qualifier.has_layout = true;
    qualifier.size = 16U;
    qualifier.alignment = 32U;
    qualifier.has_abi_alignment_override = true;
    qualifier.value.aligned.base = words;
    if (!minic_core_type_register(&types, &pointer_type, &pointer) ||
        !minic_core_type_register(&types, &qualifier, &qualified)) return 2;
    if (!minic_aarch64_neon_vector128_core_type(&types, words) ||
        !minic_aarch64_neon_vector128_core_type(&types, bytes) ||
        !minic_aarch64_neon_vector128_core_type(&types, signed_bytes) ||
        !minic_aarch64_neon_vector128_core_type(&types, qualified) ||
        minic_aarch64_neon_vector128_core_type(&types, pointer) ||
        minic_aarch64_neon_vector128_core_type(&types, nonbuiltin) ||
        minic_aarch64_neon_vector128_core_type(&types, misaligned) ||
        minic_aarch64_neon_vector128_core_type(&types, wrong_count) ||
        minic_aarch64_neon_vector128_core_type(NULL, words) ||
        minic_aarch64_neon_vector128_core_type(&types, MINIC_CORE_TYPE_INVALID))
        return 2;

    minic_core_type_context_destroy(&types);
    return 0;
}

int main(void) {
    const int result = run();
    if (result != 0)
        fprintf(stderr, "AArch64 Core NEON machine contract failed at check %d\n",
                result);
    return result;
}
