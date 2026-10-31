#include "target/x86_64/codegen.h"
#include "target/x86_64/machine_emit.h"
#include "target/common/core_call_view.h"
#include "target/common/core_block_walk.h"
#include "target/common/direct_block_register.h"
#include "target/common/program_visit.h"
#include "target/common/program_emit.h"
#include "target/common/elf_alias_emit.h"
#include "target/common/global_emit.h"

#include "target/data_layout.h"
#include "target/x86_64/global_emit.h"
#include "target/x86_64/abi.h"
#include "target/x86_64/frame.h"
#include "target/x86_64/codegen_options.h"
#include "core/value_uses.h"

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

/* X86-only address-register proof. Core's exact use census and the verified
 * local FramePlan provide the sole object identity. No delayed pointer
 * transport: the next instruction in the SAME Core block must be a genuine
 * scalar LOAD/STORE with this pointer as its address. Keep atomic/wide,
 * record, CALL, asm and cross-block cases on their original stack path. */
static bool x86_adjacent_direct_object_address(
    const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *producer,
    const MinicCoreInstruction *consumer, const size_t *use_counts) {
    const MinicCoreValueId id = producer != NULL
        ? producer->result : MINIC_CORE_VALUE_INVALID;
    MinicType stored;
    if (fn == NULL || frame == NULL || producer == NULL || consumer == NULL ||
        use_counts == NULL ||
        producer->kind != MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS ||
        producer->value.object_id >= frame->object_count ||
        frame->object_offsets == NULL ||
        id == MINIC_CORE_VALUE_INVALID || id >= fn->value_count ||
        use_counts[id] != 1U || !minic_type_is_pointer(fn->values[id].type))
        return false;
    if (consumer->kind == MINIC_CORE_INSTRUCTION_LOAD)
        return consumer->value.load.address == id &&
               !minic_type_is_int128_integer(consumer->type) &&
               !minic_type_is_long_double(consumer->type) &&
               (minic_type_is_integer(consumer->type) ||
                minic_type_is_pointer(consumer->type));
    if (consumer->kind != MINIC_CORE_INSTRUCTION_STORE ||
        consumer->value.store.address != id ||
        consumer->value.store.stored_value >= fn->value_count)
        return false;
    stored = fn->values[consumer->value.store.stored_value].type;
    return !minic_type_is_int128_integer(stored) &&
           !minic_type_is_long_double(stored) &&
           (minic_type_is_integer(stored) || minic_type_is_pointer(stored));
}

/* Resolve a direct memory operand back through Core's address-only steps.
 * This is deliberately not a general pointer/alias analysis: a LOAD of a
 * pointer, a PHI, or a function argument stops the proof. In particular,
 * link-time arithmetic in __startup_64 and fixup_pointer must remain VMA
 * arithmetic even though ordinary .head.text globals are accessed at their
 * displaced physical addresses. */
static MinicCoreGlobalId x86_head_address_global(
    const MinicCoreFunction *fn, const MinicCoreInstruction *const *defs,
    MinicCoreValueId value, bool *crossed_vma_cast) {
    size_t depth;
    if (crossed_vma_cast != NULL) *crossed_vma_cast = false;
    for (depth = 0U; depth < 32U && value < fn->value_count; ++depth) {
        const MinicCoreInstruction *ins = defs[value];
        if (ins == NULL) break;
        switch (ins->kind) {
        case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS:
            return ins->value.global_id;
        case MINIC_CORE_INSTRUCTION_FIELD_ADDRESS:
            value = ins->value.field_address.base;
            continue;
        case MINIC_CORE_INSTRUCTION_POINTER_OFFSET:
            value = ins->value.pointer_offset.base;
            continue;
        case MINIC_CORE_INSTRUCTION_SCALAR_BITCAST:
            if (crossed_vma_cast != NULL &&
                ins->value.operand < fn->value_count &&
                minic_type_is_pointer(fn->values[ins->value.operand].type) &&
                minic_type_is_integer(ins->type))
                *crossed_vma_cast = true;
            value = ins->value.operand;
            continue;
        case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION:
            value = ins->value.operand;
            continue;
        default:
            break;
        }
        break;
    }
    return MINIC_CORE_GLOBAL_INVALID;
}

/* Direct global pointers, including those passed to access helpers, retain
 * their physical RIP representation. A pointer-to-integer cast is the VMA
 * *consumer*: translate that cast's result from physical to link VMA. This
 * also handles the SAME GLOBAL_ADDRESS SSA feeding a cast, a direct memory
 * use and a CALL; neither the producer nor the helper pointer is poisoned.
 * If an integer cast later becomes a pointer used for memory, rebase it at
 * the memory consumer. Never infer aliasing through a pointer LOAD/PHI. */
static bool x86_head_plan_address_roles(
    const MinicCoreFunction *fn, const bool *reachable, bool *direct,
    MinicCoreGlobalId *vma_cast_globals,
    MinicCoreGlobalId *memory_rebase_globals) {
    const MinicCoreInstruction **defs;
    size_t i, j;
    if (fn->value_count > SIZE_MAX / sizeof(*defs)) return false;
    defs = (const MinicCoreInstruction **)calloc(
        fn->value_count == 0U ? 1U : fn->value_count, sizeof(*defs));
    if (defs == NULL) return false;
    for (i = 0U; i < fn->instruction_count; ++i) {
        const MinicCoreInstruction *ins = &fn->instructions[i];
        if (ins->result != MINIC_CORE_VALUE_INVALID &&
            ins->result < fn->value_count)
            defs[ins->result] = ins;
    }
    for (i = 0U; i < fn->block_count; ++i) {
        const MinicCoreBlock *block = &fn->blocks[i];
        if (!reachable[i]) continue;
        for (j = 0U; j < block->instruction_count; ++j) {
            const MinicCoreInstructionId id = block->instructions[j];
            const MinicCoreInstruction *ins;
            MinicCoreValueId address = MINIC_CORE_VALUE_INVALID;
            MinicCoreGlobalId global;
            if (id >= fn->instruction_count) { free(defs); return false; }
            ins = &fn->instructions[id];
            switch (ins->kind) {
            case MINIC_CORE_INSTRUCTION_LOAD:
                address = ins->value.load.address;
                break;
            case MINIC_CORE_INSTRUCTION_STORE:
                address = ins->value.store.address;
                break;
            case MINIC_CORE_INSTRUCTION_ATOMIC_LOAD:
                address = ins->value.atomic_load.address;
                break;
            case MINIC_CORE_INSTRUCTION_ATOMIC_STORE:
                address = ins->value.atomic_store.address;
                break;
            default:
                break;
            }
            global = x86_head_address_global(fn, defs, address, NULL);
            if (global < fn->global_count) direct[global] = true;
        }
    }
    /* Only after the global data-use census is complete can we choose the
     * needed representation of a scalar cast and an eventual dereference. */
    for (i = 0U; i < fn->block_count; ++i) {
        const MinicCoreBlock *block = &fn->blocks[i];
        if (!reachable[i]) continue;
        for (j = 0U; j < block->instruction_count; ++j) {
            const MinicCoreInstructionId id = block->instructions[j];
            const MinicCoreInstruction *ins;
            MinicCoreValueId address = MINIC_CORE_VALUE_INVALID;
            MinicCoreGlobalId global;
            bool crossed_vma_cast = false;
            if (id >= fn->instruction_count) { free(defs); return false; }
            ins = &fn->instructions[id];
            switch (ins->kind) {
            case MINIC_CORE_INSTRUCTION_LOAD:
                address = ins->value.load.address;
                break;
            case MINIC_CORE_INSTRUCTION_STORE:
                address = ins->value.store.address;
                break;
            case MINIC_CORE_INSTRUCTION_ATOMIC_LOAD:
                address = ins->value.atomic_load.address;
                break;
            case MINIC_CORE_INSTRUCTION_ATOMIC_STORE:
                address = ins->value.atomic_store.address;
                break;
            case MINIC_CORE_INSTRUCTION_SCALAR_BITCAST:
                if (ins->result < fn->value_count &&
                    ins->value.operand < fn->value_count &&
                    minic_type_is_pointer(
                        fn->values[ins->value.operand].type) &&
                    minic_type_is_integer(ins->type)) {
                    global = x86_head_address_global(
                        fn, defs, ins->value.operand, &crossed_vma_cast);
                    if (global < fn->global_count && direct[global] &&
                        !crossed_vma_cast)
                        vma_cast_globals[ins->result] = global;
                }
                break;
            default:
                break;
            }
            global = x86_head_address_global(
                fn, defs, address, &crossed_vma_cast);
            if (address < fn->value_count && global < fn->global_count &&
                direct[global] && crossed_vma_cast)
                memory_rebase_globals[address] = global;
        }
    }
    free(defs);
    return true;
}

static bool x86_emit_function(FILE *out, const MinicC0Program *program,
                              const MinicTargetInfo *target, const MinicFunction *declaration,
                              const MinicCoreFunction *fn, const MinicCoreFunctionFacts *facts,
                              const MinicCoreObjectStoragePlan *storage,
                              size_t function_id, bool function_sections,
                              bool inherited_head_address_context,
                              MinicDiagnostic *diag, const char *path) {
    MinicX86Frame frame = {0};
    const char *name = minic_c0_function_symbol_name(declaration);
    size_t block_index;
    MinicCoreBlockWalk walk = {0};
    bool *reachable = NULL;
    bool *head_direct_data_globals = NULL;
    MinicCoreGlobalId *head_vma_cast_globals = NULL;
    MinicCoreGlobalId *head_memory_rebase_globals = NULL;
    size_t *function_value_use_counts = NULL;
    /* A diagnostic-only escape hatch enables a same-compiler A/B against the
     * former all-stack scalar path without changing any Linux Kbuild flags. */
    const bool adjacent_gp_forward_enabled =
        getenv("MINIC_X86_DISABLE_ADJACENT_GP_FORWARD") == NULL;
    const bool direct_object_address_enabled =
        getenv("MINIC_X86_DISABLE_DIRECT_OBJECT_ADDRESS") == NULL;
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
    /* Match the shared Core CFG visibility contract: source labels remain
     * roots for computed goto and asm-goto, while dead lexical arms must not
     * contribute machine instructions or unresolved linker references. */
    reachable = (bool *)calloc(fn->block_count == 0U ? 1U : fn->block_count,
                               sizeof(*reachable));
    if (reachable == NULL || !minic_core_function_mark_reachable_blocks(
            fn, reachable, fn->block_count)) {
        x86_error(diag, path, "x86-64 cannot discover reachable Core blocks");
        goto done;
    }
    if (fn->value_count > SIZE_MAX / sizeof(*function_value_use_counts) ||
        (function_value_use_counts = (size_t *)calloc(
            fn->value_count == 0U ? 1U : fn->value_count,
            sizeof(*function_value_use_counts))) == NULL) {
        x86_error(diag, path, "x86-64 cannot allocate Core value-use census");
        goto done;
    }
    /* One canonical pass for all blocks; a Core value that has no consumers
     * anywhere in the function does not need a spill writeback. */
    if (!minic_core_function_value_use_counts(
            fn, function_value_use_counts, fn->value_count)) {
        x86_error(diag, path, "x86-64 cannot count Core function SSA uses");
        goto done;
    }
    frame.function_value_use_counts = function_value_use_counts;
    frame.kernel_head_link_addresses = !target->position_independent &&
        (inherited_head_address_context ||
         (declaration->section_name != NULL &&
          strcmp(declaration->section_name, ".head.text") == 0));
    if (frame.kernel_head_link_addresses) {
        size_t i;
        if (fn->value_count > SIZE_MAX / sizeof(*head_memory_rebase_globals) ||
            fn->value_count > SIZE_MAX / sizeof(*head_vma_cast_globals)) {
            x86_error(diag, path, "x86-64 early head address plan exceeds memory limits");
            goto done;
        }
        head_direct_data_globals = (bool *)calloc(
            fn->global_count == 0U ? 1U : fn->global_count,
            sizeof(*head_direct_data_globals));
        head_vma_cast_globals = (MinicCoreGlobalId *)malloc(
            (fn->value_count == 0U ? 1U : fn->value_count) *
            sizeof(*head_vma_cast_globals));
        head_memory_rebase_globals = (MinicCoreGlobalId *)malloc(
            (fn->value_count == 0U ? 1U : fn->value_count) *
            sizeof(*head_memory_rebase_globals));
        if (head_direct_data_globals == NULL ||
            head_vma_cast_globals == NULL ||
            head_memory_rebase_globals == NULL) {
            x86_error(diag, path, "x86-64 cannot analyze early head data addresses");
            goto done;
        }
        for (i = 0U; i < fn->value_count; ++i) {
            head_vma_cast_globals[i] = MINIC_CORE_GLOBAL_INVALID;
            head_memory_rebase_globals[i] = MINIC_CORE_GLOBAL_INVALID;
        }
        if (!x86_head_plan_address_roles(
                fn, reachable, head_direct_data_globals,
                head_vma_cast_globals, head_memory_rebase_globals)) {
            x86_error(diag, path, "x86-64 cannot plan early head address roles");
            goto done;
        }
        frame.kernel_head_direct_data_globals = head_direct_data_globals;
        frame.kernel_head_vma_cast_globals = head_vma_cast_globals;
        frame.kernel_head_memory_rebase_globals = head_memory_rebase_globals;
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
        if (!minic_multi_emit_elf_symbol_visibility(
                out, declaration->visibility, name)) goto done;
    }
    if (declaration->explicit_alignment != 0U &&
        fprintf(out, ".balign %zu\n", declaration->explicit_alignment) < 0)
        goto done;
    /* GNU Linux's 16-byte entry patch area is emitted before the function
     * symbol, and its start is 16-byte aligned for live ftrace patching. */
    if ((target->x86_codegen_options &
         MINIC_X86_CODEGEN_PATCHABLE_ENTRY_16_16) != 0U &&
        fputs("  .balign 16\n", out) == EOF)
        goto done;
    if ((target->x86_codegen_options &
         MINIC_X86_CODEGEN_PATCHABLE_ENTRY_16_16) != 0U &&
        fprintf(out,
                "  .pushsection __patchable_function_entries,\"awo\",@progbits,.Lmini_x86_patch_%zu\n"
                "  .balign 8\n"
                "  .quad .Lmini_x86_patch_%zu\n"
                "  .popsection\n"
                ".Lmini_x86_patch_%zu:\n"
                "  .rept 16\n"
                "  nop\n"
                "  .endr\n", function_id, function_id, function_id) < 0)
        goto done;
    if (fprintf(out, ".type %s,@function\n%s:\n", name, name) < 0)
        goto done;
    if ((target->x86_codegen_options & MINIC_X86_CODEGEN_IBT_ENDBR) != 0U &&
        fputs("  endbr64\n", out) == EOF)
        goto done;
    if (fputs("  pushq %rbp\n  movq %rsp, %rbp\n", out) == EOF)
        goto done;
    if (frame.frame_size != 0U &&
        fprintf(out, "  subq $%zu, %%rsp\n", frame.frame_size) < 0) goto done;
    if (frame.ms_nonvolatile_gp_save_offset != 0U) {
        const MinicX86AbiRegisters *abi = minic_x86_64_abi_registers(
            MINIC_CALLING_CONVENTION_MS_X64);
        size_t i;
        if (abi == NULL || abi->extra_nonvolatile_gp == NULL) goto done;
        for (i = 0U; i < abi->extra_nonvolatile_gp_count; ++i)
            if (fprintf(out, "  movq %s, -%zu(%%rbp)\n",
                        abi->extra_nonvolatile_gp[i],
                        frame.ms_nonvolatile_gp_save_offset + i * 8U) < 0)
                goto done;
        for (i = 0U; i < abi->extra_nonvolatile_xmm_count; ++i)
            if (fprintf(out, "  movdqu %%xmm%zu, -%zu(%%rbp)\n",
                        i + abi->extra_nonvolatile_xmm_first,
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
        const MinicX86AbiRegisters *abi = minic_x86_64_abi_registers(
            MINIC_CALLING_CONVENTION_DEFAULT);
        size_t i;
        if (abi == NULL) goto done;
        for (i = 0U; i < abi->gp_argument_count; ++i)
            if (fprintf(out, "  movq %s, -%zu(%%rbp)\n",
                        abi->gp_arguments[i],
                        frame.incoming_gp_save_offset + i * 8U) < 0)
                goto done;
    }
    if (frame.varargs_count != 0U) {
        const MinicX86AbiRegisters *abi = minic_x86_64_abi_registers(
            MINIC_CALLING_CONVENTION_DEFAULT);
        size_t k;
        if (abi == NULL) goto done;
        if (fprintf(out, "  leaq -%zu(%%rbp), %%r10\n",
                    frame.varargs_regsave_offset) < 0) goto done;
        for (k = 0U; k < abi->gp_argument_count; ++k) {
            if (fprintf(out, "  movq %s, %zu(%%r10)\n",
                        abi->gp_arguments[k], k * 8U) < 0) goto done;
        }
        for (k = 0U; k < abi->fp_argument_count; ++k) {
            if (fprintf(out, "  movdqu %%xmm%zu, %zu(%%r10)\n",
                        k, abi->gp_argument_count * 8U + k * 16U) < 0)
                goto done;
        }
    }
    if (fprintf(out, "  jmp .Lmini_x86_f%zu_b%u\n",
                function_id, (unsigned int)fn->entry_block) < 0) goto done;
    minic_core_block_walk_begin(&walk, fn, reachable);
    {
    const MinicCoreBlock *block;
    while (minic_core_block_walk_next(&walk, &block_index, &block)) {
        size_t k;
        bool previous_gp_forward = false;
        MinicCoreValueId previous_gp_result = MINIC_CORE_VALUE_INVALID;
        bool previous_direct_address = false;
        MinicCoreValueId previous_direct_value = MINIC_CORE_VALUE_INVALID;
        MinicCoreObjectId previous_direct_object = MINIC_CORE_OBJECT_INVALID;
        if (fprintf(out, ".Lmini_x86_f%zu_b%zu:\n", function_id, block_index) < 0) goto done;
        if (block->source_label_id != SIZE_MAX && !fn->suppress_source_label_aliases) {
            if (fprintf(out, ".Luser_%zu:\n", block->source_label_id) < 0)
                goto done;
            /* GNU computed-goto entries are indirect branch destinations.
             * They need ENDBR independently of the function's own entry. */
            if ((target->x86_codegen_options & MINIC_X86_CODEGEN_IBT_ENDBR) != 0U &&
                fputs("  endbr64\n", out) == EOF)
                goto done;
        }
        for (k = 0U; k < block->instruction_count; ++k) {
            MinicCoreInstructionId id = 0U;
            const MinicCoreInstruction *ins = NULL;
            bool forward_to_next = false;
            bool direct_address_to_next = false;
            const MinicCoreInstruction *next = NULL;
            MinicCoreInstructionId next_id = 0U;
            if (!minic_core_block_instruction_at(fn, block, k, &id, &ins)) {
                x86_error(diag, path, "x86-64 invalid Core block instruction");
                goto done;
            }
            if (k + 1U < block->instruction_count) {
                if (!minic_core_block_instruction_at(
                        fn, block, k + 1U, &next_id, &next)) {
                    x86_error(diag, path, "x86-64 invalid next Core block instruction");
                    goto done;
                }
                if (adjacent_gp_forward_enabled)
                    forward_to_next = minic_multi_direct_gp_adjacent_forward(
                        fn, ins, next, function_value_use_counts);
                if (direct_object_address_enabled)
                    direct_address_to_next = x86_adjacent_direct_object_address(
                        fn, &frame, ins, next, function_value_use_counts);
            }
            frame.gp_forward_source_valid = previous_gp_forward;
            frame.gp_forward_source_id = previous_gp_result;
            frame.gp_elide_result_store_valid = forward_to_next;
            frame.gp_elide_result_store_id = ins->result;
            frame.direct_object_address_producer = direct_address_to_next;
            frame.direct_object_address_consumer = previous_direct_address;
            frame.direct_object_address_value_id = previous_direct_value;
            frame.direct_object_address_object_id = previous_direct_object;
            if (!minic_x86_64_emit_core_instruction(out, program, target, fn, &frame,
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
            previous_gp_forward = forward_to_next;
            previous_gp_result = ins->result;
            previous_direct_address = direct_address_to_next;
            previous_direct_value = ins->result;
            previous_direct_object = ins->kind == MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS
                ? ins->value.object_id : MINIC_CORE_OBJECT_INVALID;
        }
        /* Terminators and the next Core block never inherit %rax transport. */
        frame.gp_forward_source_valid = false;
        frame.gp_elide_result_store_valid = false;
        frame.direct_object_address_producer = false;
        frame.direct_object_address_consumer = false;
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
        const MinicX86AbiRegisters *abi = minic_x86_64_abi_registers(
            MINIC_CALLING_CONVENTION_MS_X64);
        size_t i;
        if (abi == NULL || abi->extra_nonvolatile_gp == NULL) goto done;
        /* Epilogue must not touch RAX/XMM0: they hold the scalar return. */
        for (i = 0U; i < abi->extra_nonvolatile_xmm_count; ++i)
            if (fprintf(out, "  movdqu -%zu(%%rbp), %%xmm%zu\n",
                        frame.ms_nonvolatile_xmm_save_offset + i * 16U,
                        i + abi->extra_nonvolatile_xmm_first) < 0)
                goto done;
        for (i = 0U; i < abi->extra_nonvolatile_gp_count; ++i)
            if (fprintf(out, "  movq -%zu(%%rbp), %s\n",
                        frame.ms_nonvolatile_gp_save_offset + i * 8U,
                        abi->extra_nonvolatile_gp[i]) < 0)
                goto done;
    }
    if (fputs("  leave\n", out) == EOF ||
        ((target->x86_codegen_options & MINIC_X86_CODEGEN_RETURN_EXTERNAL_THUNK) != 0U
             ? fputs("  jmp __x86_return_thunk\n", out) == EOF
             : fputs("  ret\n", out) == EOF) ||
        fprintf(out, ".size %s,.-%s\n", name, name) < 0)
        goto done;
    /* GNU constructor: ELF startup loader traverses .init_array before main.
     * Do not use ordinary .data; register a relocated function pointer. */
    if (declaration->is_constructor &&
        fprintf(out, ".section .init_array,\"aw\",@init_array\n"
                     ".p2align 3\n  .quad %s\n", name) < 0) goto done;
    success = true;
done:
    free(head_direct_data_globals);
    free(head_vma_cast_globals);
    free(head_memory_rebase_globals);
    free(function_value_use_counts);
    free(reachable);
    minic_x86_64_frame_destroy(&frame);
    return success;
}

typedef struct MinicX86VisitContext {
    MinicDiagnostic *diagnostic;
    const char *path;
    const bool *head_address_context;
} MinicX86VisitContext;

typedef struct MinicX86FunctionNameIndex {
    const char *name;
    size_t index;
} MinicX86FunctionNameIndex;

static int x86_compare_function_names(const void *left, const void *right) {
    const MinicX86FunctionNameIndex *a = left, *b = right;
    return strcmp(a->name, b->name);
}

static size_t x86_find_function_name(
    const MinicX86FunctionNameIndex *names, size_t count, const char *name) {
    const MinicX86FunctionNameIndex key = {name, 0U};
    const MinicX86FunctionNameIndex *found;
    if (name == NULL) return SIZE_MAX;
    found = bsearch(&key, names, count, sizeof(*names),
                    x86_compare_function_names);
    return found != NULL ? found->index : SIZE_MAX;
}

/* An unannotated static helper called exclusively from .head.text executes
 * before the normal kernel virtual mapping exists. The section annotation
 * belongs to the caller, but its address-role contract reaches the callee:
 * fixup_pointer(&global, physbase) needs the *link VMA*, not a RIP-relative
 * already-relocated pointer. Do not extend this to a helper shared by normal
 * code or whose address escapes through a function pointer. */
static bool *x86_head_address_call_closure(
    const MinicC0Program *program, const MinicCoreFunction *functions,
    size_t function_count) {
    MinicX86FunctionNameIndex *names = NULL;
    bool *head = NULL, *head_call = NULL, *normal_call = NULL, *address_taken = NULL;
    bool changed;
    size_t i;
    names = calloc(function_count == 0U ? 1U : function_count, sizeof(*names));
    head = calloc(function_count == 0U ? 1U : function_count, sizeof(*head));
    head_call = calloc(function_count == 0U ? 1U : function_count,
                       sizeof(*head_call));
    normal_call = calloc(function_count == 0U ? 1U : function_count,
                         sizeof(*normal_call));
    address_taken = calloc(function_count == 0U ? 1U : function_count,
                           sizeof(*address_taken));
    if (!names || !head || !head_call || !normal_call || !address_taken)
        goto fail;
    for (i = 0U; i < function_count; ++i) {
        const MinicFunction *decl = &program->functions[i];
        names[i].name = minic_c0_function_symbol_name(decl);
        names[i].index = i;
        if (names[i].name == NULL) goto fail;
        head[i] = decl->section_name != NULL &&
                  strcmp(decl->section_name, ".head.text") == 0;
    }
    qsort(names, function_count, sizeof(*names), x86_compare_function_names);
    /* Taking a function's address permits an opaque, possibly non-head
     * caller, regardless of the direct-call census. */
    for (i = 0U; i < function_count; ++i) {
        const MinicCoreFunction *fn = &functions[i];
        size_t j;
        for (j = 0U; j < fn->instruction_count; ++j) {
            const MinicCoreInstruction *ins = &fn->instructions[j];
            size_t callee_index;
            if (ins->kind != MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS ||
                ins->value.function_symbol_id >= fn->function_symbol_count)
                continue;
            callee_index = x86_find_function_name(
                names, function_count,
                fn->function_symbols[ins->value.function_symbol_id].name);
            if (callee_index < function_count)
                address_taken[callee_index] = true;
        }
    }
    do {
        changed = false;
        memset(head_call, 0, function_count * sizeof(*head_call));
        memset(normal_call, 0, function_count * sizeof(*normal_call));
        for (i = 0U; i < function_count; ++i) {
            const MinicCoreFunction *fn = &functions[i];
            size_t j;
            for (j = 0U; j < fn->instruction_count; ++j) {
                const MinicCoreInstruction *ins = &fn->instructions[j];
                size_t callee_index;
                if (ins->kind != MINIC_CORE_INSTRUCTION_CALL ||
                    ins->value.call.callee_id >= fn->callee_count)
                    continue;
                callee_index = x86_find_function_name(
                    names, function_count,
                    fn->callees[ins->value.call.callee_id].name);
                if (callee_index >= function_count) continue;
                if (head[i]) head_call[callee_index] = true;
                else normal_call[callee_index] = true;
            }
        }
        for (i = 0U; i < function_count; ++i) {
            const MinicFunction *decl = &program->functions[i];
            if (!head[i] && head_call[i] && !normal_call[i] &&
                !address_taken[i] && decl->is_internal &&
                decl->section_name == NULL) {
                head[i] = true;
                changed = true;
            }
        }
    } while (changed);
    free(names);
    free(head_call);
    free(normal_call);
    free(address_taken);
    return head;
fail:
    free(names);
    free(head);
    free(head_call);
    free(normal_call);
    free(address_taken);
    return NULL;
}

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
                             function_sections,
                             ctx->head_address_context != NULL &&
                             ctx->head_address_context[index],
                             ctx->diagnostic, ctx->path);
}

static bool x86_between(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *functions, size_t function_count,
    bool function_sections, void *context) {
    if (!minic_program_emit_source_file_asm_stage(
            out, program, target, functions, function_count,
            function_sections, context) ||
        !minic_multi_elf_emit_function_aliases(out, program, "@function"))
        return false;
    if ((target->x86_codegen_options & MINIC_X86_CODEGEN_IBT_ENDBR) != 0U &&
        fputs(".section .note.gnu.property,\"a\"\n"
              "  .balign 8\n"
              "  .long 1f-0f\n"
              "  .long 4f-1f\n"
              "  .long 5\n"
              "0: .string \"GNU\"\n"
              "1: .balign 8\n"
              "  .long 0xc0000002\n"
              "  .long 3f-2f\n"
              "2: .long 1\n"
              "3: .balign 8\n"
              "4:\n", out) == EOF)
        return false;
    return true;
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
    MinicX86VisitContext visits = { diagnostic, path, NULL };
    if (path == NULL || program == NULL || target == NULL ||
        target->architecture != MINIC_TARGET_ARCH_X86_64 ||
        (core_function_count != 0U && core_functions == NULL) ||
        core_function_count != program->function_count) {
        x86_error(diagnostic, path, "x86-64 invalid program or target");
        return false;
    }
    if (!target->position_independent) {
        visits.head_address_context = x86_head_address_call_closure(
            program, core_functions, core_function_count);
        if (visits.head_address_context == NULL) {
            x86_error(diagnostic, path, "x86-64 cannot plan early head call roles");
            return false;
        }
    }
    success = minic_program_emit_write(
        path, program, target, core_functions, core_function_count,
        function_sections, &config, (void *)&visits);
    free((void *)visits.head_address_context);
    if (!success && diagnostic != NULL && diagnostic->message[0] == '\0')
        x86_error(diagnostic, path, "x86-64 cannot emit assembly");
    return success;
}
