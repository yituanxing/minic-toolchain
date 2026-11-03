/* Native host proof of sealed CoreType-based AArch64 Q-register emission.
 * The separate ACLE/QEMU gate owns actual machine runtime qualification. */
#include "target/aarch64/neon_emit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    type.value.vector.is_builtin = builtin;
    return minic_core_type_register(types, &type, result);
}

static bool has_text(FILE *out, const char *needle) {
    char text[4096];
    size_t count;
    if (fflush(out) != 0 || fseek(out, 0L, SEEK_SET) != 0)
        return false;
    count = fread(text, 1U, sizeof(text) - 1U, out);
    text[count] = '\0';
    return strstr(text, needle) != NULL;
}

static bool empty_file(FILE *out) {
    return fflush(out) == 0 && fseek(out, 0L, SEEK_END) == 0 &&
           ftell(out) == 0L;
}

static int run(void) {
    const MinicA64FrameView frame = {false, 1024U, 0U, SIZE_MAX};
    const MinicA64FrameView dynamic = {true, 12288U, 0U, SIZE_MAX};
    MinicCoreTypeContext types;
    MinicCoreTypeId u64, u8, s8, words, bytes, signed_bytes;
    MinicCoreTypeId nonbuiltin, misaligned, wrong_count, pointer, qualified;
    MinicCoreType qualifier = {0}, pointer_type = {0};
    FILE *out;
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
    qualifier.kind = MINIC_CORE_TYPE_QUALIFIED;
    qualifier.has_layout = true;
    qualifier.size = 16U;
    qualifier.alignment = 16U;
    qualifier.is_read_only = true;
    qualifier.value.qualified.base = words;
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

    out = tmpfile();
    if (out == NULL) return 3;
    if (!minic_aarch64_neon_emit_load_core(out, &frame, &types, words, 0U, 16U) ||
        !has_text(out, "  ldr q16, [x10]\n") ||
        !has_text(out, "  str q16, [sp, #16]\n")) return 4;
    fclose(out);

    out = tmpfile();
    if (out == NULL) return 5;
    if (!minic_aarch64_neon_emit_store_core(out, &frame, &types, bytes, 0U, 16U) ||
        !has_text(out, "  ldr q16, [sp, #16]\n") ||
        !has_text(out, "  str q16, [x10]\n")) return 6;
    fclose(out);

    out = tmpfile();
    if (out == NULL) return 7;
    if (!minic_aarch64_neon_emit_xor_core(out, &frame, &types, words,
                                         16U, 32U, 48U) ||
        !has_text(out, "  ldr q16, [sp, #16]\n") ||
        !has_text(out, "  ldr q17, [sp, #32]\n") ||
        !has_text(out, "  eor v16.16b, v16.16b, v17.16b\n") ||
        !has_text(out, "  str q16, [sp, #48]\n")) return 8;
    fclose(out);

    out = tmpfile();
    if (out == NULL) return 9;
    if (!minic_aarch64_neon_emit_xor_core(out, &dynamic, &types, signed_bytes,
                                         8192U, 8208U, 8224U) ||
        !has_text(out, "  add x17, x19, x17\n") ||
        !has_text(out, "  ldr q16, [x17]\n") ||
        !has_text(out, "  ldr q17, [x17]\n") ||
        !has_text(out, "  str q16, [x17]\n")) return 10;
    fclose(out);

    /* Machine admission checks Core shape, not the source's C qualifiers.
     * Rejected shapes must not write even the first machine instruction. */
    out = tmpfile();
    if (out == NULL) return 11;
    if (minic_aarch64_neon_emit_xor_core(out, &frame, &types, pointer,
                                        0U, 16U, 32U) || !empty_file(out) ||
        minic_aarch64_neon_emit_load_core(out, &frame, &types, nonbuiltin,
                                         0U, 16U) || !empty_file(out) ||
        minic_aarch64_neon_emit_store_core(out, &frame, &types, misaligned,
                                          0U, 16U) || !empty_file(out) ||
        minic_aarch64_neon_emit_xor_core(out, &frame, &types, wrong_count,
                                        0U, 16U, 32U) || !empty_file(out))
        return 12;
    fclose(out);

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
