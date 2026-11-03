#ifndef MINIC_CORE_MODULE_IR_H
#define MINIC_CORE_MODULE_IR_H

/* Translation-unit symbols, data and source-owned file ASM after semantic
 * lowering.  This representation has no C types, AST pointers or GAS
 * directives.  The backend owns physical object-file/assembly syntax. */
#include "core/core_type.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum MinicCoreModuleVisibility {
    MINIC_CORE_MODULE_VISIBILITY_DEFAULT = 0,
    MINIC_CORE_MODULE_VISIBILITY_HIDDEN,
    MINIC_CORE_MODULE_VISIBILITY_INTERNAL,
    MINIC_CORE_MODULE_VISIBILITY_PROTECTED
} MinicCoreModuleVisibility;

typedef enum MinicCoreModuleFragmentKind {
    MINIC_CORE_MODULE_FRAGMENT_ZERO = 0,
    MINIC_CORE_MODULE_FRAGMENT_BITS,
    MINIC_CORE_MODULE_FRAGMENT_RELOCATION
} MinicCoreModuleFragmentKind;

typedef enum MinicCoreModuleRelocationKind {
    MINIC_CORE_MODULE_RELOCATION_OBJECT = 0,
    MINIC_CORE_MODULE_RELOCATION_FUNCTION,
    MINIC_CORE_MODULE_RELOCATION_LABEL
} MinicCoreModuleRelocationKind;

/* These are target-neutral initializer *shapes*, not instructions or ELF
 * encodings. Retaining the ingress classifier's exact evidence also lets
 * an ISA retain an intentionally narrower global-initializer policy. */
typedef enum MinicCoreModulePayloadKind {
    MINIC_CORE_MODULE_PAYLOAD_ZERO = 0,
    MINIC_CORE_MODULE_PAYLOAD_ZERO_RELOCATIONS,
    MINIC_CORE_MODULE_PAYLOAD_RECORD_FLAT,
    MINIC_CORE_MODULE_PAYLOAD_RECORD_RECURSIVE,
    MINIC_CORE_MODULE_PAYLOAD_RECORD_FLEXIBLE,
    MINIC_CORE_MODULE_PAYLOAD_ARRAY_RECORD,
    MINIC_CORE_MODULE_PAYLOAD_ARRAY_RECURSIVE,
    MINIC_CORE_MODULE_PAYLOAD_ARRAY_SCALAR_RELOCATIONS,
    MINIC_CORE_MODULE_PAYLOAD_SCALAR_RELOCATION,
    MINIC_CORE_MODULE_PAYLOAD_SCALAR_BITS
} MinicCoreModulePayloadKind;

typedef enum MinicCoreModuleDefaultSection {
    MINIC_CORE_MODULE_SECTION_BSS = 0,
    MINIC_CORE_MODULE_SECTION_DATA,
    MINIC_CORE_MODULE_SECTION_RODATA,
    MINIC_CORE_MODULE_SECTION_RELRO
} MinicCoreModuleDefaultSection;

typedef struct MinicCoreModuleFragment {
    MinicCoreModuleFragmentKind kind;
    size_t byte_offset;
    /* ZERO: nonzero run length; BITS or RELOCATION: scalar byte width. */
    size_t byte_count;
    uint64_t bits;
    /* ELF scalar decimal spelling may sign-extend an integer literal. The
     * layout remains bit-exact regardless of the assembler's formatting. */
    bool bits_signed_integer;
    MinicCoreModuleRelocationKind relocation_kind;
    /* Owned symbolic identity for object/function and local-label targets.
     * Label relocations reference the exact local symbol emitted by the
     * corresponding published Core block; no source statement IDs escape. */
    char *relocation_symbol;
    int64_t relocation_addend;
} MinicCoreModuleFragment;

/* Original source ordinal is the array index, including entries that a
 * reachability pass later marks non-emittable. All strings/fragments owned. */
typedef struct MinicCoreModuleGlobal {
    char *name;
    char *section_name;
    char *alias_target_name;
    MinicCoreTypeId core_type_id;
    size_t storage_size;
    size_t storage_alignment;
    size_t explicit_alignment;
    /* Sealed initializer-shape evidence (not C types or AST locations).
     * Some targets intentionally admit only nonempty flattened payloads. */
    size_t initializer_slot_count;
    size_t relocation_slot_count;
    size_t alias_target_index; /* SIZE_MAX when not an alias. */
    size_t owner_function_index; /* SIZE_MAX at file scope. */
    MinicCoreModuleVisibility visibility;
    MinicCoreModulePayloadKind payload_kind;
    MinicCoreModuleDefaultSection default_section;
    bool emittable;
    bool is_extern;
    bool is_alias;
    bool is_internal;
    bool is_weak;
    bool is_read_only;
    bool is_zero_initialized;
    bool is_tentative;
    bool is_block_scope_extern_only;
    bool is_thread_local;
    /* Sealed target-neutral proof for Common ELF NOBITS .bss policy.
     * RV64's independent explicit-section @progbits policy need not use it. */
    bool common_elf_bss_admitted;
    MinicCoreModuleFragment *fragments;
    size_t fragment_count;
    size_t fragment_capacity;
} MinicCoreModuleGlobal;

typedef struct MinicCoreModuleFunctionSymbol {
    char *name; /* canonical assembler-facing symbol, not a C declarator */
    char *section_name;
    size_t explicit_alignment;
    size_t alias_target_index; /* SIZE_MAX when not an alias. */
    char *alias_target_name;
    MinicCoreModuleVisibility visibility;
    bool is_defined;
    bool is_internal;
    bool is_referenced;
    bool is_weak;
    bool is_constructor;
    bool is_variadic;
} MinicCoreModuleFunctionSymbol;

typedef struct MinicCoreModuleFileAsm {
    char *text;
    size_t length;
} MinicCoreModuleFileAsm;

typedef struct MinicCoreModule {
    /* Borrowed only: the enclosing CoreFunctionSet owns the common arena. */
    const MinicCoreTypeContext *type_context;
    MinicCoreModuleGlobal *globals;
    size_t global_count;
    MinicCoreModuleFunctionSymbol *function_symbols;
    size_t function_count;
    MinicCoreModuleFileAsm *file_asms;
    size_t file_asm_count;
    bool finalized;
} MinicCoreModule;

void minic_core_module_initialize(MinicCoreModule *module);
void minic_core_module_destroy(MinicCoreModule *module);
/* Pure, target-neutral initializer layout contract, reusable by each object
 * format writer. ISA-specific relocation widths/directives remain Target-owned. */
bool minic_core_module_global_fragments_valid(const MinicCoreModuleGlobal *global);
bool minic_core_module_verify(const MinicCoreModule *module);

#endif
