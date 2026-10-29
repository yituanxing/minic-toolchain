#ifndef MINIC_TARGET_COMMON_DIRECT_PROGRAM_EMIT_H
#define MINIC_TARGET_COMMON_DIRECT_PROGRAM_EMIT_H

#include "core/core_ir.h"
#include "target/common/core_function_facts.h"
#include "target/common/core_object_storage.h"
#include "frontend/ast.h"
#include "minic/compiler.h"
#include "target/target_info.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/* Two tested direct LP64 backends share the TU-level failure/cleanup and
 * GNU/ELF output lifecycle, NOT instruction selection or ABI register banks.
 * Machine IR would replace the individual function callbacks, not this
 * target-neutral object/function serialization loop. */
typedef bool (*MinicMultiDirectEmitFunction)(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicFunction *decl, const MinicCoreFunction *fn,
    const MinicCoreFunctionFacts *facts,
    const MinicCoreObjectStoragePlan *storage, size_t function_id,
    bool function_sections);

typedef struct MinicMultiDirectProgramConfig {
    MinicTargetArchitecture architecture;
    const char *architecture_name; /* diagnostic/trace lowercase ISA name */
    const char *display_name;      /* AArch64 / LoongArch64 */
    const char *gnu_stack_note;    /* literal encoded GNU .note directive */
    const char *elf_function_type; /* target GNU AS type spelling, e.g. %function */
    const char *elf_object_type;   /* target GNU AS type spelling, e.g. %object */
    MinicMultiDirectEmitFunction emit_function;
} MinicMultiDirectProgramConfig;

bool minic_multi_direct_write_program(
    const char *path, const MinicC0Program *program,
    const MinicTargetInfo *target, const MinicCoreFunction *core_functions,
    size_t core_function_count, bool function_sections,
    const MinicMultiDirectProgramConfig *config,
    MinicDiagnostic *diagnostic);

/* Shared byte-identical opt-in Core reject diagnostics. The caller passes
 * the source location (__LINE__) so a target's actual gate stays visible. */
bool minic_multi_direct_machine_reject(
    const MinicCoreFunction *fn, const char *arch,
    const char *name, const char *stage, size_t block_id,
    size_t instruction_id, unsigned int backend_line);

#endif
