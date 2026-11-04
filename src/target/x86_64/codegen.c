#include "target/x86_64/codegen.h"
#include "target/x86_64/machine_emit.h"
#include "target/common/core_call_view.h"
#include "target/common/core_block_walk.h"
#include "target/common/direct_block_register.h"
#include "target/common/program_visit.h"
#include "target/common/program_emit.h"
#include "target/common/elf_alias_emit.h"
#include "target/common/global_emit.h"
#include "target/common/module_global_emit.h"

#include "target/x86_64/global_emit.h"
#include "target/x86_64/abi.h"
#include "target/x86_64/frame.h"
#include "target/x86_64/codegen_options.h"
#include "core/value_uses.h"
#include "core/symbol_identity.h"

#include <inttypes.h>
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

/* Function codegen consumes the frozen Core type graph.  In particular an
 * enum's arithmetic shape and the pointee of a qualified pointer must not
 * be reconstructed from the source-language type arena. */
static const MinicCoreType *x86_function_type(const MinicCoreFunction *fn,
                                              MinicCoreTypeId id) {
    if (fn == NULL || fn->type_context == NULL) return NULL;
    return minic_core_type_get(fn->type_context,
                               minic_core_type_unqualified(fn->type_context, id));
}

static bool x86_function_kind(const MinicCoreFunction *fn,
                              MinicCoreTypeId id, MinicCoreTypeKind kind) {
    const MinicCoreType *type = x86_function_type(fn, id);
    return type != NULL && type->kind == kind;
}

static bool x86_function_direct_gp_scalar(const MinicCoreFunction *fn,
                                          MinicCoreTypeId id) {
    const MinicCoreType *type = x86_function_type(fn, id);
    return type != NULL &&
           (type->kind == MINIC_CORE_TYPE_POINTER ||
            (type->kind == MINIC_CORE_TYPE_INTEGER &&
             type->value.integer.bit_width <= 64U));
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
    if (fn == NULL || frame == NULL || producer == NULL || consumer == NULL ||
        use_counts == NULL ||
        producer->kind != MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS ||
        producer->value.object_id >= frame->object_count ||
        frame->object_offsets == NULL ||
        id == MINIC_CORE_VALUE_INVALID || id >= fn->value_count ||
        use_counts[id] != 1U ||
        !x86_function_kind(fn, fn->values[id].core_type_id,
                           MINIC_CORE_TYPE_POINTER))
        return false;
    if (consumer->kind == MINIC_CORE_INSTRUCTION_LOAD)
        return consumer->value.load.address == id &&
               x86_function_direct_gp_scalar(fn, consumer->core_type_id);
    if (consumer->kind != MINIC_CORE_INSTRUCTION_STORE ||
        consumer->value.store.address != id ||
        consumer->value.store.stored_value >= fn->value_count)
        return false;
    return x86_function_direct_gp_scalar(
        fn, fn->values[consumer->value.store.stored_value].core_type_id);
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
                x86_function_kind(fn,
                                  fn->values[ins->value.operand].core_type_id,
                                  MINIC_CORE_TYPE_POINTER) &&
                x86_function_kind(fn, ins->core_type_id,
                                  MINIC_CORE_TYPE_INTEGER))
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

/* Both head-address passes inspect exactly the same memory-address roles.
 * Aggregate COPY and vector storage have two independently rebased
 * addresses, unlike scalar LOAD/STORE. Keep this Core decoding local to
 * the head-address policy and do not infer pointer provenance through PHI. */
static size_t x86_head_memory_addresses(
    const MinicCoreInstruction *ins, MinicCoreValueId addresses[2]) {
    addresses[0] = MINIC_CORE_VALUE_INVALID;
    addresses[1] = MINIC_CORE_VALUE_INVALID;
    switch (ins->kind) {
    case MINIC_CORE_INSTRUCTION_LOAD:
        addresses[0] = ins->value.load.address;
        return 1U;
    case MINIC_CORE_INSTRUCTION_STORE:
        addresses[0] = ins->value.store.address;
        return 1U;
    case MINIC_CORE_INSTRUCTION_ATOMIC_LOAD:
        addresses[0] = ins->value.atomic_load.address;
        return 1U;
    case MINIC_CORE_INSTRUCTION_ATOMIC_STORE:
        addresses[0] = ins->value.atomic_store.address;
        return 1U;
    case MINIC_CORE_INSTRUCTION_RECORD_LOAD:
        addresses[0] = ins->value.record_load.source_address;
        return 1U;
    case MINIC_CORE_INSTRUCTION_RECORD_COPY:
        addresses[0] = ins->value.record_copy.destination_address;
        addresses[1] = ins->value.record_copy.source_address;
        return 2U;
    case MINIC_CORE_INSTRUCTION_VECTOR_STORAGE_COPY:
        addresses[0] = ins->value.vector_storage_copy.destination_address;
        addresses[1] = ins->value.vector_storage_copy.source_address;
        return 2U;
    default:
        return 0U;
    }
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
            MinicCoreValueId addresses[2];
            size_t k, count;
            if (id >= fn->instruction_count) { free(defs); return false; }
            ins = &fn->instructions[id];
            count = x86_head_memory_addresses(ins, addresses);
            for (k = 0U; k < count; ++k) {
                MinicCoreGlobalId global = x86_head_address_global(
                    fn, defs, addresses[k], NULL);
                if (global < fn->global_count) direct[global] = true;
            }
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
            MinicCoreValueId addresses[2];
            MinicCoreGlobalId global;
            bool crossed_vma_cast = false;
            size_t k, count;
            if (id >= fn->instruction_count) { free(defs); return false; }
            ins = &fn->instructions[id];
            count = x86_head_memory_addresses(ins, addresses);
            switch (ins->kind) {
            case MINIC_CORE_INSTRUCTION_SCALAR_BITCAST:
                if (ins->result < fn->value_count &&
                    ins->value.operand < fn->value_count &&
                    x86_function_kind(fn,
                                      fn->values[ins->value.operand].core_type_id,
                                      MINIC_CORE_TYPE_POINTER) &&
                    x86_function_kind(fn, ins->core_type_id,
                                      MINIC_CORE_TYPE_INTEGER)) {
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
            for (k = 0U; k < count; ++k) {
                MinicCoreValueId address = addresses[k];
                global = x86_head_address_global(
                    fn, defs, address, &crossed_vma_cast);
                if (address < fn->value_count && global < fn->global_count &&
                    direct[global] && crossed_vma_cast)
                    memory_rebase_globals[address] = global;
            }
        }
    }
    free(defs);
    return true;
}

static bool x86_no_sse_fp_type(const MinicCoreFunction *fn,
                              MinicCoreTypeId id) {
    return x86_function_kind(fn, id, MINIC_CORE_TYPE_FLOAT);
}

/* -mno-sse is a code-generation contract, not permission to emit an
 * instruction that will #UD before the kernel enables FPU/XMM state. Reject
 * reachable floating ABI/operations until x86 has a genuine soft-FP path.
 * Do not reject unused floating declarations or unreachable lexical arms. */
static bool x86_no_sse_function_safe(
    const MinicCoreFunction *fn, const bool *reachable) {
    size_t i, j;
    if (fn == NULL || fn->type_context == NULL ||
        (fn->parameter_count != 0U && fn->core_parameter_type_ids == NULL))
        return false;
    if (x86_no_sse_fp_type(fn, fn->core_return_type_id)) return false;
    for (i = 0U; i < fn->parameter_count; ++i)
        if (x86_no_sse_fp_type(fn, fn->core_parameter_type_ids[i]))
            return false;
    for (i = 0U; i < fn->block_count; ++i) {
        const MinicCoreBlock *block = &fn->blocks[i];
        if (!reachable[i]) continue;
        for (j = 0U; j < block->instruction_count; ++j) {
            const MinicCoreInstructionId id = block->instructions[j];
            const MinicCoreInstruction *ins;
            if (id >= fn->instruction_count) return false;
            ins = &fn->instructions[id];
            if (x86_no_sse_fp_type(fn, ins->core_type_id) ||
                (ins->result < fn->value_count &&
                 x86_no_sse_fp_type(fn,
                     fn->values[ins->result].core_type_id)))
                return false;
            if (ins->kind == MINIC_CORE_INSTRUCTION_CALL ||
                ins->kind == MINIC_CORE_INSTRUCTION_INDIRECT_CALL) {
                MinicCoreCallView call;
                size_t k;
                if (!minic_core_call_view_build(fn, ins, &call) ||
                    (call.parameter_count != 0U &&
                     call.core_parameter_type_ids == NULL) ||
                    x86_no_sse_fp_type(fn, call.core_return_type_id))
                    return false;
                for (k = 0U; k < call.parameter_count; ++k)
                    if (x86_no_sse_fp_type(fn,
                            call.core_parameter_type_ids[k]))
                        return false;
            }
        }
    }
    return true;
}

static bool x86_emit_function(FILE *out,
                              const MinicTargetInfo *target,
                              const MinicCoreModuleFunctionSymbol *module_decl,
                              const MinicCoreModule *module,
                              const MinicCoreFunction *fn, const MinicCoreFunctionFacts *facts,
                              const MinicCoreObjectStoragePlan *storage,
                              size_t function_id, bool function_sections,
                              bool inherited_head_address_context,
                              MinicDiagnostic *diag, const char *path) {
    MinicX86Frame frame = {0};
    const char *name = module_decl->name;
    const char *section_name = module_decl->section_name;
    const size_t explicit_alignment = module_decl->explicit_alignment;
    const bool is_internal = module_decl->is_internal;
    const bool is_weak = module_decl->is_weak;
    const bool is_constructor = module_decl->is_constructor;
    size_t block_index;
    MinicCoreBlockWalk walk = {0};
    const bool *reachable = NULL;
    bool *owned_reachable = NULL;
    bool *head_direct_data_globals = NULL;
    MinicCoreGlobalId *head_vma_cast_globals = NULL;
    MinicCoreGlobalId *head_memory_rebase_globals = NULL;
    const size_t *function_value_use_counts = NULL;
    size_t *owned_value_use_counts = NULL;
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
                target, fn, facts, storage, &frame);
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
    frame.module = module;
    /* Match the shared Core CFG visibility contract: source labels remain
     * roots for computed goto and asm-goto, while dead lexical arms must not
     * contribute machine instructions or unresolved linker references. */
    if (minic_core_function_facts_verified(facts, fn)) {
        reachable = facts->reachable_blocks;
        function_value_use_counts = facts->value_use_counts;
    } else {
        owned_reachable = calloc(fn->block_count == 0U ? 1U : fn->block_count,
                                 sizeof(*owned_reachable));
        if (owned_reachable != NULL &&
            minic_core_function_mark_reachable_blocks(
                fn, owned_reachable, fn->block_count))
            reachable = owned_reachable;
    }
    if (reachable == NULL) {
        x86_error(diag, path, "x86-64 cannot discover reachable Core blocks");
        goto done;
    }
    if ((target->x86_codegen_options & MINIC_X86_CODEGEN_NO_SSE) != 0U &&
        !x86_no_sse_function_safe(fn, reachable)) {
        x86_error(diag, path,
                  "x86-64 -mno-sse rejects reachable floating ABI or operation");
        goto done;
    }
    if (function_value_use_counts == NULL) {
        if (fn->value_count > SIZE_MAX / sizeof(*owned_value_use_counts) ||
            (owned_value_use_counts = calloc(
                fn->value_count == 0U ? 1U : fn->value_count,
                sizeof(*owned_value_use_counts))) == NULL) {
            x86_error(diag, path, "x86-64 cannot allocate Core value-use census");
            goto done;
        }
        /* Standalone function entry is not owned by ProgramVisitor. */
        if (!minic_core_function_value_use_counts(
                fn, owned_value_use_counts, fn->value_count)) {
            x86_error(diag, path, "x86-64 cannot count Core function SSA uses");
            goto done;
        }
        function_value_use_counts = owned_value_use_counts;
    }
    frame.function_value_use_counts = function_value_use_counts;
    frame.kernel_head_link_addresses = !target->position_independent &&
        (inherited_head_address_context ||
         (section_name != NULL && strcmp(section_name, ".head.text") == 0));
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
    if (section_name != NULL) {
        if (fprintf(out, ".section %s,\"ax\",@progbits\n", section_name) < 0)
            goto done;
    } else if (function_sections) {
        if (fprintf(out, ".section .text.%s,\"ax\",@progbits\n", name) < 0) goto done;
    } else if (fprintf(out, ".text\n") < 0) goto done;
    if (!is_internal) {
        if (fprintf(out, is_weak ? ".weak %s\n" : ".globl %s\n", name) < 0)
            goto done;
        if (!minic_multi_module_emit_elf_symbol_visibility(
                out, module_decl->visibility, name)) goto done;
    }
    if (explicit_alignment != 0U &&
        fprintf(out, ".balign %zu\n", explicit_alignment) < 0)
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
        /* Linux Kbuild's -mno-sse is not just a performance option: before
         * CR4.OSFXSR is enabled, even spilling an unused %xmm register in an
         * early exception handler raises #UD. The kernel never has SysV FP
         * variadic register arguments under this ABI mode. In ordinary SysV
         * code, %al specifies the FP argument-register count, so avoid the
         * optional SSE spills altogether when its value is zero. */
        if ((target->x86_codegen_options & MINIC_X86_CODEGEN_NO_SSE) == 0U) {
            if (fprintf(out, "  testb %%al, %%al\n"
                             "  jz .Lmini_x86_f%zu_varargs_no_fp\n",
                        function_id) < 0) goto done;
            for (k = 0U; k < abi->fp_argument_count; ++k) {
                if (fprintf(out, "  movdqu %%xmm%zu, %zu(%%r10)\n",
                            k, abi->gp_argument_count * 8U + k * 16U) < 0)
                    goto done;
            }
            if (fprintf(out, ".Lmini_x86_f%zu_varargs_no_fp:\n",
                        function_id) < 0) goto done;
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
        if (block->local_label_symbol != NULL && !fn->suppress_local_label_aliases) {
            if (!minic_core_label_symbol_valid(block->local_label_symbol) ||
                fprintf(out, "%s:\n", block->local_label_symbol) < 0)
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
            if (!minic_x86_64_emit_core_instruction(out, module, target, fn, &frame,
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
                out, target, &frame, &block->terminator, fn, function_id)) {
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
    if (is_constructor &&
        fprintf(out, ".section .init_array,\"aw\",@init_array\n"
                     ".p2align 3\n  .quad %s\n", name) < 0) goto done;
    success = true;
done:
    free(head_direct_data_globals);
    free(head_vma_cast_globals);
    free(head_memory_rebase_globals);
    free(owned_value_use_counts);
    free(owned_reachable);
    minic_x86_64_frame_destroy(&frame);
    return success;
}

typedef struct MinicX86VisitContext {
    MinicDiagnostic *diagnostic;
    const char *path;
    const bool *head_address_context;
    const MinicCoreModule *module;
} MinicX86VisitContext;

/* An unannotated static helper called exclusively from .head.text executes
 * before the normal kernel virtual mapping exists. The section annotation
 * belongs to the caller, but its address-role contract reaches the callee:
 * fixup_pointer(&global, physbase) needs the *link VMA*, not a RIP-relative
 * already-relocated pointer. Do not extend this to a helper shared by normal
 * code or whose address escapes through a function pointer. */
static bool *x86_head_address_call_closure(
    const MinicCoreFunction *functions, size_t function_count,
    const MinicCoreModule *module) {
    MinicCoreModuleFunctionNameIndex names = {0};
    bool *head = NULL, *head_call = NULL, *normal_call = NULL, *address_taken = NULL;
    bool changed;
    size_t i;
    if (module == NULL || module->function_count != function_count ||
        (function_count != 0U && functions == NULL) ||
        !minic_core_module_function_name_index_build(module, &names))
        return NULL;
    head = calloc(function_count == 0U ? 1U : function_count, sizeof(*head));
    head_call = calloc(function_count == 0U ? 1U : function_count,
                       sizeof(*head_call));
    normal_call = calloc(function_count == 0U ? 1U : function_count,
                         sizeof(*normal_call));
    address_taken = calloc(function_count == 0U ? 1U : function_count,
                           sizeof(*address_taken));
    if (!head || !head_call || !normal_call || !address_taken)
        goto fail;
    for (i = 0U; i < function_count; ++i) {
        const MinicCoreModuleFunctionSymbol *module_decl =
            &module->function_symbols[i];
        const char *section = module_decl->section_name;
        head[i] = section != NULL && strcmp(section, ".head.text") == 0;
    }
    /* Taking a function's address permits an opaque, possibly non-head
     * caller, regardless of the direct-call census. */
    for (i = 0U; i < function_count; ++i) {
        const MinicCoreFunction *fn = &functions[i];
        size_t j;
        /* The source of an address edge must itself be emitted. In
         * particular, same-asm-name static candidates can be lowered for
         * Core reachability without surviving its final fixed point. */
        if (!minic_program_module_function_selected(
                &module->function_symbols[i],
                MINIC_PROGRAM_FUNCTION_LIVE_DEFINITIONS)) continue;
        for (j = 0U; j < fn->instruction_count; ++j) {
            const MinicCoreInstruction *ins = &fn->instructions[j];
            size_t begin, end, k;
            const MinicCoreFunctionSymbol *symbol;
            if (ins->kind != MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS ||
                ins->value.function_symbol_id >= fn->function_symbol_count)
                continue;
            symbol = &fn->function_symbols[ins->value.function_symbol_id];
            if (!minic_core_module_function_name_index_find(
                    &names, symbol->name, symbol->name_length, &begin, &end))
                goto fail;
            for (k = begin; k < end; ++k)
                address_taken[names.entries[k].function_index] = true;
        }
    }
    do {
        changed = false;
        memset(head_call, 0, function_count * sizeof(*head_call));
        memset(normal_call, 0, function_count * sizeof(*normal_call));
        for (i = 0U; i < function_count; ++i) {
            const MinicCoreFunction *fn = &functions[i];
            size_t j;
            if (!minic_program_module_function_selected(
                    &module->function_symbols[i],
                    MINIC_PROGRAM_FUNCTION_LIVE_DEFINITIONS)) continue;
            for (j = 0U; j < fn->instruction_count; ++j) {
                const MinicCoreInstruction *ins = &fn->instructions[j];
                size_t begin, end, k;
                const MinicCoreCallee *callee;
                if (ins->kind != MINIC_CORE_INSTRUCTION_CALL ||
                    ins->value.call.callee_id >= fn->callee_count)
                    continue;
                callee = &fn->callees[ins->value.call.callee_id];
                if (!minic_core_module_function_name_index_find(
                        &names, callee->name, callee->name_length,
                        &begin, &end)) goto fail;
                for (k = begin; k < end; ++k) {
                    const size_t target_index = names.entries[k].function_index;
                    if (head[i]) head_call[target_index] = true;
                    else normal_call[target_index] = true;
                }
            }
        }
        for (i = 0U; i < function_count; ++i) {
            const MinicCoreModuleFunctionSymbol *module_decl =
                &module->function_symbols[i];
            const bool internal = module_decl->is_internal;
            const char *section = module_decl->section_name;
            if (!head[i] && head_call[i] && !normal_call[i] &&
                !address_taken[i] && internal && section == NULL) {
                head[i] = true;
                changed = true;
            }
        }
    } while (changed);
    minic_core_module_function_name_index_destroy(&names);
    free(head_call);
    free(normal_call);
    free(address_taken);
    return head;
fail:
    minic_core_module_function_name_index_destroy(&names);
    free(head);
    free(head_call);
    free(normal_call);
    free(address_taken);
    return NULL;
}


/* Production ModuleIR path: global payload and source symbol facts were
 * already sealed before target emission.  No C AST/global descriptor reads. */
static bool x86_visit_module_global(
    FILE *out, const MinicCoreModule *module, const MinicTargetInfo *target,
    const MinicCoreModuleGlobal *global, size_t index, void *opaque) {
    MinicX86VisitContext *ctx = opaque;
    if (global->is_extern) {
        if (global->alias_target_index != SIZE_MAX)
            return minic_multi_elf_emit_module_object_alias(
                out, module, global, "@object");
        return true;
    }
    if (minic_x86_64_emit_module_global(out, target, module, index))
        return true;
    if (getenv("MINIC_X86_GLOBAL_TRACE") != NULL)
        (void)fprintf(stderr,
            "MINIC_X86_MODULE_GLOBAL_REJECT object=%s index=%zu fragments=%zu\n",
            global->name != NULL ? global->name : "<unnamed>",
            index, global->fragment_count);
    x86_error(ctx->diagnostic, ctx->path,
              "x86-64 cannot emit Core module global");
    return false;
}

static bool x86_visit_function(
    FILE *out, const MinicCoreModule *module, const MinicTargetInfo *target,
    const MinicCoreModuleFunctionSymbol *module_decl,
    const MinicCoreFunction *fn,
    const MinicCoreFunctionFacts *facts,
    const MinicCoreObjectStoragePlan *storage, size_t index,
    bool function_sections, void *opaque) {
    const MinicX86VisitContext *ctx = opaque;
    return x86_emit_function(out, target, module_decl,
                             module, fn, facts, storage, index,
                             function_sections,
                             ctx->head_address_context != NULL &&
                             ctx->head_address_context[index],
                             ctx->diagnostic, ctx->path);
}

static bool x86_between(
    FILE *out, const MinicCoreModule *module, const MinicTargetInfo *target,
    const MinicCoreFunction *functions, size_t function_count,
    bool function_sections, void *context) {
    (void)functions;
    (void)function_count;
    (void)function_sections;
    (void)context;
    if (!minic_program_emit_source_file_asm_module(out, module) ||
        !minic_multi_elf_emit_module_function_aliases(
            out, module, "@function"))
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

static bool x86_write_core_module_impl(
    const char *path, const MinicTargetInfo *target,
    const MinicCoreFunction *core_functions, size_t core_function_count,
    bool function_sections, const MinicCoreModule *module,
    MinicDiagnostic *diagnostic) {
    static const MinicProgramEmitConfig config = {
        x86_visit_function, NULL,
        x86_between,
        ".section .note.GNU-stack,\"\",@progbits\n", NULL,
        MINIC_PROGRAM_FUNCTION_LIVE_DEFINITIONS, false
    };
    bool success;
    MinicX86VisitContext visits = { diagnostic, path, NULL, module };
    if (path == NULL || module == NULL || target == NULL ||
        target->architecture != MINIC_TARGET_ARCH_X86_64 ||
        (core_function_count != 0U && core_functions == NULL) ||
        module->function_count != core_function_count) {
        x86_error(diagnostic, path, "x86-64 invalid module or target");
        return false;
    }
    if (!target->position_independent) {
        visits.head_address_context = x86_head_address_call_closure(
            core_functions, core_function_count, module);
        if (visits.head_address_context == NULL) {
            x86_error(diagnostic, path, "x86-64 cannot plan early head call roles");
            return false;
        }
    }
    success = minic_program_emit_write_module(
        path, module, target, core_functions, core_function_count,
        function_sections, &config, (void *)&visits,
        x86_visit_module_global);
    free((void *)visits.head_address_context);
    if (!success && diagnostic != NULL && diagnostic->message[0] == '\0')
        x86_error(diagnostic, path, "x86-64 cannot emit assembly");
    return success;
}

bool minic_x86_64_write_core_module(
    const char *path, const MinicTargetInfo *target,
    const MinicCoreFunction *core_functions, size_t core_function_count,
    bool function_sections, const MinicCoreModule *module,
    MinicDiagnostic *diagnostic) {
    return module != NULL &&
           x86_write_core_module_impl(path, target, core_functions,
                                       core_function_count, function_sections,
                                       module, diagnostic);
}
