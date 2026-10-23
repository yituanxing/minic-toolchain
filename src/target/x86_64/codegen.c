#include "target/x86_64/codegen.h"
#include "target/x86_64/machine_emit.h"
#include "target/common/core_call_view.h"

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
                              const MinicCoreFunction *fn, size_t function_id,
                              bool function_sections, MinicDiagnostic *diag, const char *path) {
    MinicX86Frame frame = {0};
    const char *name = minic_c0_function_symbol_name(declaration);
    const char *vis;
    size_t block_index;
    bool success = false;
    {
        const bool named = name != NULL && name[0] != '\0';
        const bool verified = named && minic_core_function_verify(fn);
        const bool planned = verified &&
            minic_x86_64_frame_init(program, target, fn, &frame);
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
    if (fprintf(out, ".type %s,@function\n%s:\n"
                     "  pushq %%rbp\n  movq %%rsp, %%rbp\n", name, name) < 0)
        goto done;
    if (frame.frame_size != 0U &&
        fprintf(out, "  subq $%zu, %%rsp\n", frame.frame_size) < 0) goto done;
    if (frame.objects_from_aligned_rsp &&
        fprintf(out, "  andq $-%zu, %%rsp\n", frame.object_base_alignment) < 0)
        goto done;
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
    for (block_index = 0U; block_index < fn->block_count; ++block_index) {
        const MinicCoreBlock *block = &fn->blocks[block_index];
        size_t k;
        if (fprintf(out, ".Lmini_x86_f%zu_b%zu:\n", function_id, block_index) < 0) goto done;
        if (block->source_label_id != SIZE_MAX && !fn->suppress_source_label_aliases &&
            fprintf(out, ".Luser_%zu:\n", block->source_label_id) < 0) goto done;
        for (k = 0U; k < block->instruction_count; ++k) {
            const MinicCoreInstructionId id = block->instructions[k];
            if (id >= fn->instruction_count ||
                !minic_x86_64_emit_core_instruction(out, program, target, fn, &frame,
                                      &fn->instructions[id], function_id)) {
                char message[256];
                (void)snprintf(message, sizeof(message),
                               "x86-64 Core instruction unsupported: function %s, kind %d",
                               name, id < fn->instruction_count ? (int)fn->instructions[id].kind : -1);
                x86_error(diag, path, message);
                goto done;
            }
        }
        if (!block->has_terminator ||
            !minic_x86_64_emit_core_terminator(out, &frame, &block->terminator, fn, function_id)) {
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
    if (fprintf(out, ".Lmini_x86_f%zu_ret:\n  leave\n  ret\n"
                     ".size %s,.-%s\n", function_id, name, name) < 0) goto done;
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

bool minic_x86_64_write_c0_program_with_core_functions(
    const char *path, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *core_functions, size_t core_function_count,
    bool function_sections, MinicDiagnostic *diagnostic) {
    FILE *out;
    size_t i;
    bool success = true;
    if (path == NULL || program == NULL || target == NULL ||
        target->architecture != MINIC_TARGET_ARCH_X86_64 ||
        (core_function_count != 0U && core_functions == NULL) ||
        core_function_count != program->function_count) {
        x86_error(diagnostic, path, "x86-64 invalid program or target");
        return false;
    }
    if (program->file_asm_count != 0U) {
        x86_error(diagnostic, path, "x86-64 file-scope asm not implemented yet");
        return false;
    }
    out = fopen(path, "wb");
    if (out == NULL) {
        x86_error(diagnostic, path, "x86-64 cannot create assembly output");
        return false;
    }
    for (i = 0U; i < program->global_object_count && success; ++i) {
        const MinicGlobalObject *obj = &program->global_objects[i];
        if (obj->is_extern) {
            if (obj->alias_target != MINIC_GLOBAL_OBJECT_INVALID) {
                x86_error(diagnostic, path, "x86-64 external global alias unsupported");
                success = false;
            }
            continue;
        }
        success = minic_x86_64_emit_global(out, program, target, obj);
        if (!success) {
            char message[256];
            (void)snprintf(message, sizeof(message),
                           "x86-64 global initializer/relocation unsupported: %s "
                           "(record=%d init=%zu reloc=%zu union=%zu zero=%d tentative=%d)",
                           obj->name != NULL ? obj->name : "<unnamed>",
                           (int)minic_type_is_record(obj->type),
                           obj->initializer_count, obj->relocation_count,
                           obj->union_selection_count,
                           (int)obj->is_zero_initialized, (int)obj->is_tentative);
            x86_error(diagnostic, path, message);
        }
    }
    for (i = 0U; i < program->function_count && success; ++i) {
        const MinicFunction *f = &program->functions[i];
        if (!f->is_defined || (f->is_internal && !f->is_referenced)) continue;
        if (f->alias_target != MINIC_FUNCTION_INVALID) {
            x86_error(diagnostic, path, "x86-64 function alias not implemented yet");
            success = false;
        } else {
            success = x86_emit_function(out, program, target, f, &core_functions[i],
                                        i, function_sections, diagnostic, path);
        }
    }
    /* ELF/GNU-stack contract: emitted objects never request executable stack. */
    if (success && fprintf(out, ".section .note.GNU-stack,\"\",@progbits\n") < 0)
        success = false;
    if (fclose(out) != 0) success = false;
    if (!success) (void)remove(path);
    return success;
}
