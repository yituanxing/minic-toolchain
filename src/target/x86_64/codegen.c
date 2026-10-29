#include "target/x86_64/codegen.h"
#include "target/x86_64/machine_emit.h"
#include "target/common/core_call_view.h"
#include "target/common/core_block_walk.h"
#include "target/common/program_visit.h"
#include "target/common/program_emit.h"
#include "target/common/elf_alias_emit.h"

#include "target/data_layout.h"
#include "target/x86_64/global_emit.h"
#include "target/x86_64/abi.h"
#include "target/x86_64/frame.h"

#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void x86_error(MinicDiagnostic *diag, const char *path, const char *message) {
    if (diag != NULL) {
        diag->path = path;
        diag->line = 1U;
        diag->column = 1U;
        (void)snprintf(diag->message, sizeof(diag->message), "%s", message);
    }
}

static bool x86_emit_function(FILE *out, const MinicC0Program *program,
                              const MinicTargetInfo *target, const MinicFunction *declaration,
                              const MinicCoreFunction *fn, const MinicCoreFunctionFacts *facts,
                              const MinicCoreObjectStoragePlan *storage,
                              size_t function_id, bool function_sections, MinicDiagnostic *diag, const char *path) {
    MinicX86Frame frame = {0};
    const char *name = minic_c0_function_symbol_name(declaration);
    const char *vis;
    size_t block_index;
    MinicCoreBlockWalk walk = {0};
    bool success = false;
    {
        const bool named = name != NULL && name[0] != '\0';
        const bool verified = named &&
            (minic_core_function_facts_verified(facts, fn) ||
             minic_core_function_verify(fn));
        const bool planned = verified &&
            minic_x86_64_frame_init_with_storage(
                program, target, fn, facts, storage, &frame);
        if (!planned) {
            if (getenv("MINIC_M1_TRACE") != NULL) {
                (void)fprintf(stderr,
                    "MINIC_X86_FRAME_REJECT function=%s stage=%s "
                    "values=%zu params=%zu blocks=%zu\n",
                    named ? name : "(unnamed)",
                    !named ? "invalid-name" :
                    !verified ? "core-verification" : "abi-or-frame",
                    fn->value_count, fn->parameter_count, fn->block_count);
                if (verified)
                    (void)minic_core_function_dump(stderr, fn);
            }
            minic_x86_64_frame_destroy(&frame);
            x86_error(diag, path,
                      "x86-64 cannot allocate or verify Core function frame");
            return false;
        }
    }
    if (declaration->section_name != NULL) {
        if (fprintf(out, ".section %s,\"ax\",@progbits\n", declaration->section_name) < 0)
            goto done;
    } else if (function_sections) {
        if (fprintf(out, ".section .text.%s,\"ax\",@progbits\n", name) < 0) goto done;
    } else if (fprintf(out, ".text\n") < 0) goto done;
    if (!declaration->is_internal) {
        if (fprintf(out, declaration->is_weak ? ".weak %s\n" : ".globl %s\n", name) < 0)
            goto done;
        vis = declaration->visibility == MINIC_SYMBOL_VISIBILITY_HIDDEN ? ".hidden" :
              declaration->visibility == MINIC_SYMBOL_VISIBILITY_INTERNAL ? ".internal" :
              declaration->visibility == MINIC_SYMBOL_VISIBILITY_PROTECTED ? ".protected" : NULL;
        if (declaration->visibility != MINIC_SYMBOL_VISIBILITY_DEFAULT &&
            (vis == NULL || fprintf(out, "%s %s\n", vis, name) < 0)) goto done;
    }
    if ((declaration->explicit_alignment != 0U &&
         fprintf(out, ".balign %zu\n", declaration->explicit_alignment) < 0) ||
        fprintf(out, ".type %s,@function\n%s:\n"
                     "  pushq %%rbp\n  movq %%rsp, %%rbp\n", name, name) < 0)
        goto done;
    if (frame.frame_size != 0U &&
        fprintf(out, "  subq $%zu, %%rsp\n", frame.frame_size) < 0) goto done;
    if (frame.ms_nonvolatile_gp_save_offset != 0U) {
        size_t i;
        if (fprintf(out,
                "  movq %%rdi, -%zu(%%rbp)\n"
                "  movq %%rsi, -%zu(%%rbp)\n",
                frame.ms_nonvolatile_gp_save_offset,
                frame.ms_nonvolatile_gp_save_offset + 8U) < 0)
            goto done;
        for (i = 0U; i < 10U; ++i)
            if (fprintf(out, "  movdqu %%xmm%zu, -%zu(%%rbp)\n",
                        i + 6U,
                        frame.ms_nonvolatile_xmm_save_offset + i * 16U) < 0)
                goto done;
    }
    if (frame.has_sret &&
        fprintf(out, "  movq %%rdi, -%zu(%%rbp)\n",
                frame.sret_save_offset) < 0) goto done;
    if (frame.objects_from_aligned_rsp &&
        fprintf(out, "  andq $-%zu, %%rsp\n", frame.object_base_alignment) < 0)
        goto done;
    if (frame.saves_incoming_gp) {
        static const char *const regs[6] = {
            "%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"};
        size_t i;
        for (i = 0U; i < 6U; ++i)
            if (fprintf(out, "  movq %s, -%zu(%%rbp)\n",
                        regs[i], frame.incoming_gp_save_offset + i * 8U) < 0)
                goto done;
    }
    if (frame.varargs_count != 0U) {
        static const char *const gp_registers[] =
            {"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"};
        size_t k;
        if (fprintf(out, "  leaq -%zu(%%rbp), %%r10\n",
                    frame.varargs_regsave_offset) < 0) goto done;
        for (k = 0U; k < 6U; ++k) {
            if (fprintf(out, "  movq %s, %zu(%%r10)\n",
                        gp_registers[k], k * 8U) < 0) goto done;
        }
        for (k = 0U; k < 8U; ++k) {
            if (fprintf(out, "  movdqu %%xmm%zu, %zu(%%r10)\n",
                        k, 48U + k * 16U) < 0) goto done;
        }
    }
    if (fprintf(out, "  jmp .Lmini_x86_f%zu_b%u\n",
                function_id, (unsigned int)fn->entry_block) < 0) goto done;
    minic_core_block_walk_begin(&walk, fn, NULL);
    {
    const MinicCoreBlock *block;
    while (minic_core_block_walk_next(&walk, &block_index, &block)) {
        size_t k;
        if (fprintf(out, ".Lmini_x86_f%zu_b%zu:\n", function_id, block_index) < 0) goto done;
        if (block->source_label_id != SIZE_MAX && !fn->suppress_source_label_aliases &&
            fprintf(out, ".Luser_%zu:\n", block->source_label_id) < 0) goto done;
        for (k = 0U; k < block->instruction_count; ++k) {
            MinicCoreInstructionId id = 0U;
            const MinicCoreInstruction *ins = NULL;
            if (!minic_core_block_instruction_at(fn, block, k, &id, &ins) ||
                !minic_x86_64_emit_core_instruction(out, program, target, fn, &frame,
                                      ins, function_id, id)) {
                char message[256];
                if (getenv("MINIC_ASM_FRONTIER_TRACE") && ins &&
                    ins->kind == MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM &&
                    ins->value.structured_inline_asm.inline_asm_id <
                    fn->inline_asm_count) {
                    const MinicCoreInlineAsm *a = &fn->inline_asms[
                        ins->value.structured_inline_asm.inline_asm_id];
                    size_t j;
                    (void)fprintf(stderr,
                        "X86_TARGET_STRUCTURED_FRONTIER function=%s "
                        "asm_length=%zu operands=%zu volatile=%d memory=%d "
                        "preview=%.120s\n", name, a->template_length,
                        ins->value.structured_inline_asm.operand_count,
                        (int)a->is_volatile, (int)a->has_memory_clobber,
                        a->template_text ? a->template_text : "");
                    for (j = 0U; j < ins->value.structured_inline_asm.operand_count;
                         ++j) {
                        const MinicCoreStructuredInlineAsmOperand *b =
                            &ins->value.structured_inline_asm.operands[j];
                        (void)fprintf(stderr,
                            "X86_TARGET_STRUCTURED_OPERAND index=%zu "
                            "kind=%d value=%u fixed=%d\n", j,
                            (int)b->kind, (unsigned)b->value,
                            (int)b->has_fixed_register_binding);
                    }
                }
                (void)snprintf(message, sizeof(message),
                               "x86-64 Core instruction unsupported: function %s, kind %d",
                               name, id < fn->instruction_count ? (int)fn->instructions[id].kind : -1);
                x86_error(diag, path, message);
                goto done;
            }
        }
        if (!block->has_terminator ||
            !minic_x86_64_emit_core_terminator(
                out, program, target, &frame, &block->terminator, fn, function_id)) {
            char message[256];
            (void)snprintf(message, sizeof(message),
                           "x86-64 Core terminator unsupported: function %s block=%zu kind=%d "
                           "has_terminator=%d return_object=%u",
                           name, block_index,
                           block->has_terminator ? (int)block->terminator.kind : -1,
                           block->has_terminator ? 1 : 0,
                           block->has_terminator
                               ? (unsigned int)block->terminator.return_object
                               : 0U);
            x86_error(diag, path, message);
            goto done;
        }
    }
    }
    if (fprintf(out, ".Lmini_x86_f%zu_ret:\n", function_id) < 0) goto done;
    if (frame.ms_nonvolatile_gp_save_offset != 0U) {
        size_t i;
        /* Epilogue must not touch RAX/XMM0: they hold the scalar return. */
        for (i = 0U; i < 10U; ++i)
            if (fprintf(out, "  movdqu -%zu(%%rbp), %%xmm%zu\n",
                        frame.ms_nonvolatile_xmm_save_offset + i * 16U,
                        i + 6U) < 0)
                goto done;
        if (fprintf(out,
                "  movq -%zu(%%rbp), %%rdi\n"
                "  movq -%zu(%%rbp), %%rsi\n",
                frame.ms_nonvolatile_gp_save_offset,
                frame.ms_nonvolatile_gp_save_offset + 8U) < 0)
            goto done;
    }
    if (fprintf(out, "  leave\n  ret\n.size %s,.-%s\n", name, name) < 0)
        goto done;
    /* GNU constructor: ELF startup loader traverses .init_array before main.
     * Do not use ordinary .data; register a relocated function pointer. */
    if (declaration->is_constructor &&
        fprintf(out, ".section .init_array,\"aw\",@init_array\n"
                     ".p2align 3\n  .quad %s\n", name) < 0) goto done;
    success = true;
done:
    minic_x86_64_frame_destroy(&frame);
    return success;
}

typedef struct MinicX86VisitContext {
    MinicDiagnostic *diagnostic;
    const char *path;
} MinicX86VisitContext;

static bool x86_visit_global(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicGlobalObject *obj, size_t index, void *opaque) {
    const MinicX86VisitContext *ctx = opaque;
    (void)index;
    if (obj->is_extern) {
        if (obj->alias_target != MINIC_GLOBAL_OBJECT_INVALID)
            return minic_multi_elf_emit_object_alias(
                out, program, obj, "@object");
        return true;
    }
    if (minic_x86_64_emit_global(out, program, target, obj)) return true;
    {
        char message[256];
        if (getenv("MINIC_X86_GLOBAL_TRACE") != NULL) {
            const MinicArrayType *outer = minic_type_is_array(obj->type)
                ? minic_c0_program_array_type(program, obj->type.array_type_id) : NULL;
            size_t r;
            (void)fprintf(stderr,
                          "MINIC_X86_GLOBAL_FRONTIER object=%s base=%u depth=%u "
                          "array_count=%zu array_zero=%d element_base=%u "
                          "element_record=%zu init=%zu reloc=%zu\n",
                          obj->name != NULL ? obj->name : "<unnamed>",
                          (unsigned int)obj->type.base_kind,
                          obj->type.pointer_depth,
                          outer != NULL ? outer->element_count : 0U,
                          outer != NULL && outer->is_zero_length ? 1 : 0,
                          outer != NULL ? (unsigned int)outer->element_type.base_kind : 0U,
                          outer != NULL ? outer->element_type.record_id : 0U,
                          obj->initializer_count, obj->relocation_count);
            if (outer != NULL && minic_type_is_record(outer->element_type)) {
                const MinicRecord *record = minic_c0_program_record(
                    program, outer->element_type.record_id);
                size_t f;
                (void)fprintf(stderr,
                              "MINIC_X86_GLOBAL_RECORD_FRONTIER record=%zu "
                              "count=%zu union=%d complete=%d\n",
                              outer->element_type.record_id,
                              record != NULL ? record->field_count : 0U,
                              record != NULL && record->is_union ? 1 : 0,
                              record != NULL && record->is_complete ? 1 : 0);
                if (record != NULL) for (f = 0U; f < record->field_count; ++f) {
                    const MinicRecordField *field = &record->fields[f];
                    (void)fprintf(stderr,
                                  "MINIC_X86_GLOBAL_FIELD_FRONTIER index=%zu "
                                  "base=%u count=%zu bitfield=%d union=%d\n",
                                  f, (unsigned int)field->type.base_kind,
                                  field->element_count, field->is_bit_field ? 1 : 0,
                                  minic_type_is_record(field->type) ? 1 : 0);
                }
            }
            for (r = 0U; r < obj->relocation_count; ++r) {
                const MinicGlobalRelocation *reloc = &obj->relocations[r];
                (void)fprintf(stderr,
                              "MINIC_X86_GLOBAL_RELOCATION_FRONTIER object=%s "
                              "index=%zu kind=%u slot=%zu target_kind=%u "
                              "target_id=%zu addend=%" PRId64 "\n",
                              obj->name != NULL ? obj->name : "<unnamed>",
                              r, (unsigned int)reloc->location_kind,
                              reloc->location_index, (unsigned int)reloc->target_kind,
                              reloc->target_id, reloc->target_byte_addend);
            }
        }
        (void)snprintf(message, sizeof(message),
                       "x86-64 global initializer/relocation unsupported: %s "
                       "(record=%d init=%zu reloc=%zu union=%zu zero=%d tentative=%d)",
                       obj->name != NULL ? obj->name : "<unnamed>",
                       (int)minic_type_is_record(obj->type),
                       obj->initializer_count, obj->relocation_count,
                       obj->union_selection_count,
                       (int)obj->is_zero_initialized, (int)obj->is_tentative);
        x86_error(ctx->diagnostic, ctx->path, message);
    }
    return false;
}

static bool x86_visit_function(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicFunction *decl, const MinicCoreFunction *fn,
    const MinicCoreFunctionFacts *facts,
    const MinicCoreObjectStoragePlan *storage, size_t index,
    bool function_sections, void *opaque) {
    const MinicX86VisitContext *ctx = opaque;
    return x86_emit_function(out, program, target, decl, fn, facts, storage, index,
                             function_sections, ctx->diagnostic, ctx->path);
}

static bool x86_between(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *functions, size_t function_count,
    bool function_sections, void *context) {
    return minic_program_emit_source_file_asm_stage(
               out, program, target, functions, function_count,
               function_sections, context) &&
           minic_multi_elf_emit_function_aliases(out, program, "@function");
}

bool minic_x86_64_write_c0_program_with_core_functions(
    const char *path, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *core_functions, size_t core_function_count,
    bool function_sections, MinicDiagnostic *diagnostic) {
    static const MinicProgramEmitConfig config = {
        x86_visit_global, x86_visit_function, NULL,
        x86_between,
        ".section .note.GNU-stack,\"\",@progbits\n", NULL,
        MINIC_PROGRAM_FUNCTION_LIVE_DEFINITIONS, false
    };
    bool success;
    const MinicX86VisitContext visits = { diagnostic, path };
    if (path == NULL || program == NULL || target == NULL ||
        target->architecture != MINIC_TARGET_ARCH_X86_64 ||
        (core_function_count != 0U && core_functions == NULL) ||
        core_function_count != program->function_count) {
        x86_error(diagnostic, path, "x86-64 invalid program or target");
        return false;
    }
    success = minic_program_emit_write(
        path, program, target, core_functions, core_function_count,
        function_sections, &config, (void *)&visits);
    if (!success && diagnostic != NULL && diagnostic->message[0] == '\0')
        x86_error(diagnostic, path, "x86-64 cannot emit assembly");
    return success;
}
