/* M0 metadata-only ABI audit. No new target code is emitted here. */
#include "target/target_info.h"

#include <stdio.h>

static int verify_lp64(const char *name,
                       MinicTargetArchitecture expected_arch,
                       MinicIntegerSign expected_char_sign,
                       MinicIntegerRank expected_wchar_rank,
                       MinicIntegerSign expected_wchar_sign) {
    const MinicTargetInfo *info = minic_target_info_from_name(name);
    const MinicDataLayout *layout;
    if (info == NULL || info->architecture != expected_arch ||
        info != minic_target_info_for_architecture(expected_arch) ||
        info->integer_model == NULL || info->data_layout == NULL) {
        (void)fprintf(stderr, "FAIL target identity: %s\n", name);
        return 1;
    }
    layout = info->data_layout;
    if (info->integer_model->plain_char_sign != expected_char_sign ||
        info->integer_model->semantic_width_bits[MINIC_INTEGER_RANK_BOOL] != 1U ||
        info->integer_model->semantic_width_bits[MINIC_INTEGER_RANK_CHAR] != 8U ||
        info->integer_model->semantic_width_bits[MINIC_INTEGER_RANK_SHORT] != 16U ||
        info->integer_model->semantic_width_bits[MINIC_INTEGER_RANK_INT] != 32U ||
        info->integer_model->semantic_width_bits[MINIC_INTEGER_RANK_LONG] != 64U ||
        info->integer_model->semantic_width_bits[MINIC_INTEGER_RANK_INT128] != 128U ||
        info->wide_character_type.integer_rank != expected_wchar_rank ||
        info->wide_character_type.integer_sign != expected_wchar_sign ||
        info->word_integer_rank != MINIC_INTEGER_RANK_LONG) {
        (void)fprintf(stderr, "FAIL integer/char/wchar ABI: %s\n", name);
        return 1;
    }
    if (layout->pointer_size != 8U || layout->pointer_alignment != 8U ||
        layout->integer_size[MINIC_INTEGER_RANK_INT] != 4U ||
        layout->integer_size[MINIC_INTEGER_RANK_LONG] != 8U ||
        layout->integer_size[MINIC_INTEGER_RANK_INT128] != 16U ||
        layout->integer_alignment[MINIC_INTEGER_RANK_INT128] != 16U ||
        layout->float_size != 4U || layout->float_alignment != 4U ||
        layout->double_size != 8U || layout->double_alignment != 8U ||
        layout->long_double_size != 16U || layout->long_double_alignment != 16U) {
        (void)fprintf(stderr, "FAIL LP64 storage ABI: %s\n", name);
        return 1;
    }
    return 0;
}

int main(void) {
    const MinicTargetInfo *rv = minic_default_target_info();
    const MinicTargetInfo *x86 = minic_target_info_from_name("x86_64-linux");
    const MinicTargetInfo *arm = minic_target_info_from_name("aarch64-linux");
    const MinicTargetInfo *loong = minic_target_info_from_name("loongarch64-linux");
    if (rv == NULL || x86 == NULL || arm == NULL || loong == NULL ||
        rv == arm || x86 == arm || rv == loong || x86 == loong ||
        arm == loong || rv->architecture != MINIC_TARGET_ARCH_RISCV64 ||
        x86->architecture != MINIC_TARGET_ARCH_X86_64 ||
        minic_target_info_from_name("mips64-linux") != NULL ||
        minic_target_info_from_name(NULL) != NULL ||
        minic_target_info_for_architecture((MinicTargetArchitecture)999) != NULL) {
        (void)fprintf(stderr, "FAIL four independent target identities\n");
        return 1;
    }
    if (verify_lp64("aarch64-linux", MINIC_TARGET_ARCH_AARCH64,
                    MINIC_INTEGER_SIGN_UNSIGNED, MINIC_INTEGER_RANK_INT,
                    MINIC_INTEGER_SIGN_UNSIGNED) != 0 ||
        verify_lp64("loongarch64-linux", MINIC_TARGET_ARCH_LOONGARCH64,
                    MINIC_INTEGER_SIGN_SIGNED, MINIC_INTEGER_RANK_INT,
                    MINIC_INTEGER_SIGN_SIGNED) != 0) return 1;
    (void)puts("FOUR_BACKEND_TARGET_MODELS_M0=PASS registered=4 distinct_layouts=4 aarch64_char=unsigned loongarch64_char=signed no_machine_codegen_claim=1");
    return 0;
}
