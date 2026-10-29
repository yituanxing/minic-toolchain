#include "minias_internal.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* Addressing payloads are ISA-private.  This fake Target provides a different
 * opaque memory shape from x86 to ensure Common invokes the registered
 * lifetime hook without ever knowing how memory operands are represented. */
typedef struct FakeAddress {
    MiniAsSymbolExpr *symbol;
    unsigned address_format;
} FakeAddress;

static unsigned memory_destroyed;

static void dispose_fake_address(void *opaque) {
    FakeAddress *address = opaque;
    assert(address != NULL && address->address_format == 42U);
    free(address->symbol);
    free(address);
    ++memory_destroyed;
}

/* The assembler must be able to unwind a parser error before the target has
 * assigned a semantic operand count, including target-private nested data. */
static void partial_parse_unwind(void) {
    MiniAsMCInst *inst = minias_mcinst_alloc(6U);
    assert(inst != NULL);
    assert(inst->capacity == 6U && inst->count == 0U);
    inst->dispose_memory = dispose_fake_address;

    inst->operands[0].kind = MINIAS_MC_SYMBOL;
    inst->operands[0].symbol = calloc(1U, sizeof(MiniAsSymbolExpr));
    assert(inst->operands[0].symbol != NULL);
    strcpy(inst->operands[0].symbol->name, "global_symbol");

    inst->operands[1].kind = MINIAS_MC_SYMBOL_DIFFERENCE;
    inst->operands[1].difference = calloc(1U, sizeof(MiniAsSymbolDifference));
    assert(inst->operands[1].difference != NULL);
    strcpy(inst->operands[1].difference->positive.name, "end");
    strcpy(inst->operands[1].difference->negative.name, "begin");

    inst->operands[2].kind = MINIAS_MC_MEMORY;
    inst->operands[2].memory = calloc(1U, sizeof(FakeAddress));
    assert(inst->operands[2].memory != NULL);
    FakeAddress *first = inst->operands[2].memory;
    first->address_format = 42U;
    first->symbol = calloc(1U, sizeof(MiniAsSymbolExpr));
    assert(first->symbol != NULL);
    strcpy(first->symbol->name, "pc_relative_global");

    inst->operands[3].kind = MINIAS_MC_REGISTER;
    inst->operands[3].reg = 31U;
    inst->operands[4].kind = MINIAS_MC_MEMORY;
    inst->operands[4].memory = calloc(1U, sizeof(FakeAddress));
    assert(inst->operands[4].memory != NULL);
    FakeAddress *second = inst->operands[4].memory;
    second->address_format = 42U;
    inst->operands[5].kind = MINIAS_MC_IMMEDIATE;
    inst->operands[5].immediate = UINT64_C(0xdeadbeef);

    /* Emulates an ISA parser failing after recording only the first operand.
     * The allocated capacity must still free slots 1, 2 and 4. */
    inst->count = 1U;
    minias_mcinst_destroy(inst);
    assert(memory_destroyed == 2U);
}

static void legacy_target_compatibility(void) {
    MiniAsMCInst *inst = calloc(1U, sizeof(*inst) +
                                3U * sizeof(MiniAsMCOperand));
    assert(inst != NULL);
    assert(inst->capacity == 0U);
    inst->count = 3U;
    inst->operands[0].kind = MINIAS_MC_REGISTER;
    inst->operands[1].kind = MINIAS_MC_SYMBOL;
    inst->operands[1].symbol = calloc(1U, sizeof(MiniAsSymbolExpr));
    assert(inst->operands[1].symbol != NULL);
    inst->operands[2].kind = MINIAS_MC_MEMORY;
    inst->operands[2].memory = calloc(1U, sizeof(FakeAddress));
    assert(inst->operands[2].memory != NULL);
    minias_mcinst_destroy(inst);
}

int main(void) {
    partial_parse_unwind();
    legacy_target_compatibility();
    minias_mcinst_destroy(NULL);
    MiniAsMCInst *empty = minias_mcinst_alloc(0U);
    assert(empty != NULL);
    minias_mcinst_destroy(empty);
    return 0;
}
