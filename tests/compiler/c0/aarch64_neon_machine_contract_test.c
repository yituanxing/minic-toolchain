/* Native C host test of AArch64's exact owned-frame Q-register emitter.
 * No fake ARM CPU execution: these tests validate the emission contract;
 * the separate strict ACLE/QEMU gate validates actual machine behavior. */
#include "frontend/type.h"
#include "target/aarch64/neon_emit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int expect_text(FILE *out, const char *needle) {
    char text[4096];
    size_t count;
    if (fflush(out) != 0 || fseek(out, 0L, SEEK_SET) != 0)
        return 0;
    count = fread(text, 1U, sizeof(text) - 1U, out);
    text[count] = '\0';
    return strstr(text, needle) != NULL;
}

static int empty_file(FILE *out) {
    long length;
    if (fflush(out) != 0 || fseek(out, 0L, SEEK_END) != 0)
        return 0;
    length = ftell(out);
    return length == 0L;
}

static int run(void) {
    const MinicA64FrameView frame = {false, 1024U, 0U, SIZE_MAX};
    const MinicA64FrameView dynamic = {true, 12288U, 0U, SIZE_MAX};
    const MinicType words = minic_type_aarch64_fixed_vector128(
        MINIC_TYPE_BASE_AARCH64_UINT64X2);
    const MinicType bytes = minic_type_aarch64_fixed_vector128(
        MINIC_TYPE_BASE_AARCH64_UINT8X16);
    const MinicType signed_bytes = minic_type_aarch64_fixed_vector128(
        MINIC_TYPE_BASE_AARCH64_INT8X16);
    const MinicType polynomial = minic_type_aarch64_fixed_vector128(
        MINIC_TYPE_BASE_AARCH64_POLY8X16);
    MinicType invalid = words;
    MinicExpression expressions[4] = {{0}};
    MinicInlineAsmOperand outputs[1] = {{0}};
    MinicInlineAsmOperand inputs[3] = {{0}};
    MinicC0Program program = {0};
    MinicInlineAsm source = {0};
    static const char eor3_template[] =
        ".arch_extension sha3\n"
        "eor3 %0.16b, %1.16b, %2.16b, %3.16b";
    size_t input_index;
    FILE *out;
    if (!minic_aarch64_neon_vector128_type(words) ||
        !minic_aarch64_neon_vector128_type(bytes) ||
        !minic_aarch64_neon_vector128_type(signed_bytes) ||
        !minic_aarch64_neon_vector128_type(polynomial) ||
        minic_aarch64_neon_vector128_type(minic_type_unsigned_long_long()))
        return 1;

    out = tmpfile();
    if (out == NULL) return 2;
    if (!minic_aarch64_neon_emit_load(out, &frame, words, 0U, 16U) ||
        !expect_text(out, "  ldr q16, [x10]\n") ||
        !expect_text(out, "  str q16, [sp, #16]\n")) return 3;
    fclose(out);

    out = tmpfile();
    if (out == NULL) return 4;
    if (!minic_aarch64_neon_emit_store(out, &frame, bytes, 0U, 16U) ||
        !expect_text(out, "  ldr q16, [sp, #16]\n") ||
        !expect_text(out, "  str q16, [x10]\n")) return 5;
    fclose(out);

    out = tmpfile();
    if (out == NULL) return 6;
    if (!minic_aarch64_neon_emit_xor(out, &frame, words, 16U, 32U, 48U) ||
        !expect_text(out, "  ldr q16, [sp, #16]\n") ||
        !expect_text(out, "  ldr q17, [sp, #32]\n") ||
        !expect_text(out, "  eor v16.16b, v16.16b, v17.16b\n") ||
        !expect_text(out, "  str q16, [sp, #48]\n")) return 7;
    fclose(out);

    out = tmpfile();
    if (out == NULL) return 8;
    if (!minic_aarch64_neon_emit_xor(out, &dynamic, bytes,
                                      8192U, 8208U, 8224U) ||
        !expect_text(out, "  add x17, x19, x17\n") ||
        !expect_text(out, "  ldr q16, [x17]\n") ||
        !expect_text(out, "  ldr q17, [x17]\n") ||
        !expect_text(out, "  str q16, [x17]\n")) return 9;
    fclose(out);

    out = tmpfile();
    if (out == NULL) return 10;
    if (minic_aarch64_neon_emit_eor3(out, &frame, words, false,
                                      0U, 16U, 32U, 48U) ||
        !empty_file(out)) return 11;
    if (minic_aarch64_neon_emit_eor3(out, &frame, bytes, true,
                                     0U, 16U, 32U, 48U) ||
        !empty_file(out)) return 12;
    if (!minic_aarch64_neon_emit_eor3(out, &frame, words, true,
                                      0U, 16U, 32U, 48U) ||
        !expect_text(out, "  .arch_extension sha3\n") ||
        !expect_text(out, "  eor3 v16.16b, v16.16b, v17.16b, v18.16b\n"))
        return 13;
    fclose(out);

    invalid.pointer_depth = 1U;
    out = tmpfile();
    if (out == NULL) return 14;
    if (minic_aarch64_neon_emit_xor(out, &frame, invalid, 0U, 16U, 32U) ||
        !empty_file(out)) return 15;
    invalid = words;
    invalid.base_qualifiers = MINIC_TYPE_QUALIFIER_CONST;
    if (minic_aarch64_neon_emit_load(out, &frame, invalid, 0U, 16U) ||
        !empty_file(out)) return 16;
    fclose(out);

    for (input_index = 0U; input_index < 4U; ++input_index) {
        expressions[input_index].type = words;
        expressions[input_index].value_category = input_index == 0U
            ? MINIC_VALUE_LVALUE : MINIC_VALUE_RVALUE;
    }
    program.expressions = expressions;
    program.expression_count = 4U;
    outputs[0].constraint_text = "=w";
    outputs[0].constraint_length = 2U;
    outputs[0].access = MINIC_INLINE_ASM_OPERAND_WRITE_ONLY;
    outputs[0].expression = 0U;
    for (input_index = 0U; input_index < 3U; ++input_index) {
        inputs[input_index].constraint_text = "w";
        inputs[input_index].constraint_length = 1U;
        inputs[input_index].access = MINIC_INLINE_ASM_OPERAND_READ_ONLY;
        inputs[input_index].expression = input_index + 1U;
    }
    source.template_text = (char *)eor3_template;
    source.template_length = sizeof(eor3_template) - 1U;
    source.outputs = outputs;
    source.output_count = 1U;
    source.inputs = inputs;
    source.input_count = 3U;
    if (!minic_aarch64_neon_sha3_eor3_source_form(&program, &source))
        return 17;
    inputs[1].constraint_text = "r";
    if (minic_aarch64_neon_sha3_eor3_source_form(&program, &source))
        return 18;
    inputs[1].constraint_text = "w";
    expressions[2].type = bytes;
    if (minic_aarch64_neon_sha3_eor3_source_form(&program, &source))
        return 19;
    expressions[2].type = words;
    source.has_memory_clobber = true;
    if (minic_aarch64_neon_sha3_eor3_source_form(&program, &source))
        return 20;
    source.has_memory_clobber = false;
    source.template_text = "eor3 %0.16b, %1.16b, %2.16b, %3.16b";
    if (minic_aarch64_neon_sha3_eor3_source_form(&program, &source))
        return 21;
    return 0;
}

int main(void) {
    const int result = run();
    if (result != 0)
        fprintf(stderr, "AArch64 NEON machine contract failed at check %d\n",
                result);
    return result;
}
