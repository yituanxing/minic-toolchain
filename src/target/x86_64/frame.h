#ifndef MINIC_TARGET_X86_64_FRAME_H
#define MINIC_TARGET_X86_64_FRAME_H

#include "core/core_ir.h"
#include "frontend/ast.h"
#include "target/target_info.h"
#include "target/common/core_object_storage.h"
#include "target/common/core_function_facts.h"
#include "target/x86_64/call_plan.h"

#include <stdbool.h>
#include <stddef.h>

/* x86-64 ABI frame *plan* (SysV or Microsoft x64), not emitted instructions. The backend
 * retains independent register moves, object-address and prologue code.
 * A future Machine IR may consume or replace this target-specific plan. */
/* Per-Core-value 8-byte frame slots, or 16 bytes for int128/x87 long double.
 * The extra limb is x86 backend-owned; Core IR identities are unchanged. */
typedef struct MinicX86Frame {
    size_t value_count;
    size_t *value_offsets;
    /* Borrowed from the function's canonical use census while emitting
     * its blocks. A NULL census retains the standalone emitter's
     * conservative always-spill behavior. This is not a register allocator:
     * live Core values still use their original ABI-safe frame slots. */
    const size_t *function_value_use_counts;
    /* One-instruction, target-owned transport for Common's proven adjacent
     * single-use scalar GP pair. x86's producer leaves its value in %rax;
     * the immediately following consumer can read that same %rax directly.
     * Boolean guards keep standalone/zero-initialized frame clients inert. */
    bool gp_forward_source_valid;
    MinicCoreValueId gp_forward_source_id;
    bool gp_elide_result_store_valid;
    MinicCoreValueId gp_elide_result_store_id;
    size_t *object_offsets;
    MinicCoreObjectStoragePlan object_storage;
    bool borrows_object_storage;
    MinicX86ArgumentPlan incoming_arguments;
    MinicX86OutgoingCallPlans outgoing_call_plans;
    /* Borrowed for the duration of this function emission. Target machine
     * location planning remains x86-owned and consumes semantic CALL views. */
    const MinicCoreCallIndex *semantic_calls;
    size_t object_count;
    size_t frame_size;
    /* Before early x86 kernel page tables are installed, a .head.text C
     * function needs link-time kernel virtual symbol values for physical
     * relocation arithmetic. RIP-relative LEA would instead materialize
     * the currently executing (relocated) physical symbol address. */
    bool kernel_head_link_addresses;
    /* Wide scalar operations use RDX as a scratch register. Snapshot the
     * incoming SysV GP bank in the prologue before lowering any Core body.
     * This keeps a later TI parameter alive across an earlier param's
     * two-word Core load/store and general source evaluation. */
    bool saves_incoming_gp;
    size_t incoming_gp_save_offset;
    /* A Win64 callee must preserve RDI/RSI and XMM6-XMM15, even when its
     * body calls a SysV function that is free to overwrite those registers.
     * Each save owns disjoint rbp-relative frame slots. */
    size_t ms_nonvolatile_gp_save_offset;
    size_t ms_nonvolatile_xmm_save_offset;
    /* Hidden SysV memory-result destination occupies RDI and must survive
     * arbitrary nested calls until every aggregate return path. */
    bool has_sret;
    size_t sret_save_offset;
    size_t object_base_alignment;
    bool objects_from_aligned_rsp;
    /* SysV x86-64 va_list: gp_offset / fp_offset / overflow / reg_save.
     * Each va_start owns its own 24-byte cursor tag, while the incoming
     * argument registers have one shared 176-byte save area per function. */
    MinicCoreValueId *varargs_results;
    size_t varargs_count;
    size_t varargs_regsave_offset;
    size_t varargs_tag_base_offset;
    size_t varargs_fixed_stack_slots;
    unsigned int varargs_gp_offset;
    unsigned int varargs_fp_offset;
} MinicX86Frame;

bool minic_x86_64_frame_init_with_storage(
    const MinicC0Program *, const MinicTargetInfo *, const MinicCoreFunction *,
    const MinicCoreFunctionFacts *, const MinicCoreObjectStoragePlan *,
    MinicX86Frame *);
void minic_x86_64_frame_destroy(MinicX86Frame *frame);

#endif
