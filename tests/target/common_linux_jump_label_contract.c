/* Target-neutral Linux static-key source/Core grammar, with target-owned
 * %c0 (AArch64) versus %0 (LoongArch64) relocation syntax. */
#include "target/common/linux_jump_label_asm.h"
#include <stdio.h>
#include <string.h>

static char aarch64_template[] =
    "1:nop.pushsection__jump_table,\"aw\".align3"
    ".long1b-.,%l[l_yes]-..quad%c0-..popsection";
static char loong_template[] =
    "1:nop.pushsection__jump_table,\"aw\".align3"
    ".long1b-.,%l[l_yes]-..quad%0-..popsection";
static const char aarch64_record[] =
    ".pushsection__jump_table,\"aw\".align3"
    ".long1b-.,%l[l_yes]-..quad%c0-..popsection";
static const char loong_record[] =
    ".pushsection__jump_table,\"aw\".align3"
    ".long1b-.,%l[l_yes]-..quad%0-..popsection";

static unsigned int aarch64_form(const char *text, size_t length) {
    return minic_multi_linux_jump_label_form(text, length, aarch64_record);
}
static unsigned int loong_form(const char *text, size_t length) {
    return minic_multi_linux_jump_label_form(text, length, loong_record);
}

int main(void) {
    static const char *const accepted_keys[] = {
        "k", "_key", ".L123", "Symbols9.$", "k+0", "k-42", "a.b+123"
    };
    static const char *const rejected_keys[] = {
        "", "9key", "$key", "key+", "key-", "key+0x10",
        "key++1", "key; .text", "key + 12", "key+1+2", "key\n.balign8"
    };
    MinicCoreInlineAsm source = {0};
    char long_key[130];
    size_t i;

    if (aarch64_form(aarch64_template, strlen(aarch64_template)) != 1U ||
        loong_form(loong_template, strlen(loong_template)) != 1U ||
        aarch64_form(loong_template, strlen(loong_template)) != 0U ||
        loong_form(aarch64_template, strlen(aarch64_template)) != 0U ||
        aarch64_form("1:b%l[l_yes].pushsection__jump_table,\"aw\".align3"
                     ".long1b-.,%l[l_yes]-..quad%c0-..popsection",
                     sizeof("1:b%l[l_yes].pushsection__jump_table,\"aw\".align3"
                            ".long1b-.,%l[l_yes]-..quad%c0-..popsection") - 1U) != 2U ||
        aarch64_form(NULL, 0U) != 0U ||
        loong_form("1:nop.pushsection__alien,\"aw\".align3",
                   sizeof("1:nop.pushsection__alien,\"aw\".align3") - 1U) != 0U)
        return 1;

    for (i = 0U; i < sizeof(accepted_keys) / sizeof(accepted_keys[0]); ++i)
        if (!minic_multi_linux_jump_key_relocation(accepted_keys[i]))
            return 2;
    for (i = 0U; i < sizeof(rejected_keys) / sizeof(rejected_keys[0]); ++i)
        if (minic_multi_linux_jump_key_relocation(rejected_keys[i]))
            return 3;
    (void)memset(long_key, 'a', sizeof(long_key));
    long_key[128] = '\0';
    if (!minic_multi_linux_jump_key_relocation(long_key)) return 4;
    long_key[128] = 'a';
    long_key[129] = '\0';
    if (minic_multi_linux_jump_key_relocation(long_key)) return 5;

    source.template_text = aarch64_template;
    source.template_length = strlen(aarch64_template);
    source.is_goto = true;
    source.is_volatile = true;
    source.source_inline_asm_id = SIZE_MAX;
    source.goto_target = 0U;
    source.goto_second_target = MINIC_CORE_BLOCK_INVALID;
    source.resolved_immediates[0] = "key+4";
    source.resolved_immediate_count = 1U;
    if (!minic_multi_linux_jump_label_asm_goto_core(&source, aarch64_form) ||
        minic_multi_linux_jump_label_asm_goto_core(&source, loong_form))
        return 6;
    source.template_text = loong_template;
    source.template_length = strlen(loong_template);
    if (!minic_multi_linux_jump_label_asm_goto_core(&source, loong_form) ||
        minic_multi_linux_jump_label_asm_goto_core(&source, aarch64_form))
        return 7;
    source.resolved_immediates[0] = "key++4";
    if (minic_multi_linux_jump_label_asm_goto_core(&source, loong_form))
        return 8;
    source.resolved_immediates[0] = "key+4";
    source.source_inline_asm_id = 0U;
    if (minic_multi_linux_jump_label_asm_goto_core(&source, loong_form))
        return 9;
    source.source_inline_asm_id = SIZE_MAX;
    source.goto_second_target = 1U;
    if (minic_multi_linux_jump_label_asm_goto_core(&source, loong_form))
        return 10;

    (void)puts("COMMON_LINUX_JUMP_LABEL_CONTRACT=PASS forms=2 key_valid=7 key_invalid=11 bound=128 core_positive=2 core_negative=5");
    return 0;
}
