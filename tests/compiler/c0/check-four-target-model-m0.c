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

/* RV32 is a distinct ILP32 consumer of the existing TargetInfo/DataLayout
 * model. Test GNU C language promotions, not only raw pointer bytes. */
static int verify_rv32(void) {
    const MinicTargetInfo *t = minic_target_info_from_name("riscv32-linux");
    MinicType v;
    if (t == NULL || t->architecture != MINIC_TARGET_ARCH_RISCV32 ||
        t->riscv_isa.xlen != 32U || t->riscv_isa.abi != MINIC_RISCV_ABI_ILP32 ||
        t->data_layout->pointer_size != 4U ||
        t->data_layout->pointer_alignment != 4U ||
        t->data_layout->integer_size[MINIC_INTEGER_RANK_LONG] != 4U ||
        t->data_layout->integer_size[MINIC_INTEGER_RANK_LONG_LONG] != 8U ||
        t->data_layout->integer_alignment[MINIC_INTEGER_RANK_LONG_LONG] != 8U ||
        t->integer_model->semantic_width_bits[MINIC_INTEGER_RANK_LONG] != 32U ||
        t->integer_model->semantic_width_bits[MINIC_INTEGER_RANK_LONG_LONG] != 64U ||
        t->integer_model->plain_char_sign != MINIC_INTEGER_SIGN_UNSIGNED ||
        t->word_integer_rank != MINIC_INTEGER_RANK_INT ||
        t->variadic_scalar_read_kind != MINIC_VARIADIC_SCALAR_READ_UNSUPPORTED ||
        !minic_target_info_integer_common(t, minic_type_unsigned_int(),
                                          minic_type_long(), &v) ||
        !minic_type_equal(v, minic_type_unsigned_long()) ||
        !minic_target_info_integer_common(t, minic_type_unsigned_long(),
                                          minic_type_long_long(), &v) ||
        !minic_type_equal(v, minic_type_long_long()))
        return (void)fprintf(stderr, "RV32_ILP32_MODEL=FAIL\n"), 1;
    return puts("RV32_ILP32_MODEL=PASS xlen=32 ptr=4 long=4 longlong=8 signedness=qualified") == EOF ? 1 : 0;
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
    if (verify_rv32() != 0 ||
        verify_lp64("aarch64-linux", MINIC_TARGET_ARCH_AARCH64,
                    MINIC_INTEGER_SIGN_UNSIGNED, MINIC_INTEGER_RANK_INT,
                    MINIC_INTEGER_SIGN_UNSIGNED) != 0 ||
        verify_lp64("loongarch64-linux", MINIC_TARGET_ARCH_LOONGARCH64,
                    MINIC_INTEGER_SIGN_SIGNED, MINIC_INTEGER_RANK_INT,
                    MINIC_INTEGER_SIGN_SIGNED) != 0) return 1;
    (void)puts("FOUR_BACKEND_TARGET_MODELS_M0=PASS registered=4 distinct_layouts=4 aarch64_char=unsigned loongarch64_char=signed no_machine_codegen_claim=1");
    return 0;
}
