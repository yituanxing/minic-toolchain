#define _GNU_SOURCE
#include "core/module_ir.h"
#include "target/riscv32/rv32_global_emit.h"
#include "target/riscv64/global_emit.h"
#include "target/x86_64/global_emit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Three externally callable target global emitters must reject forged
 * finalized Modules before printing. The two LP64 direct targets have only
 * a private global emitter, reached through the verified full writer. */
typedef enum ProbeArch { PROBE_RV32, PROBE_RV64, PROBE_X86 } ProbeArch;

static bool probe_emit(ProbeArch arch, const MinicCoreModule *module,
                       bool expect_success, const char *needle) {
    MinicDataLayout layout = {.pointer_size = arch == PROBE_RV32 ? 4U : 8U};
    MinicTargetInfo target = {
        .architecture = arch == PROBE_RV32 ? MINIC_TARGET_ARCH_RISCV32 :
                        arch == PROBE_RV64 ? MINIC_TARGET_ARCH_RISCV64 :
                                            MINIC_TARGET_ARCH_X86_64,
        .data_layout = &layout
    };
    char *output = NULL;
    size_t output_size = 0U;
    FILE *stream = open_memstream(&output, &output_size);
    bool result, valid;
    if (stream == NULL) return false;
    target.riscv_isa.xlen = arch == PROBE_RV32 ? 32U : 64U;
    result = arch == PROBE_RV32
        ? minic_riscv32_emit_module_global(stream, &target, module, 0U)
        : arch == PROBE_RV64
        ? minic_riscv64_emit_module_global(stream, &target, module, 0U)
        : minic_x86_64_emit_module_global(stream, &target, module, 0U);
    if (fclose(stream) != 0) result = false;
    valid = result == expect_success && output != NULL &&
        ((expect_success && (needle == NULL || strstr(output, needle) != NULL)) ||
         (!expect_success && output_size == 0U));
    free(output);
    return valid;
}

static bool probe_case(const MinicCoreModule *module, const char *label,
                       bool core_valid, bool rv32_valid, bool rv64_valid,
                       bool x86_valid, const char *rv_needle,
                       const char *x86_needle) {
    const bool global_valid = minic_core_module_verify_global(module, 0U);
    const bool full_valid = minic_core_module_verify(module);
    if (global_valid != core_valid || full_valid != core_valid ||
        !probe_emit(PROBE_RV32, module, rv32_valid, rv_needle) ||
        !probe_emit(PROBE_RV64, module, rv64_valid, rv_needle) ||
        !probe_emit(PROBE_X86, module, x86_valid, x86_needle)) {
        (void)fprintf(stderr, "DIRECT_GLOBAL_CONTRACT_FAILED=%s core=%d full=%d\n",
                      label, (int)global_valid, (int)full_valid);
        return false;
    }
    return true;
}

int main(void) {
    MinicCoreTypeContext types;
    MinicCoreType byte = {.kind = MINIC_CORE_TYPE_INTEGER,
                          .has_layout = true, .size = 1U, .alignment = 1U,
                          .value.integer.bit_width = 8U};
    MinicCoreType word = {.kind = MINIC_CORE_TYPE_INTEGER,
                          .has_layout = true, .size = 4U, .alignment = 4U,
                          .value.integer.bit_width = 32U};
    MinicCoreType wide = {.kind = MINIC_CORE_TYPE_INTEGER,
                          .has_layout = true, .size = 8U, .alignment = 8U,
                          .value.integer.bit_width = 64U};
    MinicCoreType array = {.kind = MINIC_CORE_TYPE_ARRAY,
                           .has_layout = true, .size = 16U, .alignment = 4U};
    MinicCoreType flexible = {.kind = MINIC_CORE_TYPE_RECORD,
                              .has_layout = true, .size = 8U, .alignment = 8U};
    MinicCoreType incomplete = {.kind = MINIC_CORE_TYPE_ARRAY};
    MinicCoreType pointer = {.kind = MINIC_CORE_TYPE_POINTER,
                             .has_layout = true, .size = 4U, .alignment = 4U};
    MinicCoreRecordField fields[2] = {
        {.byte_offset = 0U, .alignment = 8U},
        {.byte_offset = 8U, .alignment = 4U}
    };
    MinicCoreTypeId byte_id, word_id, wide_id, array_id, incomplete_id;
    MinicCoreTypeId flexible_id, ptr4_id, ptr8_id;
    MinicCoreModuleFragment zero = {
        .kind = MINIC_CORE_MODULE_FRAGMENT_ZERO, .byte_count = 4U
    };
    MinicCoreModuleFragment bits = {
        .kind = MINIC_CORE_MODULE_FRAGMENT_BITS, .byte_count = 4U, .bits = 42U
    };
    MinicCoreModuleFragment reloc = {
        .kind = MINIC_CORE_MODULE_FRAGMENT_RELOCATION,
        .byte_count = 8U, .relocation_kind = MINIC_CORE_MODULE_RELOCATION_OBJECT,
        .relocation_symbol = "external"
    };
    MinicCoreModuleReference reference = {
        .kind = MINIC_CORE_MODULE_REFERENCE_OBJECT, .target_index = 1U
    };
    MinicCoreModuleGlobal globals[2] = {
        {.name = "owned", .emittable = true, .storage_size = 4U,
         .storage_alignment = 4U, .fragments = &zero, .fragment_count = 1U,
         .is_zero_initialized = true,
         .alias_target_index = SIZE_MAX, .owner_function_index = SIZE_MAX},
        {.name = "external", .is_extern = true,
         .alias_target_index = SIZE_MAX, .owner_function_index = SIZE_MAX}
    };
    MinicCoreModule module = {
        .globals = globals, .global_count = 2U, .finalized = true,
        .type_context = &types
    };
    MinicCoreModuleGlobal *object = &globals[0];
    bool success = false;
    minic_core_type_context_initialize(&types);
    if (!minic_core_type_register(&types, &byte, &byte_id) ||
        !minic_core_type_register(&types, &word, &word_id) ||
        !minic_core_type_register(&types, &wide, &wide_id)) goto done;
    array.value.array.element = word_id;
    array.value.array.count = 4U;
    array.value.array.is_complete = true;
    if (!minic_core_type_register(&types, &array, &array_id)) goto done;
    incomplete.value.array.element = word_id;
    if (!minic_core_type_register(&types, &incomplete, &incomplete_id)) goto done;
    fields[0].type_id = wide_id;
    fields[1].type_id = incomplete_id;
    flexible.value.record.is_complete = true;
    flexible.value.record.fields = fields;
    flexible.value.record.field_count = 2U;
    if (!minic_core_type_register(&types, &flexible, &flexible_id)) goto done;
    pointer.value.pointer.pointee = word_id;
    if (!minic_core_type_register(&types, &pointer, &ptr4_id)) goto done;
    pointer.size = pointer.alignment = 8U;
    if (!minic_core_type_register(&types, &pointer, &ptr8_id)) goto done;
    globals[1].core_type_id = word_id;
    object->core_type_id = word_id;

#define CHECK_CASE(label, core, rv32, rv64, x86, rvtext, x86text) \
    do { if (!probe_case(&module, label, core, rv32, rv64, x86, rvtext, x86text)) \
        goto done; } while (0)
    CHECK_CASE("matching_zero", true, true, true, true, NULL, NULL);
    zero.byte_count = object->storage_size = 8U;
    CHECK_CASE("wrong_scalar_size", false, false, false, false, NULL, NULL);
    zero.byte_count = object->storage_size = 4U;
    object->storage_alignment = 1U;
    CHECK_CASE("underalignment", false, false, false, false, NULL, NULL);
    object->storage_alignment = 4U;
    object->core_type_id = array_id;
    object->storage_size = 0U;
    object->fragments = NULL;
    object->fragment_count = 0U;
    CHECK_CASE("complete_array_zero_extent", false, false, false, false, NULL, NULL);
    object->core_type_id = flexible_id;
    object->storage_size = zero.byte_count = 20U;
    object->storage_alignment = 8U;
    object->fragments = &zero;
    object->fragment_count = 1U;
    CHECK_CASE("flexible_tail", true, true, true, true, NULL, NULL);
    object->core_type_id = byte_id;
    object->storage_size = object->storage_alignment = 1U;
    object->fragments = &bits;
    bits.byte_count = 1U;
    bits.bits = 0x100U;
    CHECK_CASE("zero_visible_byte", true, true, true, true, NULL, NULL);
    bits.bits = 42U;
    CHECK_CASE("zero_tag_nonzero_bits", false, false, false, false, NULL, NULL);
    object->is_zero_initialized = false;
    CHECK_CASE("default_bss_nonzero", false, false, false, false, NULL, NULL);
    object->is_zero_initialized = true;
    object->is_tentative = true;
    CHECK_CASE("conflicting_flags", false, false, false, false, NULL, NULL);
    object->is_tentative = false;
    object->is_zero_initialized = false;
    object->default_section = MINIC_CORE_MODULE_SECTION_DATA;
    object->core_type_id = word_id;
    object->storage_size = object->storage_alignment = bits.byte_count = 4U;
    object->section_name = ".bss.direct.nonzero";
    CHECK_CASE("explicit_bss_nonzero", true, true, true, false,
               "@progbits", NULL);
    object->section_name = NULL;

    /* A source-free pointer may legitimately carry a relocation even when
     * its source numeric initializer slots are all zero. */
    object->core_type_id = ptr8_id;
    object->storage_size = object->storage_alignment = reloc.byte_count = 8U;
    object->fragments = &reloc;
    object->references = &reference;
    object->reference_count = 1U;
    object->is_zero_initialized = true;
    CHECK_CASE("zero_tag_symbol_relocation", true, false, true, true, NULL, NULL);
    object->is_tentative = true;
    object->is_zero_initialized = false;
    CHECK_CASE("tentative_relocation", false, false, false, false, NULL, NULL);
    object->is_tentative = false;
    object->default_section = MINIC_CORE_MODULE_SECTION_BSS;
    CHECK_CASE("bss_relocation", false, false, false, false, NULL, NULL);
    object->default_section = MINIC_CORE_MODULE_SECTION_DATA;
    object->references = NULL;
    object->reference_count = 0U;
    CHECK_CASE("unmatched_relocation", false, false, false, false, NULL, NULL);

    /* A forged function relocation must never index a missing Module
     * function-symbol table merely because finalized was set by its maker. */
    {
        MinicCoreModuleReference function_reference = {
            .kind = MINIC_CORE_MODULE_REFERENCE_FUNCTION, .target_index = 0U
        };
        module.function_count = 1U;
        module.function_symbols = NULL;
        reloc.relocation_kind = MINIC_CORE_MODULE_RELOCATION_FUNCTION;
        reloc.relocation_symbol = "missing_function_symbol";
        object->references = &function_reference;
        object->reference_count = 1U;
        CHECK_CASE("missing_function_symbols", false, false, false, false,
                   NULL, NULL);
        module.function_count = 0U;
        object->references = NULL;
        object->reference_count = 0U;
        reloc.relocation_kind = MINIC_CORE_MODULE_RELOCATION_OBJECT;
        reloc.relocation_symbol = "external";
    }

    /* RV32's corresponding legal relocation is pointer-width 4, unlike
     * both LP64 emitters; test that target's positive case on its own. */
    object->core_type_id = ptr4_id;
    object->storage_size = object->storage_alignment = reloc.byte_count = 4U;
    object->references = &reference;
    object->reference_count = 1U;
    object->is_zero_initialized = true;
    if (!minic_core_module_verify_global(&module, 0U) ||
        !probe_emit(PROBE_RV32, &module, true, NULL)) goto done;

    object->is_extern = true;
    object->storage_size = 0U;
    object->storage_alignment = 0U;
    object->fragments = NULL;
    object->fragment_count = 0U;
    object->references = NULL;
    object->reference_count = 0U;
    CHECK_CASE("extern_noop", true, true, false, false, NULL, NULL);
    success = true;
#undef CHECK_CASE
done:
    minic_core_type_context_destroy(&types);
    if (success)
        puts("DIRECT_GLOBAL_MODULE_CONTRACT=PASS three_public_emitters=3 core_local_global_preflight=1 forged_finalized=10 legal_abi_exceptions=5");
    return success ? 0 : 1;
}
