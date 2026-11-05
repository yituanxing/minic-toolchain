#ifndef MINIC_TARGET_RISCV32_INTERNAL_H
#define MINIC_TARGET_RISCV32_INTERNAL_H
#include "core/core_ir.h"
#include "core/module_ir.h"
#include "target/common/core_function_facts.h"
#include "target/common/core_object_storage.h"
#include "minic/compiler.h"
#include "target/target_info.h"
#include "target/riscv/isa_features.h"
#include "target/riscv32/rv32_abi_plan.h"
#include "target/riscv/machine_frame_select.h"
#include <stdbool.h>
#include <stddef.h>

/* RV32I/ILP32 physical-machine contract: Core IR never owns frame slots,
 * integer-register placement, scratch-register choices or ABI stack. */
typedef struct Rv32Frame {
    /* ISA policy is a by-value snapshot, never a pointer into a copied
     * target or a short-lived lowerer's configuration. */
    MinicRiscvIsa isa;
    /* Outgoing ILP32 stack arguments are pre-reserved at sp+0 on every call.
     * This keeps sp 16-byte aligned, including nested GNU/LLVM library calls,
     * and avoids moving the Core SSA/local object frame during calls. */
    size_t outgoing_size;
    size_t frame_size;
    /* RV64-aligned-frame contract mirrored with an ILP32 4-byte entry SP. */
    bool has_dynamic_stack_alignment;
    size_t stack_alignment;
    size_t entry_sp_offset;
    /* Saved hidden sret pointer for record returns larger than 2*XLEN. */
    size_t sret_offset;
    size_t saved_base_offset; /* caller's s0 in dynamic-frame functions */
    bool has_dynamic_stack;
    bool is_variadic;
    size_t varargs_size;
    size_t varargs_offset;
    size_t *object_offsets;
    size_t *value_offsets;
    MinicCoreObjectStoragePlan object_storage;
    bool borrows_object_storage;
    size_t object_count;
    size_t value_count;
    /* ABI is planned before frame allocation; outgoing area size and every
     * machine CALL consume this one verified immutable call layout. */
    Rv32FunctionAbiPlan abi;
} Rv32Frame;

/* Finalized ILP32 ABI frame ownership: rv32_frame.c. Physical transfers
 * are emitted only through the shared typed RISC-V MachineIR selector. */
bool rv32_frame_init_with_storage(Rv32Frame *,
    const MinicCoreFunction *, const MinicTargetInfo *, bool,
    const MinicCoreFunctionFacts *, const MinicCoreObjectStoragePlan *);
void rv32_frame_destroy(Rv32Frame *);
/* Borrow the already-finalized ILP32 frame/psABI facts for physical MIR.
 * Never run a second frame allocation or argument-register cursor here. */
bool rv32_machine_physical_frame(const Rv32Frame *,
                                 MinicRiscvPhysicalFrame *);
/* Sealed Core scalar/type widths, used by the live ABI planner. */
/* Every machine scalar is classified from its sealed Core type ID. */
bool rv32_core_scalar_storage_words(const MinicCoreFunction *, MinicCoreTypeId,
                                    unsigned *);
bool rv32_core_scalar(const MinicCoreFunction *, MinicCoreTypeId,
                      unsigned *, bool *);
bool rv32_core_is_wide_integer(const MinicCoreFunction *, MinicCoreTypeId);
bool rv32_core_unsigned_integer(const MinicCoreFunction *, MinicCoreTypeId);
bool rv32_core_integer_type(const MinicCoreFunction *, MinicCoreTypeId);
bool rv32_core_pointer_type(const MinicCoreFunction *, MinicCoreTypeId);
MinicCoreTypeId rv32_core_pointee_type(const MinicCoreFunction *,
                                      MinicCoreTypeId);
#endif
