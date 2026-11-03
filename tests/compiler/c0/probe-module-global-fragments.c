#define _GNU_SOURCE
#include "target/common/module_global_emit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *lp64_directive(size_t width) {
    return width == 1U ? ".byte" : width == 2U ? ".2byte" :
           width == 4U ? ".4byte" : width == 8U ? ".8byte" : NULL;
}

static bool emitted_matches(const MinicCoreModuleGlobal *global,
                            size_t relocation_width, const char *expected,
                            bool should_succeed, bool signed_spelling) {
    char *text = NULL;
    size_t length = 0U;
    FILE *out = open_memstream(&text, &length);
    bool success;
    if (out == NULL) return false;
    success = minic_multi_module_emit_global_fragments(
        out, global, lp64_directive, signed_spelling, relocation_width);
    if (fclose(out) != 0) success = false;
    if ((success != should_succeed) || text == NULL ||
        strcmp(text, expected) != 0) {
        free(text);
        return false;
    }
    free(text);
    return true;
}

int main(void) {
    MinicCoreModuleGlobal global = {0};
    MinicCoreModuleFragment fragments[] = {
        {.kind = MINIC_CORE_MODULE_FRAGMENT_ZERO, .byte_offset = 0U,
         .byte_count = 3U},
        {.kind = MINIC_CORE_MODULE_FRAGMENT_BITS, .byte_offset = 3U,
         .byte_count = 1U, .bits = 0x1ffU},
        {.kind = MINIC_CORE_MODULE_FRAGMENT_RELOCATION, .byte_offset = 4U,
         .byte_count = 8U, .relocation_kind = MINIC_CORE_MODULE_RELOCATION_OBJECT,
         .relocation_symbol = "target_obj", .relocation_addend = -16},
        {.kind = MINIC_CORE_MODULE_FRAGMENT_ZERO, .byte_offset = 12U,
         .byte_count = 4U},
        {.kind = MINIC_CORE_MODULE_FRAGMENT_BITS, .byte_offset = 16U,
         .byte_count = 8U, .bits = UINT64_C(18446744073709551615)}
    };
    const char *expected =
        "  .zero 3\n  .byte 255\n  .8byte target_obj-16\n"
        "  .zero 4\n  .8byte 18446744073709551615\n";
    global.name = "test_obj";
    global.storage_size = 24U;
    global.storage_alignment = 8U;
    global.fragments = fragments;
    global.fragment_count = sizeof(fragments) / sizeof(*fragments);
    if (!emitted_matches(&global, 8U, expected, true, false) ||
        !emitted_matches(&global, 4U, "", false, false)) return 1;

    fragments[3].byte_offset = 11U;
    if (!emitted_matches(&global, 8U, "", false, false)) return 2;
    fragments[3].byte_offset = 12U;
    fragments[2].relocation_symbol = "bad\ninjected";
    if (!emitted_matches(&global, 8U, "", false, false)) return 3;
    fragments[2].relocation_symbol = "target_obj";
    fragments[2].byte_count = 16U;
    if (!emitted_matches(&global, 8U, "", false, false)) return 4;
    fragments[2].byte_count = 8U;

    global.storage_size = 0U;
    global.fragment_count = 0U;
    if (!emitted_matches(&global, 8U, "", true, false)) return 5;
    fragments[0].kind = MINIC_CORE_MODULE_FRAGMENT_ZERO;
    fragments[0].byte_count = 0U;
    global.fragment_count = 1U;
    if (!emitted_matches(&global, 8U, "", false, false)) return 6;
    global.fragment_count = 0U;

    fragments[0].kind = MINIC_CORE_MODULE_FRAGMENT_BITS;
    fragments[0].byte_offset = 0U;
    fragments[0].byte_count = 4U;
    fragments[0].bits = UINT64_C(0xffffffff);
    fragments[0].bits_signed_integer = true;
    global.storage_size = 4U;
    global.fragment_count = 1U;
    if (!emitted_matches(&global, 0U, "  .4byte -1\n", true, true)) return 7;
    fragments[0].byte_count = 1U;
    global.storage_size = 1U;
    if (!emitted_matches(&global, 0U, "  .byte 255\n", true, true)) return 8;
    fragments[0].kind = MINIC_CORE_MODULE_FRAGMENT_RELOCATION;
    fragments[0].byte_count = 4U;
    fragments[0].relocation_kind = MINIC_CORE_MODULE_RELOCATION_LABEL;
    fragments[0].relocation_symbol = ".Luser_23";
    global.storage_size = 4U;
    if (!emitted_matches(&global, 4U, "  .4byte .Luser_23\n", true, false))
        return 9;

    {
        char *text = NULL;
        size_t length = 0U;
        FILE *out = open_memstream(&text, &length);
        if (out == NULL) return 10;
        global.section_name = ".bss.test";
        global.visibility = MINIC_CORE_MODULE_VISIBILITY_HIDDEN;
        global.is_read_only = true;
        if (!minic_multi_module_emit_elf_symbol_visibility(
                out, global.visibility, global.name) ||
            !minic_multi_module_emit_elf_section(out, &global, ".data") ||
            !minic_multi_module_emit_elf_binding(out, &global) ||
            fclose(out) != 0) {
            free(text);
            return 11;
        }
        if (strcmp(text,
                   ".hidden test_obj\n.section .bss.test,\"a\",@nobits\n"
                   ".globl test_obj\n") != 0) {
            free(text);
            return 12;
        }
        free(text);
    }
    puts("MODULE_GLOBAL_FRAGMENTS=PASS zero bits signed reloc label strict_layout ELF_section");
    return 0;
}
