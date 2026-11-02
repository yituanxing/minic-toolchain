#include "target/riscv64/codegen.h"
#include "target/riscv64/global_emit.h"
#include "target/riscv64/isa.h"
#include "target/common/global_emit.h"
#include "target/common/module_global_emit.h"
#include "target/riscv64/abi.h"
#include "target/riscv64/codegen_internal.h"
#include "target/riscv64/core_codegen.h"
#include "target/riscv/function_symbol.h"
#include "target/data_layout.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool minic_riscv64_alignment_power(size_t alignment, unsigned int *power) {
    unsigned int result;
    size_t value;

    if (alignment == 0U || power == NULL) {
        return false;
    }
    result = 0U;
    value = alignment;
    while (value > 1U) {
        if ((value & 1U) != 0U) {
            return false;
        }
        value >>= 1U;
        result += 1U;
    }
    *power = result;
    return true;
}

static const char *minic_riscv64_integer_data_directive(size_t width) {
    return width == 1U   ? ".byte"
           : width == 2U ? ".half"
           : width == 4U ? ".word"
           : width == 8U ? ".dword"
                         : NULL;
}

static bool minic_riscv64_emit_typed_bits(FILE *file,
                                          const MinicC0Program *program,
                                          MinicType type,
                                          uint64_t bits) {
    const char *directive;
    size_t width;
    size_t alignment;
    uint64_t mask;

    if (file == NULL || program == NULL ||
        (!minic_type_is_integer(type) && !minic_type_is_pointer(type) &&
         !minic_type_is_float(type) && !minic_type_is_double(type)) ||
        !minic_data_layout_type(minic_default_data_layout(), program, type, &width, &alignment) ||
        (width != 1U && width != 2U && width != 4U && width != 8U)) {
        return false;
    }
    (void)alignment;
    directive = minic_riscv64_integer_data_directive(width);
    if (directive == NULL) {
        return false;
    }
    if (width < 8U) {
        const unsigned int bit_width = (unsigned int)(width * 8U);

        mask = (UINT64_C(1) << bit_width) - UINT64_C(1);
        bits &= mask;
    }
    /* Preserve the historical byte spelling as an unsigned payload. GNU as
     * consumes the same low 8 bits, and plain char on this target is unsigned. */
    if (width == 1U || minic_type_is_float(type) || minic_type_is_double(type) ||
        (minic_type_is_integer(type) && minic_type_is_unsigned_integer(type))) {
        return fprintf(file, "  %s %" PRIu64 "\n", directive, bits) >= 0;
    }
    {
        int64_t signed_value;

        if (width < 8U) {
            const unsigned int bit_width = (unsigned int)(width * 8U);
            const uint64_t sign_bit = UINT64_C(1) << (bit_width - 1U);

            if ((bits & sign_bit) != 0U) {
                bits |= ~((UINT64_C(1) << bit_width) - UINT64_C(1));
            }
        }
        (void)memcpy(&signed_value, &bits, sizeof(signed_value));
        return fprintf(file, "  %s %" PRId64 "\n", directive, signed_value) >= 0;
    }
}

/* Shared layout emits via RV64's existing typed GNU scalar spelling. */
static bool minic_riscv64_emit_shared_bits(
    FILE *file, const MinicC0Program *program,
    const MinicTargetInfo *target, MinicType type, uint64_t bits) {
    return target != NULL && target->architecture == MINIC_TARGET_ARCH_RISCV64 &&
           minic_riscv64_emit_typed_bits(file, program, type, bits);
}

/* Keep the preexisting RV64 source-admission envelope independent of the
 * shared payload plan. In particular, an uninitialized unsupported scalar
 * leaf must not become accepted merely because its payload is all zero. */
static bool minic_riscv64_global_scalar_type(const MinicC0Program *program,
                                             MinicType object_type,
                                             MinicType *scalar_type,
                                             size_t *scalar_width) {
    MinicType type;
    size_t alignment;

    if (program == NULL || scalar_type == NULL || scalar_width == NULL)
        return false;
    type = object_type;
    while (minic_type_is_array(type)) {
        const MinicArrayType *array_type =
            minic_c0_program_array_type(program, type.array_type_id);
        if (array_type == NULL) return false;
        type = array_type->element_type;
    }
    if ((!minic_type_is_integer(type) && !minic_type_is_pointer(type) &&
         !minic_type_is_float(type) && !minic_type_is_double(type)) ||
        !minic_data_layout_type(minic_default_data_layout(), program, type,
                                scalar_width, &alignment) ||
        (*scalar_width != 1U && *scalar_width != 2U &&
         *scalar_width != 4U && *scalar_width != 8U))
        return false;
    (void)alignment;
    *scalar_type = type;
    return true;
}

static bool minic_riscv64_emit_symbol_value(FILE *file,
                                            const MinicC0Program *program,
                                            const MinicGlobalRelocation *relocation,
                                            size_t width) {
    const char *directive;
    const char *target_name;
    int64_t target_addend;

    directive = minic_riscv64_integer_data_directive(width);
    if (file == NULL || directive == NULL) {
        return false;
    }
    if (relocation->target_kind == MINIC_GLOBAL_RELOCATION_LABEL) {
        const MinicStatement *label;

        label = minic_c0_program_statement(program, relocation->target_id);
        return label != NULL && label->kind == MINIC_STATEMENT_LABEL &&
               fprintf(file, "  %s .Luser_%zu\n", directive, relocation->target_id) >= 0;
    }
    target_name = minic_multi_global_relocation_target_name(program, relocation);
    if (target_name == NULL || target_name[0] == '\0' ||
        !minic_data_layout_global_relocation_target_addend(
            minic_default_data_layout(), program, relocation, &target_addend)) {
        return false;
    }
    if (target_addend == 0U) {
        return fprintf(file, "  %s %s\n", directive, target_name) >= 0;
    }
    if (target_addend > 0) {
        return fprintf(file, "  %s %s+%" PRId64 "\n", directive, target_name, target_addend) >= 0;
    }
    return fprintf(file, "  %s %s%" PRId64 "\n", directive, target_name, target_addend) >= 0;
}

/* Reuse the proven common zero-relocation byte walk.  Keep RV64's .dword
 * spelling and expression syntax behind a narrow target-owned callback. */
static bool minic_riscv64_emit_symbol_relocation(
    FILE *file, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicGlobalRelocation *relocation, size_t width) {
    return target != NULL && target->architecture == MINIC_TARGET_ARCH_RISCV64 &&
           minic_riscv64_emit_symbol_value(file, program, relocation, width);
}

static bool minic_riscv64_array_uses_recursive_initializer(
    const MinicC0Program *program, MinicType type) {
    const MinicArrayType *array_type;

    if (program == NULL || !minic_type_is_array(type)) {
        return false;
    }
    array_type = minic_c0_program_array_type(program, type.array_type_id);
    return array_type != NULL &&
           (minic_type_is_record(array_type->element_type) ||
            minic_type_is_array(array_type->element_type));
}

/* Common decides payload shape and walks bytes. The generic record-array and
 * recursive-record wrappers use common scalar spelling; RV64 has historically
 * used .half/.word/.dword, so pass the native scalar callback directly to
 * that very same recursive walker for these three planned payload kinds. */
static bool minic_riscv64_emit_planned_payload(
    FILE *file, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicGlobalObject *object, const MinicMultiGlobalEmissionPlan *plan) {
    if (plan->payload_kind == MINIC_MULTI_GLOBAL_PAYLOAD_RECORD_RECURSIVE ||
        plan->payload_kind == MINIC_MULTI_GLOBAL_PAYLOAD_RECORD_FLEXIBLE ||
        plan->payload_kind == MINIC_MULTI_GLOBAL_PAYLOAD_ARRAY_RECORD)
        return minic_multi_emit_recursive_aggregate_global(
            file, program, target, object, plan->storage_size,
            minic_riscv64_emit_symbol_relocation,
            minic_riscv64_emit_shared_bits);
    return minic_multi_emit_global_payload(
        file, program, target, object, plan,
        minic_riscv64_emit_symbol_relocation,
        minic_riscv64_emit_shared_bits);
}

bool minic_riscv64_emit_file_asm_bytes(FILE *file, const char *text,
                                       size_t length) {
    if (file == NULL || text == NULL ||
        !minic_riscv64_source_asm_marker_safe(text, length)) {
        return false;
    }
    if (length != 0U && fwrite(text, 1U, length, file) != length) {
        return false;
    }
    return fputc('\n', file) != EOF;
}

bool minic_riscv64_emit_file_asm(FILE *file, const MinicFileAsm *file_asm) {
    return file_asm != NULL &&
        minic_riscv64_emit_file_asm_bytes(
            file, file_asm->text, file_asm->length);
}

static bool minic_riscv64_zero_size_object_definition(const MinicC0Program *program,
                                                      const MinicGlobalObject *object) {
    size_t object_alignment;
    size_t storage_size;

    if (program == NULL || object == NULL ||
        !minic_data_layout_global_object(
            minic_default_data_layout(), program, object, &storage_size, &object_alignment)) {
        return false;
    }
    (void)object_alignment;
    if (storage_size != 0U || object->initializer_count != 0U || object->relocation_count != 0U) {
        return false;
    }
    if (minic_type_is_record(object->type)) {
        const MinicRecord *record;

        record = minic_c0_program_record(program, object->type.record_id);
        return record != NULL && record->is_complete && record->field_count == 0U;
    }
    if (minic_type_is_array(object->type)) {
        const MinicArrayType *array_type;

        array_type = minic_c0_program_array_type(program, object->type.array_type_id);
        return array_type != NULL &&
               (array_type->is_zero_length || array_type->element_count != 0U);
    }
    return false;
}

bool minic_riscv64_emit_global_object(FILE *file,
                                             const MinicC0Program *program,
                                             const MinicGlobalObject *object) {
    const MinicTargetInfo *target =
        minic_target_info_for_architecture(MINIC_TARGET_ARCH_RISCV64);
    MinicMultiGlobalEmissionPlan plan;
    size_t object_alignment;
    size_t storage_size;

    if (target == NULL || !minic_data_layout_global_object(
            target->data_layout, program, object,
            &storage_size, &object_alignment)) {
        return false;
    }

    MinicType scalar_type;
    unsigned int alignment_power;
    size_t scalar_width;
    bool zero_size_object_definition;

    if (file == NULL || program == NULL || object == NULL || object->name_length == 0U ||
        object_alignment == 0U ||
        !minic_riscv64_alignment_power(object_alignment, &alignment_power)) {
        return false;
    }
    zero_size_object_definition = minic_riscv64_zero_size_object_definition(program, object);
    if (storage_size == 0U && !zero_size_object_definition) {
        return false;
    }

    scalar_width = 0U;
    if (zero_size_object_definition) {
        if (object->initializer_count != 0U || object->relocation_count != 0U) {
            return false;
        }
    } else if (object->is_zero_initialized || object->is_tentative) {
        if (object->initializer_count != 0U) {
            return false;
        }
    } else if (minic_type_is_record(object->type)) {
        const MinicRecord *record;

        record = minic_c0_program_record(program, object->type.record_id);
        if (record == NULL || !record->is_complete ||
            (object->initializer_count == 0U && !zero_size_object_definition)) {
            return false;
        }
    } else if (minic_riscv64_array_uses_recursive_initializer(program, object->type) ||
               (minic_type_is_array(object->type) && object->relocation_count != 0U)) {
        if (object->initializer_count == 0U) {
            return false;
        }
    } else {
        if (object->relocation_count != 0U ||
            !minic_riscv64_global_scalar_type(program, object->type, &scalar_type, &scalar_width) ||
            scalar_width == 0U || object->initializer_count > storage_size / scalar_width) {
            return false;
        }
        if (minic_riscv64_integer_data_directive(scalar_width) == NULL) {
            return false;
        }
    }

    /* RV64 deliberately emits explicit source sections as @progbits,
     * including .bss.* names, rather than inheriting Common's NOBITS source
     * policy. The payload-only API leaves that independent ELF policy alone. */
    if (!minic_multi_global_payload_plan_build(
            program, target, object, &plan) ||
        plan.storage_size != storage_size ||
        plan.storage_alignment != object_alignment)
        return false;

    if (object->section_name != NULL) {
        if (!minic_multi_elf_safe_section_name(object->section_name))
            return false;
        if (fprintf(file,
                    ".section %s,\"%s\",@progbits\n",
                    object->section_name,
                    object->is_read_only ? "a" : "aw") < 0) {
            return false;
        }
    } else {
        const char *storage_section;

        /* A const object containing link-time addresses is only read-only after
           dynamic relocation.  Keep pure constants in .rodata, but place
           relocation-bearing const data in the conventional writable relocation
           section so a PIE loader can apply R_RISCV_RELATIVE/R_RISCV_64 without
           writing into the executable read-only LOAD segment. */
        storage_section =
            object->is_read_only
                ? (object->relocation_count != 0U ? ".section .data.rel.ro"
                                                  : ".section .rodata")
                : ".data";
        if (fprintf(file, "%s\n", storage_section) < 0) {
            return false;
        }
    }
    if (!object->is_internal) {
        if (fprintf(file, object->is_weak ? ".weak %s\n" : ".globl %s\n", object->name) < 0) {
            return false;
        }
        if (!minic_multi_emit_elf_symbol_visibility(
                file, object->visibility, object->name))
            return false;
    }
    if (fprintf(file,
                ".type %s, @object\n"
                ".align %u\n"
                "%s:\n",
                object->name,
                alignment_power,
                object->name) < 0) {
        return false;
    }
    if (zero_size_object_definition) {
        /* GNU zero-length objects still own a symbol and type, but have no
           payload bytes. The label plus .size 0 is the complete definition. */
    } else if (!minic_riscv64_emit_planned_payload(
                   file, program, target, object, &plan))
        return false;
    return fprintf(file, ".size %s, %zu\n", object->name, storage_size) >= 0;
}

/* The legacy C global serializer above remains for standalone source-policy
 * fixtures. A production translation unit hands us a sealed ModuleIR object:
 * byte spans, final ABI layout, symbols and checked relocation addends are
 * already owned by Core. RV64 still independently enforces its historical
 * narrower admission envelope before selecting GNU data directives. */
static const MinicCoreType *rv64_module_core_base(
    const MinicCoreTypeContext *types, MinicCoreTypeId id) {
    return minic_core_type_get(types, minic_core_type_unqualified(types, id));
}

static bool rv64_module_symbol_safe(const char *name) {
    const unsigned char *p = (const unsigned char *)name;
    if (p == NULL || *p == '\0') return false;
    for (; *p != '\0'; ++p)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') || *p == '_' || *p == '.' || *p == '$'))
            return false;
    return true;
}

static bool rv64_module_array_recursive(
    const MinicCoreTypeContext *types, const MinicCoreType *type) {
    const MinicCoreType *element;
    if (type == NULL || type->kind != MINIC_CORE_TYPE_ARRAY) return false;
    element = rv64_module_core_base(types, type->value.array.element);
    return element != NULL && (element->kind == MINIC_CORE_TYPE_RECORD ||
                               element->kind == MINIC_CORE_TYPE_ARRAY);
}

static bool rv64_module_scalar_width(
    const MinicCoreTypeContext *types, const MinicCoreType *type,
    size_t *width) {
    while (type != NULL && type->kind == MINIC_CORE_TYPE_ARRAY)
        type = rv64_module_core_base(types, type->value.array.element);
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

static bool rv64_module_zero_extent_legal(const MinicCoreType *type) {
    return type != NULL &&
        ((type->kind == MINIC_CORE_TYPE_RECORD &&
          type->value.record.is_complete && type->value.record.field_count == 0U) ||
         (type->kind == MINIC_CORE_TYPE_ARRAY &&
          (type->value.array.is_zero_length || type->value.array.count != 0U)));
}

static bool rv64_module_admit_global(const MinicCoreModule *module,
                                    const MinicCoreModuleGlobal *global) {
    const MinicCoreType *type;
    size_t scalar_width;
    bool zero_extent;
    if (module == NULL || module->type_context == NULL || global == NULL ||
        !rv64_module_symbol_safe(global->name) || global->is_extern ||
        global->is_alias || global->is_thread_local ||
        (global->is_internal && global->is_weak) ||
        global->storage_alignment == 0U ||
        (type = rv64_module_core_base(module->type_context,
                                      global->core_type_id)) == NULL)
        return false;
    zero_extent = global->storage_size == 0U && rv64_module_zero_extent_legal(type);
    if (global->storage_size == 0U && !zero_extent) return false;
    if (zero_extent) {
        if (global->initializer_slot_count != 0U ||
            global->relocation_slot_count != 0U) return false;
    } else if (global->is_zero_initialized || global->is_tentative) {
        if (global->initializer_slot_count != 0U) return false;
    } else if (type->kind == MINIC_CORE_TYPE_RECORD) {
        if (!type->value.record.is_complete ||
            global->initializer_slot_count == 0U) return false;
    } else if (rv64_module_array_recursive(module->type_context, type) ||
               (type->kind == MINIC_CORE_TYPE_ARRAY &&
                global->relocation_slot_count != 0U)) {
        if (global->initializer_slot_count == 0U) return false;
    } else if (global->relocation_slot_count != 0U ||
               !rv64_module_scalar_width(module->type_context, type,
                                          &scalar_width) ||
               global->initializer_slot_count >
                    global->storage_size / scalar_width) {
        return false;
    }
    return true;
}

static const char *rv64_module_visibility(MinicCoreModuleVisibility visibility) {
    switch (visibility) {
    case MINIC_CORE_MODULE_VISIBILITY_DEFAULT: return NULL;
    case MINIC_CORE_MODULE_VISIBILITY_HIDDEN: return ".hidden";
    case MINIC_CORE_MODULE_VISIBILITY_INTERNAL: return ".internal";
    case MINIC_CORE_MODULE_VISIBILITY_PROTECTED: return ".protected";
    default: return NULL;
    }
}

bool minic_riscv64_emit_module_global(FILE *file, const MinicTargetInfo *target,
                                      const MinicCoreModule *module, size_t ordinal) {
    const MinicCoreModuleGlobal *global;
    unsigned int alignment_power;
    const char *visibility;
    if (file == NULL || target == NULL ||
        target->architecture != MINIC_TARGET_ARCH_RISCV64 ||
        target->data_layout == NULL || target->data_layout->pointer_size != 8U ||
        module == NULL || !module->finalized ||
        ordinal >= module->global_count || module->globals == NULL)
        return false;
    global = &module->globals[ordinal];
    if (!rv64_module_admit_global(module, global) ||
        !minic_riscv64_alignment_power(global->storage_alignment, &alignment_power) ||
        (global->visibility != MINIC_CORE_MODULE_VISIBILITY_DEFAULT &&
         rv64_module_visibility(global->visibility) == NULL)) return false;
    if (global->section_name != NULL) {
        if (!minic_multi_elf_safe_section_name(global->section_name) ||
            fprintf(file, ".section %s,\"%s\",@progbits\n",
                    global->section_name,
                    global->is_read_only ? "a" : "aw") < 0) return false;
    } else if (fprintf(file, "%s\n",
                       global->is_read_only
                           ? (global->relocation_slot_count != 0U
                                ? ".section .data.rel.ro" : ".section .rodata")
                           : ".data") < 0) {
        return false;
    }
    if (!global->is_internal) {
        visibility = rv64_module_visibility(global->visibility);
        if (fprintf(file, "%s %s\n", global->is_weak ? ".weak" : ".globl",
                    global->name) < 0 ||
            (visibility != NULL &&
             fprintf(file, "%s %s\n", visibility, global->name) < 0))
            return false;
    }
    if (fprintf(file, ".type %s, @object\n.align %u\n%s:\n",
                global->name, alignment_power, global->name) < 0)
        return false;
    /* GNU zero-extent definitions contain no payload directives. */
    if (global->storage_size != 0U &&
        !minic_multi_module_emit_global_fragments(
            file, global, minic_riscv64_integer_data_directive, true, 8U))
        return false;
    return fprintf(file, ".size %s, %zu\n",
                   global->name, global->storage_size) >= 0;
}

