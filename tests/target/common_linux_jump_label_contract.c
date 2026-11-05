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
        "k", "_key", ".L123", "Symbols9.$", "a.b"
    };
    static const char *const rejected_keys[] = {
        "", "9key", "$key", "key+", "key-", "key+4", "key+0x10",
        "key++1", "key; .text", "key + 12", "key+1+2", "key\n.balign8"
    };
    MinicCoreInlineAsm source = {0};
    MinicCoreInlineAsmImmediate key = {
        .kind = MINIC_CORE_INLINE_ASM_IMMEDIATE_SYMBOL,
        .symbol = "key", .byte_addend = 4
    };
    char long_key[130];
    char key_text[129];
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
        if (!minic_core_inline_asm_symbol_valid(accepted_keys[i]))
            return 2;
    for (i = 0U; i < sizeof(rejected_keys) / sizeof(rejected_keys[0]); ++i)
        if (minic_core_inline_asm_symbol_valid(rejected_keys[i]))
            return 3;
    (void)memset(long_key, 'a', sizeof(long_key));
    long_key[128] = '\0';
    if (!minic_core_inline_asm_symbol_valid(long_key)) return 4;
    long_key[128] = 'a';
    long_key[129] = '\0';
    if (minic_core_inline_asm_symbol_valid(long_key)) return 5;
    if (!minic_core_inline_asm_relocation_valid(&key) ||
        !minic_multi_linux_jump_relocation_text(
            &key, key_text, sizeof(key_text)) || strcmp(key_text, "key+4") != 0)
        return 11;
    key.byte_addend = -42;
    if (!minic_multi_linux_jump_relocation_text(
            &key, key_text, sizeof(key_text)) || strcmp(key_text, "key-42") != 0)
        return 12;
    key.byte_addend = 4;

    source.template_text = aarch64_template;
    source.template_length = strlen(aarch64_template);
    source.is_goto = true;
    source.is_volatile = true;
    source.deferred_immediate_symbol = NULL;
    source.goto_target = 0U;
    source.goto_second_target = MINIC_CORE_BLOCK_INVALID;
    source.resolved_immediates[0] = key;
    source.resolved_immediate_count = 1U;
    if (!minic_multi_linux_jump_label_asm_goto_core(&source, aarch64_form) ||
        minic_multi_linux_jump_label_asm_goto_core(&source, loong_form))
        return 6;
    source.template_text = loong_template;
    source.template_length = strlen(loong_template);
    if (!minic_multi_linux_jump_label_asm_goto_core(&source, loong_form) ||
        minic_multi_linux_jump_label_asm_goto_core(&source, aarch64_form))
        return 7;
    source.resolved_immediates[0].symbol = "key++4";
    if (minic_multi_linux_jump_label_asm_goto_core(&source, loong_form))
        return 8;
    source.resolved_immediates[0] = key;
    source.deferred_immediate_symbol = "__minic_deferred_asm_immediate_0_0";
    if (minic_multi_linux_jump_label_asm_goto_core(&source, loong_form))
        return 9;
    source.deferred_immediate_symbol = NULL;
    source.goto_second_target = 1U;
    if (minic_multi_linux_jump_label_asm_goto_core(&source, loong_form))
        return 10;

    (void)puts("COMMON_LINUX_JUMP_LABEL_CONTRACT=PASS forms=2 typed_symbol_valid=5 typed_symbol_invalid=12 signed_addend=2 bound=128 core_positive=2 core_negative=5");
    return 0;
}
