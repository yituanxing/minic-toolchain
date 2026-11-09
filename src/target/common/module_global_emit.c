#include "target/common/module_global_emit.h"
#include "target/common/elf_section_name.h"
#include "core/symbol_identity.h"

#include <inttypes.h>
#include <stdint.h>
#include <string.h>

const MinicCoreType *minic_multi_module_global_core_base(
    const MinicCoreTypeContext *types, MinicCoreTypeId id) {
    return minic_core_type_get(types, minic_core_type_unqualified(types, id));
}

bool minic_multi_module_global_scalar_width(
    const MinicCoreTypeContext *types, const MinicCoreType *type,
    size_t *width) {
    if (types == NULL || width == NULL) return false;
    while (type != NULL && type->kind == MINIC_CORE_TYPE_ARRAY)
        type = minic_multi_module_global_core_base(
            types, type->value.array.element);
    if (type == NULL || !type->has_layout ||
        (type->kind != MINIC_CORE_TYPE_INTEGER &&
         type->kind != MINIC_CORE_TYPE_POINTER &&
         !(type->kind == MINIC_CORE_TYPE_FLOAT &&
           (type->value.floating.format == MINIC_CORE_FLOAT_BINARY32 ||
            type->value.floating.format == MINIC_CORE_FLOAT_BINARY64))) ||
        (type->size != 1U && type->size != 2U &&
         type->size != 4U && type->size != 8U)) return false;
    *width = type->size;
    return true;
}

/* Core owns the target-neutral initializer layout and symbol proof. The
 * object writer only checks what this GNU data-directive dialect can express:
 * scalar directive spelling and this target's relocation field width. */
static bool minic_module_global_fragment_directives_supported(
    const MinicCoreModuleGlobal *global,
    MinicModuleDataDirectiveFn directive, size_t relocation_width) {
    size_t index;
    if (directive == NULL ||
        !minic_core_module_global_fragments_valid(global))
        return false;
    for (index = 0U; index < global->fragment_count; ++index) {
        const MinicCoreModuleFragment *fragment = &global->fragments[index];
        const size_t n = fragment->byte_count;
        switch (fragment->kind) {
        case MINIC_CORE_MODULE_FRAGMENT_ZERO:
            break;
        case MINIC_CORE_MODULE_FRAGMENT_BITS:
            if (directive(n) == NULL) return false;
            break;
        case MINIC_CORE_MODULE_FRAGMENT_RELOCATION:
            if (directive(n) == NULL ||
                (relocation_width != 0U && n != relocation_width) ||
                (n != 4U && n != 8U)) return false;
            break;
        default:
            /* Core fragment verification already rejects unknown kinds. */
            return false;
        }
    }
    return true;
}

bool minic_multi_module_emit_global_fragments(
    FILE *out, const MinicCoreModuleGlobal *global,
    MinicModuleDataDirectiveFn directive, bool signed_integer_bits,
    size_t required_relocation_width) {
    size_t index;
    if (out == NULL || !minic_module_global_fragment_directives_supported(
            global, directive, required_relocation_width)) return false;
    for (index = 0U; index < global->fragment_count; ++index) {
        const MinicCoreModuleFragment *fragment = &global->fragments[index];
        const size_t width = fragment->byte_count;
        const char *data = directive(width);
        uint64_t bits = fragment->bits;
        switch (fragment->kind) {
        case MINIC_CORE_MODULE_FRAGMENT_ZERO:
            if (fprintf(out, "  .zero %zu\n", width) < 0) return false;
            break;
        case MINIC_CORE_MODULE_FRAGMENT_BITS:
            if (width < 8U) bits &= (UINT64_C(1) << (8U * width)) - UINT64_C(1);
            /* The target's GNU dialect may require signed decimal spelling.
             * Two's-complement sign extension depends only on the emitted
             * width and raw bits, not on any C initializer/source type. */
            if (signed_integer_bits && width != 1U) {
                int64_t signed_bits;
                if (width < 8U && (bits & (UINT64_C(1) << (8U * width - 1U))) != 0U)
                    bits |= ~((UINT64_C(1) << (8U * width)) - UINT64_C(1));
                (void)memcpy(&signed_bits, &bits, sizeof(signed_bits));
                if (fprintf(out, "  %s %" PRId64 "\n", data, signed_bits) < 0)
                    return false;
            } else if (fprintf(out, "  %s %" PRIu64 "\n", data, bits) < 0) {
                return false;
            }
            break;
        case MINIC_CORE_MODULE_FRAGMENT_RELOCATION:
            if (fragment->relocation_addend == 0) {
                if (fprintf(out, "  %s %s\n", data,
                            fragment->relocation_symbol) < 0) return false;
            } else if (fprintf(out, "  %s %s%+" PRId64 "\n", data,
                               fragment->relocation_symbol,
                               fragment->relocation_addend) < 0) {
                return false;
            }
            break;
        default:
            return false;
        }
    }
    return true;
}

static bool minic_module_elf_bss_section(const char *name) {
    return name != NULL && strncmp(name, ".bss", 4U) == 0 &&
           (name[4] == '\0' || name[4] == '.');
}

/* ELF SHT_NOBITS cannot encode literal nonzero data or a relocation. This
 * checks the *serialized* Core byte fragments, not an ingress C initializer
 * count: a value narrowed to zero still has a valid NOBITS representation.
 * The RISC-V writers deliberately opt out for explicit .bss.*: their
 * historical target spelling uses @progbits for that section name. */
static bool minic_module_elf_nobits_payload_valid(
    const MinicCoreModuleGlobal *global) {
    size_t index;
    for (index = 0U; index < global->fragment_count; ++index) {
        const MinicCoreModuleFragment *fragment = &global->fragments[index];
        if (fragment->kind == MINIC_CORE_MODULE_FRAGMENT_RELOCATION)
            return false;
        if (fragment->kind == MINIC_CORE_MODULE_FRAGMENT_BITS) {
            const uint64_t visible_bits = fragment->byte_count == 8U
                ? fragment->bits
                : fragment->bits & ((UINT64_C(1) <<
                                      (8U * fragment->byte_count)) - UINT64_C(1));
            if (visible_bits != 0U) return false;
        }
    }
    return true;
}

static const char *minic_module_elf_default_section(
    MinicCoreModuleDefaultSection section) {
    switch (section) {
    case MINIC_CORE_MODULE_SECTION_BSS: return ".bss";
    case MINIC_CORE_MODULE_SECTION_DATA: return ".data";
    case MINIC_CORE_MODULE_SECTION_RODATA: return ".section .rodata";
    case MINIC_CORE_MODULE_SECTION_RELRO: return ".section .data.rel.ro";
    default: return NULL;
    }
}

static bool minic_module_elf_visibility_directive(
    MinicCoreModuleVisibility visibility, const char **directive) {
    if (directive == NULL) return false;
    switch (visibility) {
    case MINIC_CORE_MODULE_VISIBILITY_DEFAULT: *directive = NULL; return true;
    case MINIC_CORE_MODULE_VISIBILITY_HIDDEN: *directive = ".hidden"; return true;
    case MINIC_CORE_MODULE_VISIBILITY_INTERNAL: *directive = ".internal"; return true;
    case MINIC_CORE_MODULE_VISIBILITY_PROTECTED: *directive = ".protected"; return true;
    default: return false;
    }
}

bool minic_multi_module_elf_global_plan_build(
    const MinicCoreModuleGlobal *global, const char *fallback_override,
    bool explicit_bss_nobits, MinicModuleDataDirectiveFn directive,
    bool signed_integer_bits, size_t relocation_width,
    MinicModuleElfGlobalPlan *plan) {
    const char *fallback;
    const char *visibility;
    if (global == NULL || plan == NULL ||
        !minic_core_assembler_symbol_lexically_safe(global->name) ||
        (global->is_internal && global->is_weak) ||
        (global->section_name != NULL &&
         !minic_elf_section_name_valid(global->section_name)) ||
        !minic_module_global_fragment_directives_supported(
            global, directive, relocation_width)) return false;
    if (explicit_bss_nobits &&
        minic_module_elf_bss_section(global->section_name) &&
        !minic_module_elf_nobits_payload_valid(global)) return false;
    fallback = fallback_override != NULL
        ? fallback_override
        : minic_module_elf_default_section(global->default_section);
    if (fallback == NULL) return false;
    if (!minic_module_elf_visibility_directive(global->visibility,
                                                &visibility)) return false;
    plan->global = global;
    plan->fallback_section = fallback;
    plan->explicit_section_type = explicit_bss_nobits &&
        minic_module_elf_bss_section(global->section_name)
        ? "nobits" : "progbits";
    plan->section_flags = global->is_read_only ? "a" : "aw";
    plan->binding_directive = global->is_internal ? NULL :
        (global->is_weak ? ".weak" : ".globl");
    plan->visibility_directive = visibility;
    plan->data_directive = directive;
    plan->relocation_width = relocation_width;
    plan->signed_integer_bits = signed_integer_bits;
    return true;
}

bool minic_multi_module_elf_global_emit_section(
    FILE *out, const MinicModuleElfGlobalPlan *plan) {
    if (out == NULL || plan == NULL || plan->global == NULL ||
        plan->fallback_section == NULL || plan->section_flags == NULL ||
        plan->explicit_section_type == NULL) return false;
    if (plan->global->section_name == NULL)
        return fprintf(out, "%s\n", plan->fallback_section) >= 0;
    return fprintf(out, ".section %s,\"%s\",@%s\n",
                   plan->global->section_name, plan->section_flags,
                   plan->explicit_section_type) >= 0;
}

bool minic_multi_module_elf_global_emit_binding(
    FILE *out, const MinicModuleElfGlobalPlan *plan) {
    if (out == NULL || plan == NULL || plan->global == NULL) return false;
    return plan->binding_directive == NULL ||
        fprintf(out, "%s %s\n", plan->binding_directive,
                plan->global->name) >= 0;
}

bool minic_multi_module_elf_global_emit_visibility(
    FILE *out, const MinicModuleElfGlobalPlan *plan) {
    if (out == NULL || plan == NULL || plan->global == NULL) return false;
    return plan->visibility_directive == NULL ||
        fprintf(out, "%s %s\n", plan->visibility_directive,
                plan->global->name) >= 0;
}

bool minic_multi_module_elf_global_emit_fragments(
    FILE *out, const MinicModuleElfGlobalPlan *plan) {
    return plan != NULL && plan->global != NULL &&
        minic_multi_module_emit_global_fragments(
            out, plan->global, plan->data_directive,
            plan->signed_integer_bits, plan->relocation_width);
}

bool minic_multi_module_emit_elf_symbol_visibility(
    FILE *out, MinicCoreModuleVisibility visibility, const char *symbol) {
    const char *directive;
    if (out == NULL || !minic_core_assembler_symbol_lexically_safe(symbol) ||
        !minic_module_elf_visibility_directive(visibility, &directive))
        return false;
    if (directive == NULL) return true;
    return fprintf(out, "%s %s\n", directive, symbol) >= 0;
}
