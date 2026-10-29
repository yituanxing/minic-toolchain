#include "minias_internal.h"

#include <stdint.h>
#include <stdlib.h>

/* MCInst's flexible operands are the *only* Common-owned per-instruction
 * heap graph.  Targets own instruction meaning, not a parallel allocator or
 * an ABI for freeing symbol/memory payloads.  In particular, a parser may
 * fail after constructing operand N but before assigning the final semantic
 * count.  Keep allocation capacity separately so those partial operands are
 * released without knowing ISA instruction arity. */
MiniAsMCInst *minias_mcinst_alloc(unsigned capacity) {
    MiniAsMCInst *inst;
    size_t size;
    size_t operands_size = (size_t)capacity * sizeof(MiniAsMCOperand);
    if ((capacity != 0U &&
         operands_size / sizeof(MiniAsMCOperand) != (size_t)capacity) ||
        operands_size > SIZE_MAX - sizeof(MiniAsMCInst))
        return NULL;
    size = sizeof(MiniAsMCInst) + operands_size;
    inst = calloc(1U, size);
    if (inst != NULL) inst->capacity = capacity;
    return inst;
}

/* Ownership is determined by the *typed kind*, not operand position, source
 * spelling or a target-specific list of heap allocations.  Every pointer in
 * the union is exclusively owned by its parent operand.  Common never sees
 * the internal fields of a Target-owned memory operand, which may have ISA-
 * specific nested allocations released by its instruction's callback.
 * All in-tree Targets allocate through minias_mcinst_alloc, even for
 * zero-operand instructions. Capacity is the single lifetime contract:
 * reading count here would resurrect an obsolete, potentially unsafe
 * ownership path. Heap-owned memory and Target payloads must supply
 * their own destructor, including partially constructed instructions. */
void minias_mcinst_destroy(MiniAsMCInst *inst) {
    size_t i, slots;
    if (inst == NULL) return;
    slots = inst->capacity;
    for (i = 0U; i < slots; ++i) {
        MiniAsMCOperand *op = &inst->operands[i];
        if (op->kind == MINIAS_MC_BOUND_SYMBOL) {
            free(op->bound_symbol);
        } else if (op->kind == MINIAS_MC_BOUND_SYMBOL_DIFFERENCE) {
            free(op->bound_difference);
        } else if (op->kind == MINIAS_MC_MEMORY && op->memory != NULL) {
            /* A missing callback is an internal Target ownership violation,
             * not permission for Common to guess how nested ISA data dies. */
            if (inst->dispose_memory == NULL) abort();
            inst->dispose_memory(op->memory);
        }
    }
    if (inst->target_payload != NULL) {
        if (inst->dispose_target_payload == NULL) abort();
        inst->dispose_target_payload(inst->target_payload);
    }
    free(inst);
}
