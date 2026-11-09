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
    MinicCoreModuleGlobal global = {.alias_target_index = SIZE_MAX};
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
    global.storage_size = 4U;
    global.fragment_count = 1U;
    if (!emitted_matches(&global, 0U, "  .4byte -1\n", true, true)) return 7;
    /* The same physical payload must serialize under both GNU dialects,
     * regardless of whether the C producer used signed, unsigned or float.
     * No source-type hint exists in the immutable Core fragment. */
    if (!emitted_matches(&global, 0U, "  .4byte 4294967295\n", true, false))
        return 17;
    fragments[0].bits = UINT64_C(0x80000000);
    if (!emitted_matches(&global, 0U, "  .4byte -2147483648\n", true, true) ||
        !emitted_matches(&global, 0U, "  .4byte 2147483648\n", true, false))
        return 18;
    fragments[0].byte_count = global.storage_size = 2U;
    fragments[0].bits = UINT64_C(0xffff);
    if (!emitted_matches(&global, 0U, "  .2byte -1\n", true, true) ||
        !emitted_matches(&global, 0U, "  .2byte 65535\n", true, false))
        return 19;
    fragments[0].byte_count = global.storage_size = 8U;
    fragments[0].bits = UINT64_MAX;
    if (!emitted_matches(&global, 0U, "  .8byte -1\n", true, true) ||
        !emitted_matches(&global, 0U, "  .8byte 18446744073709551615\n", true, false))
        return 20;
    fragments[0].byte_count = 1U;
    fragments[0].bits = UINT64_C(0x1ff);
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
        MinicModuleElfGlobalPlan plan;
        FILE *out = open_memstream(&text, &length);
        if (out == NULL) return 10;
        global.section_name = ".bss.test";
        global.visibility = MINIC_CORE_MODULE_VISIBILITY_HIDDEN;
        global.is_read_only = true;
        /* The preceding standalone relocation fragment is *not* a valid
         * ELF SHT_NOBITS payload. RV's explicit @progbits policy retains it. */
        if (minic_multi_module_elf_global_plan_build(
                &global, ".data", true, lp64_directive, false, 4U, &plan))
            return 11;
        if (!minic_multi_module_elf_global_plan_build(
                &global, ".data", false, lp64_directive, false, 4U, &plan) ||
            strcmp(plan.explicit_section_type, "progbits") != 0)
            return 12;
        fragments[0].kind = MINIC_CORE_MODULE_FRAGMENT_BITS;
        fragments[0].relocation_symbol = NULL;
        fragments[0].bits = 1U;
        if (minic_multi_module_elf_global_plan_build(
                &global, ".data", true, lp64_directive, false, 4U, &plan))
            return 13;
        /* Numeric data is interpreted by its physical width; high bits
         * beyond a byte do not become nonzero ELF section contents. */
        fragments[0].byte_count = global.storage_size = 1U;
        fragments[0].bits = 0x100U;
        if (!minic_multi_module_elf_global_plan_build(
                &global, ".data", true, lp64_directive, false, 4U, &plan) ||
            strcmp(plan.explicit_section_type, "nobits") != 0)
            return 14;
        fragments[0].kind = MINIC_CORE_MODULE_FRAGMENT_ZERO;
        fragments[0].byte_count = global.storage_size = 4U;
        if (!minic_multi_module_elf_global_plan_build(
                &global, ".data", true, lp64_directive, false, 4U, &plan) ||
            !minic_multi_module_elf_global_emit_visibility(out, &plan) ||
            !minic_multi_module_elf_global_emit_section(out, &plan) ||
            !minic_multi_module_elf_global_emit_binding(out, &plan) ||
            fclose(out) != 0) {
            free(text);
            return 15;
        }
        if (strcmp(text,
                   ".hidden test_obj\n.section .bss.test,\"a\",@nobits\n"
                   ".globl test_obj\n") != 0) {
            free(text);
            return 16;
        }
        free(text);
    }
    puts("MODULE_GLOBAL_FRAGMENTS=PASS zero bits source_neutral_signed_dialect widths=1,2,4,8 reloc label strict_layout ELF_section physical_nobits=5");
    return 0;
}
