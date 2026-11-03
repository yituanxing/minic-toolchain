#include "target/common/module_global_emit.h"
#include "target/common/elf_section_name.h"
#include "core/symbol_identity.h"

#include <inttypes.h>
#include <stdint.h>
#include <string.h>

static bool minic_module_global_symbol_safe(const char *symbol) {
    const unsigned char *cursor = (const unsigned char *)symbol;
    if (cursor == NULL || *cursor == '\0') return false;
    for (; *cursor != '\0'; ++cursor) {
        const unsigned char c = *cursor;
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '$'))
            return false;
    }
    return true;
}

static bool minic_module_global_fragment_shape_valid(
    const MinicCoreModuleGlobal *global,
    MinicModuleDataDirectiveFn directive, size_t relocation_width) {
    size_t index, cursor = 0U;
    if (global == NULL || directive == NULL ||
        (global->fragment_count != 0U && global->fragments == NULL) ||
        (global->fragment_count == 0U && global->storage_size != 0U))
        return false;
    for (index = 0U; index < global->fragment_count; ++index) {
        const MinicCoreModuleFragment *fragment = &global->fragments[index];
        const size_t n = fragment->byte_count;
        if (fragment->byte_offset != cursor || n > global->storage_size - cursor)
            return false;
        switch (fragment->kind) {
        case MINIC_CORE_MODULE_FRAGMENT_ZERO:
            if (n == 0U) return false;
            break;
        case MINIC_CORE_MODULE_FRAGMENT_BITS:
            if (n == 0U || directive(n) == NULL ||
                (n != 1U && n != 2U && n != 4U && n != 8U)) return false;
            break;
        case MINIC_CORE_MODULE_FRAGMENT_RELOCATION:
            if (n == 0U || directive(n) == NULL ||
                (relocation_width != 0U && n != relocation_width) ||
                (n != 4U && n != 8U)) return false;
            if (fragment->relocation_kind == MINIC_CORE_MODULE_RELOCATION_LABEL) {
                if (!minic_core_label_symbol_valid(
                        fragment->relocation_symbol) ||
                    fragment->relocation_addend != 0) return false;
            } else if ((fragment->relocation_kind != MINIC_CORE_MODULE_RELOCATION_OBJECT &&
                        fragment->relocation_kind != MINIC_CORE_MODULE_RELOCATION_FUNCTION) ||
                       !minic_module_global_symbol_safe(fragment->relocation_symbol)) {
                return false;
            }
            break;
        default:
            return false;
        }
        cursor += n;
    }
    return cursor == global->storage_size;
}

bool minic_multi_module_emit_global_fragments(
    FILE *out, const MinicCoreModuleGlobal *global,
    MinicModuleDataDirectiveFn directive, bool signed_integer_bits,
    size_t required_relocation_width) {
    size_t index;
    if (out == NULL || !minic_module_global_fragment_shape_valid(
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
            if (signed_integer_bits && fragment->bits_signed_integer && width != 1U) {
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

bool minic_multi_module_emit_elf_section(
    FILE *out, const MinicCoreModuleGlobal *global, const char *fallback) {
    if (out == NULL || global == NULL || fallback == NULL) return false;
    if (global->section_name == NULL)
        return fprintf(out, "%s\n", fallback) >= 0;
    if (!minic_elf_section_name_valid(global->section_name)) return false;
    return fprintf(out, ".section %s,\"%s\",@%s\n",
        global->section_name, global->is_read_only ? "a" : "aw",
        minic_module_elf_bss_section(global->section_name)
            ? "nobits" : "progbits") >= 0;
}

bool minic_multi_module_emit_elf_binding(
    FILE *out, const MinicCoreModuleGlobal *global) {
    if (out == NULL || global == NULL ||
        !minic_module_global_symbol_safe(global->name)) return false;
    if (global->is_internal) return !global->is_weak;
    return fprintf(out, "%s %s\n", global->is_weak ? ".weak" : ".globl",
                   global->name) >= 0;
}

bool minic_multi_module_emit_elf_symbol_visibility(
    FILE *out, MinicCoreModuleVisibility visibility, const char *symbol) {
    const char *directive;
    if (out == NULL || !minic_module_global_symbol_safe(symbol)) return false;
    switch (visibility) {
    case MINIC_CORE_MODULE_VISIBILITY_DEFAULT: return true;
    case MINIC_CORE_MODULE_VISIBILITY_HIDDEN: directive = ".hidden"; break;
    case MINIC_CORE_MODULE_VISIBILITY_INTERNAL: directive = ".internal"; break;
    case MINIC_CORE_MODULE_VISIBILITY_PROTECTED: directive = ".protected"; break;
    default: return false;
    }
    return fprintf(out, "%s %s\n", directive, symbol) >= 0;
}
