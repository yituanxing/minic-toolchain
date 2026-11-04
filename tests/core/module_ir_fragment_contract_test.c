/* One target-neutral fragment contract for ModuleIR verification and every
 * architecture's emitter; neither may accept malformed ranges or symbols. */
#include "core/module_ir.h"

#include <stdio.h>
#include <stdint.h>

#define CHECK(test, code) do { if (!(test)) return (code); } while (0)

static int verify_module_provenance_edges(const MinicCoreModule *baseline) {
    MinicCoreModuleFunctionSymbol functions[3] = {
        {.name = "source", .is_defined = true, .alias_target_index = SIZE_MAX,
         .specialization_source_index = SIZE_MAX},
        {.name = "clone", .is_defined = true, .alias_target_index = SIZE_MAX,
         .specialization_source_index = 0U},
        {.name = "other_clone", .is_defined = true, .alias_target_index = SIZE_MAX,
         .specialization_source_index = 0U}
    };
    MinicCoreModuleReference label = {
        .kind = MINIC_CORE_MODULE_REFERENCE_OWNER_LABEL, .target_index = 0U
    };
    MinicCoreModuleGlobal local = {
        .name = "local_table", .alias_target_index = SIZE_MAX,
        .owner_function_index = 0U, .references = &label,
        .reference_count = 1U
    };
    MinicCoreModule module = *baseline;

    /* A canonical defined source may own multiple specialization clones,
     * and a static label reference may name its matching canonical owner. */
    module.function_symbols = functions;
    module.function_count = 3U;
    module.globals = &local;
    module.global_count = 1U;
    CHECK(minic_core_module_verify(&module), 20);

    functions[1].specialization_source_index = 1U;
    CHECK(!minic_core_module_verify(&module), 21); /* clone is its own source */
    functions[1].specialization_source_index = 3U;
    CHECK(!minic_core_module_verify(&module), 22); /* out of range */
    functions[1].specialization_source_index = 0U;
    functions[0].is_defined = false;
    CHECK(!minic_core_module_verify(&module), 23); /* undefined source */
    functions[0].is_defined = true;
    functions[2].specialization_source_index = 1U;
    CHECK(!minic_core_module_verify(&module), 24); /* nested clone source */
    functions[2].specialization_source_index = 0U;
    CHECK(minic_core_module_verify(&module), 25);

    label.target_index = 1U;
    CHECK(!minic_core_module_verify(&module), 26); /* wrong owner index */
    local.owner_function_index = 1U;
    CHECK(!minic_core_module_verify(&module), 27); /* clone cannot own label */
    local.owner_function_index = 0U;
    label.target_index = 0U;
    functions[1].specialization_source_index = SIZE_MAX;
    functions[2].specialization_source_index = SIZE_MAX;
    functions[0].is_defined = false;
    CHECK(!minic_core_module_verify(&module), 28); /* undefined label owner */
    functions[0].is_defined = true;
    local.owner_function_index = SIZE_MAX;
    CHECK(!minic_core_module_verify(&module), 29); /* orphaned source label */
    local.owner_function_index = 0U;
    label.target_index = 3U;
    CHECK(!minic_core_module_verify(&module), 30); /* forged out-of-range */
    label.target_index = 0U;
    CHECK(minic_core_module_verify(&module), 31);
    return 0;
}

static int verify_module_alias_index_ownership(const MinicCoreModule *baseline) {
    MinicCoreModuleGlobal objects[2] = {
        {.name = "real_object", .alias_target_index = SIZE_MAX},
        {.name = "object_alias", .is_extern = true, .alias_target_index = 0U}
    };
    MinicCoreModuleFunctionSymbol functions[2] = {
        {.name = "real_function", .is_defined = true,
         .alias_target_index = SIZE_MAX, .specialization_source_index = SIZE_MAX},
        {.name = "function_alias", .alias_target_index = 0U,
         .specialization_source_index = SIZE_MAX}
    };
    MinicCoreModule module = *baseline;
    module.globals = objects;
    module.global_count = 2U;
    module.function_symbols = functions;
    module.function_count = 2U;

    /* Symbol indices and the target's sealed canonical name are the single
     * alias identity. No duplicate alias-name string or alias boolean. */
    CHECK(minic_core_module_verify(&module), 40);
    objects[1].alias_target_index = 2U;
    CHECK(!minic_core_module_verify(&module), 41); /* out of range */
    objects[1].alias_target_index = 1U;
    CHECK(!minic_core_module_verify(&module), 42); /* self alias */
    objects[1].alias_target_index = 0U;
    objects[0].is_extern = true;
    CHECK(!minic_core_module_verify(&module), 43); /* undefined owner */
    objects[0].is_extern = false;
    objects[0].is_tentative = true;
    CHECK(!minic_core_module_verify(&module), 44); /* tentative owner */
    objects[0].is_tentative = false;
    objects[0].alias_target_index = 1U;
    CHECK(!minic_core_module_verify(&module), 45); /* chained alias */
    objects[0].alias_target_index = SIZE_MAX;
    objects[0].name = NULL;
    CHECK(!minic_core_module_verify(&module), 46); /* unnamed target */
    objects[0].name = "real_object";
    CHECK(minic_core_module_verify(&module), 47);

    functions[1].alias_target_index = 2U;
    CHECK(!minic_core_module_verify(&module), 48); /* out of range */
    functions[1].alias_target_index = 1U;
    CHECK(!minic_core_module_verify(&module), 49); /* self alias */
    functions[1].alias_target_index = 0U;
    functions[0].is_defined = false;
    CHECK(!minic_core_module_verify(&module), 50); /* undefined owner */
    functions[0].is_defined = true;
    functions[0].alias_target_index = 1U;
    CHECK(!minic_core_module_verify(&module), 51); /* chained alias */
    functions[0].alias_target_index = SIZE_MAX;
    functions[1].is_defined = true;
    CHECK(!minic_core_module_verify(&module), 52); /* defined alias */
    functions[1].is_defined = false;
    CHECK(minic_core_module_verify(&module), 53);
    return 0;
}

static int verify_module_function_symbol_range(const MinicCoreModule *baseline) {
    MinicCoreModuleFunctionSymbol functions[3] = {
        {.name = "shared", .source_name = "external_c_name",
         .specialization_source_index = SIZE_MAX, .alias_target_index = SIZE_MAX},
        {.name = "shared", .source_name = "static_c_name", .is_defined = true,
         .is_internal = true, .is_referenced = true,
         .specialization_source_index = SIZE_MAX, .alias_target_index = SIZE_MAX},
        {.name = "elsewhere", .is_defined = true,
         .specialization_source_index = SIZE_MAX, .alias_target_index = SIZE_MAX}
    };
    MinicCoreModule module = *baseline;
    MinicCoreModuleFunctionNameIndex names = {0};
    size_t begin, end;
    module.function_symbols = functions;
    module.function_count = 3U;
    CHECK(minic_core_module_function_name_index_build(&module, &names), 60);
    CHECK(minic_core_module_function_name_index_find(
        &names, "shared", 6U, &begin, &end) && end - begin == 2U &&
        names.entries[begin].function_index == 0U &&
        names.entries[begin + 1U].function_index == 1U, 61);
    CHECK(minic_core_module_function_name_index_find(
        &names, "helper_external", 15U, &begin, &end) && begin == end, 62);
    CHECK(!minic_core_module_function_name_index_build(&module, &names), 63);
    minic_core_module_function_name_index_destroy(&names);
    CHECK(names.entries == NULL && names.count == 0U, 64);
    CHECK(minic_core_module_verify(&module), 65); /* extern + live static */
    functions[0].is_defined = true;
    CHECK(!minic_core_module_verify(&module), 66); /* two emitted definitions */
    functions[0].is_defined = false;
    functions[1].is_referenced = false;
    CHECK(minic_core_module_verify(&module), 67); /* unused local is pruned */
    functions[1].name = NULL;
    CHECK(!minic_core_module_function_name_index_build(&module, &names), 68);
    minic_core_module_function_name_index_destroy(&names);
    return 0;
}

static int verify_module_emitted_symbol_namespace(
    const MinicCoreModule *baseline, MinicCoreTypeContext *types) {
    MinicCoreType byte_type = {0};
    MinicCoreTypeId byte_id;
    MinicCoreModuleFragment byte_payload = {
        .kind = MINIC_CORE_MODULE_FRAGMENT_ZERO,
        .byte_offset = 0U, .byte_count = 1U
    };
    MinicCoreModuleFunctionSymbol functions[5] = {
        {.name = "real_function", .is_defined = true,
         .alias_target_index = SIZE_MAX, .specialization_source_index = SIZE_MAX},
        {.name = "function_alias", .alias_target_index = 0U,
         .specialization_source_index = SIZE_MAX},
        {.name = "function_alias", /* declaration, not another emitted symbol */
         .alias_target_index = SIZE_MAX, .specialization_source_index = SIZE_MAX},
        {.name = "another_function", .is_defined = true,
         .alias_target_index = SIZE_MAX, .specialization_source_index = SIZE_MAX},
        {.name = "another_alias", .alias_target_index = 0U,
         .specialization_source_index = SIZE_MAX}
    };
    MinicCoreModuleGlobal objects[3] = {
        {.name = "real_object", .emittable = true,
         .core_type_id = MINIC_CORE_TYPE_INVALID, .storage_size = 1U,
         .storage_alignment = 1U, .fragments = &byte_payload,
         .fragment_count = 1U, .alias_target_index = SIZE_MAX,
         .owner_function_index = SIZE_MAX},
        {.name = "object_alias", .is_extern = true, .emittable = true,
         .alias_target_index = 0U, .owner_function_index = SIZE_MAX},
        {.name = "declaration_only", .is_extern = true,
         .alias_target_index = SIZE_MAX, .owner_function_index = SIZE_MAX}
    };
    MinicCoreModule module = *baseline;

    byte_type.kind = MINIC_CORE_TYPE_INTEGER;
    byte_type.has_layout = true;
    byte_type.size = 1U;
    byte_type.alignment = 1U;
    byte_type.value.integer.bit_width = 8U;
    CHECK(minic_core_type_register(types, &byte_type, &byte_id), 70);
    objects[0].core_type_id = byte_id;
    module.function_symbols = functions;
    module.function_count = 5U;
    module.globals = objects;
    module.global_count = 3U;
    CHECK(minic_core_module_verify(&module), 71); /* declaration + one alias */

    functions[1].name = "real_function";
    CHECK(!minic_core_module_verify(&module), 72); /* alias equals its owner */
    functions[1].name = "another_function";
    CHECK(!minic_core_module_verify(&module), 73); /* alias vs live definition */
    functions[1].name = "another_alias";
    CHECK(!minic_core_module_verify(&module), 74); /* two aliases */
    functions[1].name = "real_object";
    CHECK(!minic_core_module_verify(&module), 75); /* function alias vs object */
    functions[1].name = "object_alias";
    CHECK(!minic_core_module_verify(&module), 76); /* function vs object alias */
    functions[1].name = "function_alias";
    CHECK(minic_core_module_verify(&module), 77);

    objects[1].name = "real_object";
    CHECK(!minic_core_module_verify(&module), 78); /* object alias self-name */
    objects[1].name = "another_function";
    CHECK(!minic_core_module_verify(&module), 79); /* object alias vs function */
    objects[1].name = "function_alias";
    CHECK(!minic_core_module_verify(&module), 80); /* object alias vs function alias */
    objects[1].name = "object_alias";
    CHECK(minic_core_module_verify(&module), 81);

    functions[0].name = "real_object";
    CHECK(!minic_core_module_verify(&module), 82); /* function vs object */
    functions[0].name = "real_function";
    objects[0].name = "function_alias";
    CHECK(!minic_core_module_verify(&module), 83); /* object vs function alias */
    objects[0].name = "real_object";

    functions[3].name = "function_alias";
    functions[3].is_internal = true;
    functions[3].is_referenced = false;
    CHECK(minic_core_module_verify(&module), 84); /* dead static has no symbol */
    functions[3].is_referenced = true;
    CHECK(!minic_core_module_verify(&module), 85); /* now live and colliding */
    functions[3].name = "another_function";
    functions[3].is_internal = false;
    objects[2].name = "real_function";
    CHECK(minic_core_module_verify(&module), 86); /* extern-only has no symbol */
    objects[2].is_extern = false;
    CHECK(minic_core_module_verify(&module), 87); /* pruned object has no symbol */
    objects[2].is_extern = true;
    functions[1].is_internal = true;
    functions[1].is_referenced = false;
    functions[1].name = "another_function";
    CHECK(!minic_core_module_verify(&module), 88); /* aliases emit even unreferenced */
    return 0;
}

static int verify_materialized_relocation_edges(
    const MinicCoreModule *baseline, MinicCoreTypeContext *types) {
    MinicCoreType word = {0}, array = {0};
    MinicCoreTypeId word_id, array_id;
    MinicCoreModuleReference references[2] = {
        {.kind = MINIC_CORE_MODULE_REFERENCE_OBJECT, .target_index = 1U},
        {.kind = MINIC_CORE_MODULE_REFERENCE_FUNCTION, .target_index = 0U}
    };
    MinicCoreModuleFragment fragments[2] = {
        {.kind = MINIC_CORE_MODULE_FRAGMENT_RELOCATION,
         .byte_offset = 0U, .byte_count = 8U,
         .relocation_kind = MINIC_CORE_MODULE_RELOCATION_OBJECT,
         .relocation_symbol = "target_object"},
        {.kind = MINIC_CORE_MODULE_FRAGMENT_RELOCATION,
         .byte_offset = 8U, .byte_count = 8U,
         .relocation_kind = MINIC_CORE_MODULE_RELOCATION_FUNCTION,
         .relocation_symbol = "callee"}
    };
    MinicCoreModuleGlobal globals[2] = {
        {.name = "live_payload", .emittable = true,
         .storage_size = 16U, .storage_alignment = 8U,
         .default_section = MINIC_CORE_MODULE_SECTION_DATA,
         .references = references, .reference_count = 2U,
         .fragments = fragments, .fragment_count = 2U,
         .alias_target_index = SIZE_MAX, .owner_function_index = SIZE_MAX},
        {.name = "target_object", .is_extern = true,
         .alias_target_index = SIZE_MAX, .owner_function_index = SIZE_MAX}
    };
    MinicCoreModuleFunctionSymbol functions[1] = {
        {.name = "callee", .is_defined = true,
         .alias_target_index = SIZE_MAX,
         .specialization_source_index = SIZE_MAX}
    };
    MinicCoreModule module = *baseline;

    word.kind = MINIC_CORE_TYPE_INTEGER;
    word.has_layout = true;
    word.size = 8U;
    word.alignment = 8U;
    word.value.integer.bit_width = 64U;
    CHECK(minic_core_type_register(types, &word, &word_id), 90);
    array.kind = MINIC_CORE_TYPE_ARRAY;
    array.has_layout = true;
    array.size = 16U;
    array.alignment = 8U;
    array.value.array.element = word_id;
    array.value.array.count = 2U;
    array.value.array.is_complete = true;
    CHECK(minic_core_type_register(types, &array, &array_id), 91);
    globals[0].core_type_id = array_id;
    module.globals = globals;
    module.global_count = 2U;
    module.function_symbols = functions;
    module.function_count = 1U;
    CHECK(minic_core_module_verify(&module), 92); /* correct ordered edges */

    globals[0].reference_count = 1U;
    CHECK(!minic_core_module_verify(&module), 93); /* extra ELF relocation */
    globals[0].reference_count = 2U;
    fragments[1].kind = MINIC_CORE_MODULE_FRAGMENT_ZERO;
    fragments[1].relocation_symbol = NULL;
    CHECK(!minic_core_module_verify(&module), 94); /* missing ELF relocation */
    fragments[1].kind = MINIC_CORE_MODULE_FRAGMENT_RELOCATION;
    fragments[1].relocation_symbol = "callee";
    fragments[0].relocation_symbol = "wrong_object";
    CHECK(!minic_core_module_verify(&module), 95); /* different target symbol */
    fragments[0].relocation_symbol = "target_object";
    references[0].kind = MINIC_CORE_MODULE_REFERENCE_FUNCTION;
    CHECK(!minic_core_module_verify(&module), 96); /* mismatched target kind */
    references[0].kind = MINIC_CORE_MODULE_REFERENCE_OBJECT;
    references[0].target_index = 0U;
    CHECK(!minic_core_module_verify(&module), 97); /* wrong target identity */
    references[0].target_index = 1U;
    references[0].kind = MINIC_CORE_MODULE_REFERENCE_OWNER_LABEL;
    references[0].target_index = 0U;
    globals[0].owner_function_index = 0U;
    fragments[0].relocation_kind = MINIC_CORE_MODULE_RELOCATION_LABEL;
    fragments[0].relocation_symbol = ".Luser_23";
    CHECK(minic_core_module_verify(&module), 98); /* a legal owner label */
    fragments[0].relocation_symbol = "target_object";
    CHECK(!minic_core_module_verify(&module), 99); /* not a lexical Core label */
    references[0].kind = MINIC_CORE_MODULE_REFERENCE_OBJECT;
    references[0].target_index = 1U;
    globals[0].owner_function_index = SIZE_MAX;
    globals[0].emittable = false;
    globals[0].fragments = NULL;
    globals[0].fragment_count = 0U;
    CHECK(minic_core_module_verify(&module), 101); /* dead data keeps edges, no bytes */
    globals[0].is_extern = true;
    globals[0].emittable = true;
    CHECK(minic_core_module_verify(&module), 102); /* unmaterialized declaration */
    return 0;
}

/* A hand-built Core Module producer has no C initializer syntax or slot
 * cardinality. It can still seal the linker-relevant definition class,
 * typed storage and exactly covered bytes for downstream RV32/RV64 policy. */
static int verify_source_free_zero_definition_facts(
    const MinicCoreModule *baseline, MinicCoreTypeContext *types) {
    MinicCoreType word = {0};
    MinicCoreTypeId word_id;
    MinicCoreModuleFragment zero = {
        .kind = MINIC_CORE_MODULE_FRAGMENT_ZERO,
        .byte_offset = 0U, .byte_count = 4U
    };
    MinicCoreModuleGlobal objects[3] = {
        {.name = "weak_tentative", .is_weak = true,
         .is_tentative = true, .emittable = true,
         .storage_size = 4U, .storage_alignment = 4U,
         .fragments = &zero, .fragment_count = 1U,
         .alias_target_index = SIZE_MAX, .owner_function_index = SIZE_MAX},
        {.name = "weak_empty", .is_weak = true,
         .is_zero_initialized = true, .emittable = true,
         .storage_size = 4U, .storage_alignment = 4U,
         .fragments = &zero, .fragment_count = 1U,
         .alias_target_index = SIZE_MAX, .owner_function_index = SIZE_MAX},
        {.name = "weak_counted_zero", .is_weak = true,
         .emittable = true, .storage_size = 4U,
         .storage_alignment = 4U, .fragments = &zero,
         .fragment_count = 1U, .alias_target_index = SIZE_MAX,
         .owner_function_index = SIZE_MAX}
    };
    MinicCoreModule module = *baseline;
    size_t i;

    word.kind = MINIC_CORE_TYPE_INTEGER;
    word.has_layout = true;
    word.size = 4U;
    word.alignment = 4U;
    word.value.integer.bit_width = 32U;
    CHECK(minic_core_type_register(types, &word, &word_id), 110);
    for (i = 0U; i < 3U; ++i) objects[i].core_type_id = word_id;
    module.globals = objects;
    module.global_count = 3U;
    CHECK(minic_core_module_verify(&module), 111);
    CHECK(objects[0].is_tentative && !objects[1].is_tentative &&
          objects[1].is_zero_initialized && !objects[2].is_zero_initialized &&
          objects[0].fragments == objects[1].fragments &&
          objects[1].fragments == objects[2].fragments, 112);
    return 0;
}

/* A second Core producer need not have C initializer slots, but must seal
 * the exact typed object extent, a physical zero/relocation contract, and
 * the distinction between default ELF BSS and explicit section spelling. */
static int verify_source_free_global_definition_contract(
    const MinicCoreModule *baseline, MinicCoreTypeContext *types) {
    MinicCoreType byte = {0}, word = {0}, wide = {0}, array = {0};
    MinicCoreType incomplete = {0}, flexible = {0}, empty = {0};
    MinicCoreType qualified = {0};
    MinicCoreTypeId byte_id, word_id, wide_id, array_id;
    MinicCoreTypeId incomplete_id, flexible_id, zero_array_id, empty_id;
    MinicCoreTypeId qualified_record_id, qualified_tail_id, qualified_flexible_id;
    MinicCoreRecordField members[2] = {
        {.byte_offset = 0U, .alignment = 8U},
        {.byte_offset = 8U, .alignment = 4U}
    };
    MinicCoreModuleFragment zero = {
        .kind = MINIC_CORE_MODULE_FRAGMENT_ZERO, .byte_count = 4U
    };
    MinicCoreModuleFragment bits = {
        .kind = MINIC_CORE_MODULE_FRAGMENT_BITS, .byte_count = 4U,
        .bits = 42U
    };
    MinicCoreModuleFragment reloc = {
        .kind = MINIC_CORE_MODULE_FRAGMENT_RELOCATION,
        .byte_count = 4U, .relocation_kind = MINIC_CORE_MODULE_RELOCATION_OBJECT,
        .relocation_symbol = "defined_elsewhere"
    };
    MinicCoreModuleReference reference = {
        .kind = MINIC_CORE_MODULE_REFERENCE_OBJECT, .target_index = 1U
    };
    MinicCoreModuleGlobal globals[2] = {
        {.name = "source_free_object", .emittable = true,
         .storage_size = 4U, .storage_alignment = 4U,
         .fragments = &zero, .fragment_count = 1U,
         .alias_target_index = SIZE_MAX, .owner_function_index = SIZE_MAX},
        {.name = "defined_elsewhere", .is_extern = true,
         .alias_target_index = SIZE_MAX, .owner_function_index = SIZE_MAX}
    };
    MinicCoreModule module = *baseline;
    MinicCoreModuleGlobal *object = &globals[0];

    byte.kind = word.kind = wide.kind = MINIC_CORE_TYPE_INTEGER;
    byte.has_layout = word.has_layout = wide.has_layout = true;
    byte.size = byte.alignment = 1U;
    byte.value.integer.bit_width = 8U;
    word.size = word.alignment = 4U;
    word.value.integer.bit_width = 32U;
    wide.size = wide.alignment = 8U;
    wide.value.integer.bit_width = 64U;
    CHECK(minic_core_type_register(types, &byte, &byte_id), 120);
    CHECK(minic_core_type_register(types, &word, &word_id), 121);
    CHECK(minic_core_type_register(types, &wide, &wide_id), 122);
    array.kind = MINIC_CORE_TYPE_ARRAY;
    array.has_layout = true;
    array.size = 16U;
    array.alignment = 4U;
    array.value.array.element = word_id;
    array.value.array.count = 4U;
    array.value.array.is_complete = true;
    CHECK(minic_core_type_register(types, &array, &array_id), 123);
    incomplete.kind = MINIC_CORE_TYPE_ARRAY;
    incomplete.value.array.element = word_id;
    CHECK(minic_core_type_register(types, &incomplete, &incomplete_id), 124);
    members[0].type_id = wide_id;
    members[1].type_id = incomplete_id;
    flexible.kind = MINIC_CORE_TYPE_RECORD;
    flexible.has_layout = true;
    flexible.size = 8U;
    flexible.alignment = 8U;
    flexible.value.record.is_complete = true;
    flexible.value.record.fields = members;
    flexible.value.record.field_count = 2U;
    CHECK(minic_core_type_register(types, &flexible, &flexible_id), 125);
    array.value.array.element = byte_id;
    array.value.array.count = 0U;
    array.size = 0U;
    array.alignment = 1U;
    CHECK(minic_core_type_register(types, &array, &zero_array_id), 126);
    empty.kind = MINIC_CORE_TYPE_RECORD;
    empty.has_layout = true;
    empty.alignment = 1U;
    empty.value.record.is_complete = true;
    CHECK(minic_core_type_register(types, &empty, &empty_id), 127);

    object->core_type_id = word_id;
    module.globals = globals;
    module.global_count = 2U;
    CHECK(minic_core_module_verify(&module), 128); /* matching scalar */

    zero.byte_count = object->storage_size = 8U;
    CHECK(!minic_core_module_verify(&module), 129); /* scalar 4 -> 8 */
    zero.byte_count = object->storage_size = 4U;
    object->storage_alignment = 1U;
    CHECK(!minic_core_module_verify(&module), 130); /* underaligned */
    object->storage_alignment = 4U;

    object->core_type_id = array_id;
    zero.byte_count = object->storage_size = 16U;
    CHECK(minic_core_module_verify(&module), 131); /* complete array */
    object->storage_size = 0U;
    object->fragments = NULL;
    object->fragment_count = 0U;
    CHECK(!minic_core_module_verify(&module), 132); /* complete int[4] != zero */

    object->core_type_id = zero_array_id;
    object->storage_alignment = 1U;
    CHECK(minic_core_module_verify(&module), 133); /* complete GNU T[0] */
    object->core_type_id = empty_id;
    CHECK(minic_core_module_verify(&module), 134); /* complete empty record */

    object->core_type_id = flexible_id;
    object->storage_alignment = 8U;
    object->fragments = &zero;
    object->fragment_count = 1U;
    zero.byte_count = object->storage_size = 20U;
    CHECK(minic_core_module_verify(&module), 135); /* 8 + 3 int tail */
    qualified.kind = MINIC_CORE_TYPE_QUALIFIED;
    qualified.has_layout = true;
    qualified.size = 8U;
    qualified.alignment = 8U;
    qualified.is_read_only = true;
    qualified.value.qualified.base = flexible_id;
    CHECK(minic_core_type_register(types, &qualified, &qualified_record_id), 154);
    object->core_type_id = qualified_record_id;
    CHECK(minic_core_module_verify(&module), 155); /* const qualified record */
    qualified.has_layout = false;
    qualified.size = 0U;
    qualified.alignment = 0U;
    qualified.value.qualified.base = incomplete_id;
    CHECK(minic_core_type_register(types, &qualified, &qualified_tail_id), 156);
    members[1].type_id = qualified_tail_id;
    CHECK(minic_core_type_register(types, &flexible, &qualified_flexible_id), 157);
    object->core_type_id = qualified_flexible_id;
    CHECK(minic_core_module_verify(&module), 158); /* qualified FAM field */
    object->core_type_id = flexible_id;
    zero.byte_count = object->storage_size = 18U;
    CHECK(!minic_core_module_verify(&module), 136); /* partial tail element */
    zero.byte_count = object->storage_size = 4U;
    CHECK(!minic_core_module_verify(&module), 137); /* less than record */

    object->core_type_id = word_id;
    object->storage_alignment = 4U;
    object->storage_size = 4U;
    CHECK(minic_core_module_verify(&module), 138); /* reset zero scalar */
    object->is_tentative = true;
    CHECK(minic_core_module_verify(&module), 139);
    object->is_zero_initialized = true;
    CHECK(!minic_core_module_verify(&module), 140); /* mutual exclusion */
    object->is_tentative = false;
    CHECK(minic_core_module_verify(&module), 141);

    object->fragments = &bits;
    CHECK(!minic_core_module_verify(&module), 142); /* zero-tag numeric 42 */
    object->is_zero_initialized = false;
    CHECK(!minic_core_module_verify(&module), 143); /* BSS numeric 42 */
    object->default_section = MINIC_CORE_MODULE_SECTION_DATA;
    CHECK(minic_core_module_verify(&module), 144); /* ordinary initialized */
    object->section_name = ".bss.explicit_progbits";
    CHECK(minic_core_module_verify(&module), 145); /* target owns @progbits */
    object->section_name = NULL;

    object->fragments = &reloc;
    object->references = &reference;
    object->reference_count = 1U;
    object->is_zero_initialized = true;
    CHECK(minic_core_module_verify(&module), 146); /* pointer = &symbol */
    object->is_zero_initialized = false;
    object->default_section = MINIC_CORE_MODULE_SECTION_BSS;
    CHECK(!minic_core_module_verify(&module), 147); /* BSS relocation */
    object->default_section = MINIC_CORE_MODULE_SECTION_DATA;
    object->is_tentative = true;
    CHECK(!minic_core_module_verify(&module), 148); /* tentative relocation */
    object->is_tentative = false;
    object->is_zero_initialized = true;
    object->section_name = ".bss.explicit_relocation";
    CHECK(minic_core_module_verify(&module), 149); /* target-owned section */
    object->section_name = NULL;
    object->is_zero_initialized = false;
    object->references = NULL;
    object->reference_count = 0U;
    object->fragments = &zero;
    object->default_section = MINIC_CORE_MODULE_SECTION_BSS;
    CHECK(minic_core_module_verify(&module), 150);

    /* Unmaterialized declarations and discarded symbols may preserve an
     * incomplete type, an unspecified extent, or no Core type at all. */
    object->is_extern = true;
    object->core_type_id = incomplete_id;
    object->storage_size = 0U;
    object->storage_alignment = 0U;
    object->fragments = NULL;
    object->fragment_count = 0U;
    CHECK(minic_core_module_verify(&module), 151);
    object->is_extern = false;
    object->emittable = false;
    object->core_type_id = MINIC_CORE_TYPE_INVALID;
    CHECK(minic_core_module_verify(&module), 152);
    return 0;
}

int main(void) {
    MinicCoreModuleFragment parts[3] = {
        {.kind = MINIC_CORE_MODULE_FRAGMENT_ZERO,
         .byte_offset = 0U, .byte_count = 3U},
        {.kind = MINIC_CORE_MODULE_FRAGMENT_BITS,
         .byte_offset = 3U, .byte_count = 2U, .bits = 0x1234U},
        {.kind = MINIC_CORE_MODULE_FRAGMENT_RELOCATION,
         .byte_offset = 5U, .byte_count = 8U,
         .relocation_kind = MINIC_CORE_MODULE_RELOCATION_FUNCTION,
         .relocation_symbol = "callee"}
    };
    MinicCoreModuleGlobal object = {
        .name = "valid", .storage_size = 13U,
        .fragments = parts, .fragment_count = 3U
    };
    MinicCoreTypeContext types;
    MinicCoreModuleFunctionSymbol symbols[2] = {
        {.name = "callee", .is_defined = true, .alias_target_index = SIZE_MAX,
         .specialization_source_index = SIZE_MAX},
        {.name = "alias", .alias_target_index = 0U,
         .specialization_source_index = SIZE_MAX}
    };
    MinicCoreModule module = {
        .type_context = &types,
        .function_symbols = symbols, .function_count = 2U,
        .finalized = true
    };

    minic_core_type_context_initialize(&types);
    CHECK(minic_core_module_global_fragments_valid(&object), 1);
    CHECK(!minic_core_module_global_fragments_valid(NULL), 2);

    object.storage_size = 12U;
    CHECK(!minic_core_module_global_fragments_valid(&object), 3);
    object.storage_size = 13U;
    parts[2].byte_offset = SIZE_MAX;
    CHECK(!minic_core_module_global_fragments_valid(&object), 4);
    parts[2].byte_offset = 5U;
    parts[2].byte_count = SIZE_MAX;
    CHECK(!minic_core_module_global_fragments_valid(&object), 5);
    parts[2].byte_count = 8U;
    parts[2].relocation_symbol = "callee\n.injected";
    CHECK(!minic_core_module_global_fragments_valid(&object), 6);
    parts[2].relocation_symbol = "callee";

    parts[2].relocation_kind = MINIC_CORE_MODULE_RELOCATION_LABEL;
    parts[2].relocation_symbol = ".Luser_007";
    CHECK(!minic_core_module_global_fragments_valid(&object), 7);
    parts[2].relocation_symbol = ".Luser_7";
    parts[2].relocation_addend = 1;
    CHECK(!minic_core_module_global_fragments_valid(&object), 8);
    parts[2].relocation_addend = 0;
    CHECK(minic_core_module_global_fragments_valid(&object), 9);

    parts[2].kind = MINIC_CORE_MODULE_FRAGMENT_ZERO;
    CHECK(!minic_core_module_global_fragments_valid(&object), 10);
    parts[2].relocation_symbol = NULL;
    CHECK(minic_core_module_global_fragments_valid(&object), 11);
    parts[2].kind = MINIC_CORE_MODULE_FRAGMENT_RELOCATION;
    parts[2].relocation_kind = MINIC_CORE_MODULE_RELOCATION_OBJECT;
    parts[2].relocation_symbol = "obj.name$1";
    CHECK(minic_core_module_global_fragments_valid(&object), 12);

    parts[1].byte_count = 3U;
    CHECK(!minic_core_module_global_fragments_valid(&object), 13);
    parts[1].byte_count = 2U;
    parts[1].relocation_symbol = "not_permitted";
    CHECK(!minic_core_module_global_fragments_valid(&object), 14);
    parts[1].relocation_symbol = NULL;

    object.fragments = NULL;
    CHECK(!minic_core_module_global_fragments_valid(&object), 15);
    object.fragments = parts;
    object.fragment_count = 0U;
    CHECK(!minic_core_module_global_fragments_valid(&object), 16);
    object.storage_size = 0U;
    CHECK(minic_core_module_global_fragments_valid(&object), 17);

    CHECK(minic_core_module_verify(&module), 18);
    CHECK(verify_module_provenance_edges(&module) == 0, 32);
    CHECK(verify_module_alias_index_ownership(&module) == 0, 54);
    CHECK(verify_module_function_symbol_range(&module) == 0, 69);
    CHECK(verify_module_emitted_symbol_namespace(&module, &types) == 0, 89);
    CHECK(verify_materialized_relocation_edges(&module, &types) == 0, 100);
    CHECK(verify_source_free_zero_definition_facts(&module, &types) == 0, 113);
    {
        const int definition_result =
            verify_source_free_global_definition_contract(&module, &types);
        if (definition_result != 0)
            (void)fprintf(stderr, "GLOBAL_DEFINITION_CONTRACT_FAILED=%d\n",
                          definition_result);
        CHECK(definition_result == 0, 153);
    }
    symbols[0].name = NULL;
    CHECK(!minic_core_module_verify(&module), 19);
    minic_core_type_context_destroy(&types);
    puts("CORE_MODULE_FRAGMENT_CONTRACT=PASS bounds forms names alias-index=14 clone_provenance=5 forged_owner_label=5 name_range=9 emitted_elf_namespace=18 canonical_source=valid relocation_edge_identity=8 source_free_zero_facts=3 typed_extent=13 definition_flags=14");
    return 0;
}
