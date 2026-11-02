#include "target/common/global_emit.h"
#include "target/common/elf_section_name.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Assembly-only adapters over the Compiler's single checked C/DataLayout
 * payload walker.  No source aggregate or initializer semantics live here. */
static const char *minic_multi_data_directive(size_t width) {
    return width == 1U ? ".byte" : width == 2U ? ".2byte" :
           width == 4U ? ".4byte" : width == 8U ? ".8byte" : NULL;
}
static bool minic_multi_emit_data_bits(FILE *out, size_t width, uint64_t bits) {
    const char *directive = minic_multi_data_directive(width);
    if (directive == NULL) return false;
    if (width < 8U) bits &= (UINT64_C(1) << (width * 8U)) - UINT64_C(1);
    return fprintf(out, "  %s %" PRIu64 "\n", directive, bits) >= 0;
}

/* One semantic payload traversal has two consumers: a source-side ModuleIR
 * fragment builder and the existing independent FILE-based regression API.
 * The latter stays a thin adapter, not a second layout implementation. */
typedef struct MinicMultiFilePayloadAdapter {
    FILE *out;
    const MinicC0Program *program;
    const MinicTargetInfo *target;
    MinicMultiRelocEmitter relocation;
    MinicMultiScalarBitsEmitter bits;
    bool legacy_common_bits;
} MinicMultiFilePayloadAdapter;


static bool minic_file_payload_zero(void *context, size_t bytes) {
    MinicMultiFilePayloadAdapter *adapter = context;
    return fprintf(adapter->out, "  .zero %zu\n", bytes) >= 0;
}

static bool minic_file_payload_bits(void *context, MinicType type,
                                    size_t width, uint64_t bits) {
    MinicMultiFilePayloadAdapter *adapter = context;
    return adapter->bits != NULL && !adapter->legacy_common_bits
        ? adapter->bits(adapter->out, adapter->program, adapter->target,
                        type, bits)
        : minic_multi_emit_data_bits(adapter->out, width, bits);
}

static bool minic_file_payload_relocation(
    void *context, const MinicGlobalRelocation *relocation, size_t width) {
    MinicMultiFilePayloadAdapter *adapter = context;
    return adapter->relocation(adapter->out, adapter->program, adapter->target,
                               relocation, width);
}

static MinicMultiGlobalPayloadSink minic_file_payload_sink(
    MinicMultiFilePayloadAdapter *adapter) {
    const MinicMultiGlobalPayloadSink sink = {
        adapter, minic_file_payload_zero, minic_file_payload_bits,
        minic_file_payload_relocation
    };
    return sink;
}

static MinicMultiFilePayloadAdapter minic_file_payload_adapter(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    MinicMultiRelocEmitter relocation, MinicMultiScalarBitsEmitter bits) {
    const MinicMultiFilePayloadAdapter adapter = {
        out, program, target, relocation, bits, false
    };
    return adapter;
}

/* Historical specialized FILE entrypoints select only a payload shape.
 * All byte layout, padding, union and relocation-slot traversal stays in
 * compiler/global_payload.c.  This adapter deliberately retains each
 * caller's original scalar spelling callback policy. */
static bool minic_multi_emit_payload_kind(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicGlobalObject *object, size_t storage_size,
    MinicMultiGlobalPayloadKind kind,
    MinicMultiRelocEmitter emit_relocation,
    MinicMultiScalarBitsEmitter emit_bits) {
    MinicMultiGlobalEmissionPlan plan = {0};
    MinicMultiFilePayloadAdapter adapter = minic_file_payload_adapter(
        out, program, target, emit_relocation, emit_bits);
    const MinicMultiGlobalPayloadSink sink = minic_file_payload_sink(&adapter);
    plan.payload_kind = kind;
    plan.storage_size = storage_size;
    return out != NULL && emit_relocation != NULL &&
           minic_multi_global_payload_visit(
               program, target, object, &plan, &sink);
}


bool minic_multi_emit_relocatable_scalar_array(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicGlobalObject *obj, size_t storage_size,
    MinicMultiRelocEmitter emit_relocation,
    MinicMultiScalarBitsEmitter emit_bits) {
    return minic_multi_emit_payload_kind(
        out, program, target, obj, storage_size,
        MINIC_MULTI_GLOBAL_PAYLOAD_ARRAY_SCALAR_RELOCATIONS,
        emit_relocation, emit_bits);
}

/* zlib's level configuration table is an array of flat records containing
 * four shorts and one function pointer. Core already flattens its initializer
 * values and relocation slots in logical field order. Target layout owns
 * record padding and the ELF relocation expressions. This deliberately does
 * NOT claim support for arbitrary nested aggregates, unions or bit-fields. */

bool minic_multi_emit_zero_relocation_global(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicGlobalObject *obj, size_t storage_size,
    MinicMultiRelocEmitter emit_relocation) {
    return minic_multi_emit_payload_kind(
        out, program, target, obj, storage_size,
        MINIC_MULTI_GLOBAL_PAYLOAD_ZERO_RELOCATIONS,
        emit_relocation, NULL);
}


/* Lua nested static records and selected unions reuse x86's recursive
 * DataLayout walk, with target-specific ELF relocation callbacks. Keep
 * the established slot, union selection, padding and depth guards. */
/* Bitfield packing is a DataLayout operation, not an ISA-codegen task.
 * The RV64 legacy walker and the shared recursive walker invoke this same
 * checked byte-range implementation. */

bool minic_multi_emit_recursive_record_global(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicGlobalObject *object, size_t storage_size,
    MinicMultiRelocEmitter emit_relocation) {
    return object != NULL && minic_type_is_record(object->type) &&
           minic_multi_emit_recursive_aggregate_global(
               out, program, target, object, storage_size,
               emit_relocation, NULL);
}

/* Recursive aggregate shape inspection must be side-effect free. All four
 * backends now share this layout walk, including RV64's GNU flexible/empty
 * record extensions and bitfield byte packing. Unsupported shapes fail
 * before any assembly is emitted, rather than fabricating a zero payload. */

bool minic_multi_emit_recursive_aggregate_global(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicGlobalObject *object, size_t storage_size,
    MinicMultiRelocEmitter emit_relocation,
    MinicMultiScalarBitsEmitter emit_bits) {
    return object != NULL && minic_multi_emit_payload_kind(
        out, program, target, object, storage_size,
        minic_type_is_array(object->type)
            ? MINIC_MULTI_GLOBAL_PAYLOAD_ARRAY_RECURSIVE
            : MINIC_MULTI_GLOBAL_PAYLOAD_RECORD_RECURSIVE,
        emit_relocation, emit_bits);
}

/* One shared recursive layout for arrays of records with nested array,
 * record and union fields. Keep the proven flat-array fast path for zlib
 * and route more complex initializer slot shapes through the x86-derived
 * recursive DataLayout walk (aXformType: 6 records, 60 flattened slots).
 * No backend may guess field offsets or silently truncate initializers. */

bool minic_multi_emit_record_array_global(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicGlobalObject *object, size_t storage_size,
    MinicMultiRelocEmitter emit_relocation) {
    return minic_multi_emit_payload_kind(
        out, program, target, object, storage_size,
        MINIC_MULTI_GLOBAL_PAYLOAD_ARRAY_RECORD,
        emit_relocation, NULL);
}


/* GNU permits a static record object to initialize a trailing flexible
 * array (BusyBox hdparm's masks_labels_t). The parser owns the flattened
 * scalar slots, while GlobalObject DataLayout owns concrete storage size.
 * Flexible record tails are ordinary recursive fields whose concrete
 * extent comes from the complete GlobalObject DataLayout, not sizeof(record).
 * Preserve the public entry point for callers; there is no separate payload
 * traversal/relocation order to maintain. */

bool minic_multi_emit_elf_relocation(FILE *out, const MinicC0Program *program,
                                   const MinicTargetInfo *target,
                                   const MinicGlobalRelocation *reloc,
                                   size_t width) {
    const char *name = NULL;
    int64_t addend;
    if (reloc == NULL || width != 8U ||
        target->data_layout->pointer_size != 8U) return false;
    if (reloc->target_kind == MINIC_GLOBAL_RELOCATION_LABEL) {
        const MinicStatement *label =
            minic_c0_program_statement(program, reloc->target_id);
        return label != NULL && label->kind == MINIC_STATEMENT_LABEL &&
               fprintf(out, "  .8byte .Luser_%zu\n", reloc->target_id) >= 0;
    }
    name = minic_multi_global_relocation_target_name(program, reloc);
    if (name == NULL || name[0] == '\0' ||
        !minic_data_layout_global_relocation_target_addend(
            target->data_layout, program, reloc, &addend)) return false;
    if (addend == 0)
        return fprintf(out, "  .8byte %s\n", name) >= 0;
    return fprintf(out, "  .8byte %s%+" PRId64 "\n", name, addend) >= 0;
}

/* A record with ABI bit-fields (including musl zero-width timespec
 * barriers) cannot use the flat scalar field walk. Select the already
 * verified recursive DataLayout walker across all four architectures. */

bool minic_multi_emit_flat_record_global(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicGlobalObject *obj, size_t storage_size,
    MinicMultiRelocEmitter emit_relocation,
    MinicMultiScalarBitsEmitter emit_bits) {
    return minic_multi_emit_payload_kind(
        out, program, target, obj, storage_size,
        MINIC_MULTI_GLOBAL_PAYLOAD_RECORD_FLAT,
        emit_relocation, emit_bits);
}

const char *minic_multi_elf_visibility_directive(MinicSymbolVisibility visibility) {
    switch (visibility) {
    case MINIC_SYMBOL_VISIBILITY_DEFAULT: return NULL;
    case MINIC_SYMBOL_VISIBILITY_HIDDEN: return ".hidden";
    case MINIC_SYMBOL_VISIBILITY_INTERNAL: return ".internal";
    case MINIC_SYMBOL_VISIBILITY_PROTECTED: return ".protected";
    default: return NULL;
    }
}

bool minic_multi_emit_elf_symbol_visibility(FILE *out, MinicSymbolVisibility visibility,
                                   const char *name) {
    const char *directive = minic_multi_elf_visibility_directive(visibility);
    return visibility == MINIC_SYMBOL_VISIBILITY_DEFAULT ||
           (directive != NULL && fprintf(out, "%s %s\n", directive, name) >= 0);
}

/* GNU original Linux named object sections are ELF metadata, not ISA ABI.
 * Disallow arbitrary GAS directives/flags supplied by C attributes. */
bool minic_multi_elf_safe_section_name(const char *name) {
    return minic_elf_section_name_valid(name);
}
static bool minic_multi_elf_section_is_bss(const char *name) {
    return name != NULL && strncmp(name, ".bss", 4U) == 0 &&
           (name[4] == '\0' || name[4] == '.');
}
bool minic_multi_emit_elf_global_section(FILE *out,
    const MinicGlobalObject *obj, const char *fallback) {
    if (out == NULL || obj == NULL || fallback == NULL) return false;
    if (obj->section_name == NULL)
        return fprintf(out, "%s\n", fallback) >= 0;
    if (!minic_multi_elf_safe_section_name(obj->section_name))
        return false;
    return fprintf(out, ".section %s,\"%s\",@%s\n",
        obj->section_name, obj->is_read_only ? "a" : "aw",
        minic_multi_elf_section_is_bss(obj->section_name)
            ? "nobits" : "progbits") >= 0;
}
bool minic_multi_emit_elf_global_binding(FILE *out,
    const MinicGlobalObject *obj) {
    if (out == NULL || obj == NULL) return false;
    if (obj->is_internal) return !obj->is_weak;
    return fprintf(out, "%s %s\n",
        obj->is_weak ? ".weak" : ".globl", obj->name) >= 0;
}


bool minic_multi_global_emission_plan_build(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicGlobalObject *obj, MinicMultiGlobalEmissionPlan *plan) {
    if (!minic_multi_global_payload_plan_build(program, target, obj, plan))
        return false;
    /* The Common ELF section writer uses NOBITS for .bss/.bss.*. Its
     * stricter admission remains separate from the payload data walk: an
     * RV64 emitter with its existing @progbits spelling cannot silently
     * inherit a different section contract during a structural migration. */
    return minic_multi_global_explicit_bss_admitted(obj);
}


bool minic_multi_emit_global_payload(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicGlobalObject *obj, const MinicMultiGlobalEmissionPlan *plan,
    MinicMultiRelocEmitter emit_relocation,
    MinicMultiScalarBitsEmitter emit_bits) {
    MinicMultiFilePayloadAdapter adapter = minic_file_payload_adapter(
        out, program, target, emit_relocation, emit_bits);
    const MinicMultiGlobalPayloadSink sink = minic_file_payload_sink(&adapter);
    if (plan != NULL &&
        (plan->payload_kind == MINIC_MULTI_GLOBAL_PAYLOAD_RECORD_RECURSIVE ||
         plan->payload_kind == MINIC_MULTI_GLOBAL_PAYLOAD_RECORD_FLEXIBLE ||
         plan->payload_kind == MINIC_MULTI_GLOBAL_PAYLOAD_ARRAY_RECORD))
        adapter.legacy_common_bits = true;
    return out != NULL && emit_relocation != NULL &&
           minic_multi_global_payload_visit(program, target, obj, plan, &sink);
}

bool minic_multi_emit_simple_global(FILE *out, const MinicC0Program *program,
                               const MinicTargetInfo *target,
                               const MinicGlobalObject *obj,
                               MinicMultiRelocEmitter emit_relocation) {
    MinicMultiGlobalEmissionPlan plan;
    if (out == NULL || emit_relocation == NULL ||
        !minic_multi_global_emission_plan_build(program, target, obj, &plan))
        return false;
    /* Complete classification precedes *all* writes, including ELF
     * visibility and section selection. Native relocation details remain
     * opaque callbacks rather than a second data-layout classifier. */
    if (!obj->is_internal &&
        !minic_multi_emit_elf_symbol_visibility(out, obj->visibility, obj->name))
        return false;
    if (!minic_multi_emit_elf_global_section(out, obj, plan.default_elf_section) ||
        !minic_multi_emit_elf_global_binding(out, obj) ||
        fprintf(out, "  .balign %zu\n.type %s,@object\n%s:\n",
                plan.storage_alignment, obj->name, obj->name) < 0 ||
        !minic_multi_emit_global_payload(out, program, target, obj, &plan,
                                         emit_relocation, NULL))
        return false;
    return fprintf(out, ".size %s,%zu\n", obj->name, plan.storage_size) >= 0;
}
