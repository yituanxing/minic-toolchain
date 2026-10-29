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
 * The fallback for pre-migration target constructors uses
 * the existing count contract, so old and new Target parsers coexist safely
 * while backend migrations proceed. */
void minias_mcinst_destroy(MiniAsMCInst *inst) {
    size_t i, slots;
    if (inst == NULL) return;
    slots = inst->capacity != 0U ? inst->capacity : inst->count;
    for (i = 0U; i < slots; ++i) {
        MiniAsMCOperand *op = &inst->operands[i];
        if (op->kind == MINIAS_MC_SYMBOL) {
            free(op->symbol);
        } else if (op->kind == MINIAS_MC_SYMBOL_DIFFERENCE) {
            free(op->difference);
        } else if (op->kind == MINIAS_MC_MEMORY && op->memory != NULL) {
            if (inst->dispose_memory != NULL)
                inst->dispose_memory(op->memory);
            else
                free(op->memory);
        }
    }
    free(inst);
}
