#include "minias_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(const char *why) {
    fprintf(stderr, "MINIAS_MCINST_READONLY_VIEW=FAIL %s\n", why);
    return 1;
}

int main(void) {
    MiniAsMCInst *inst = minias_mcinst_alloc(4U);
    MiniAsMCOperandView operands[4];
    MiniAsMCInstView encoded;
    MiniAsInstructionView view;
    MiniAsStmt owner = {0};
    char name[] = "add";
    if (!inst) return fail("allocation");
    owner.fragment_kind = MINIAS_FRAGMENT_INSTRUCTION;
    owner.op = name;
    owner.args = "x1, x2, sym";
    owner.line = 19U;
    owner.section = 3;
    owner.subsection = 2U;
    owner.offset = 16U;
    owner.size = 4U;
    owner.source_site = 9U;
    owner.site.mode_bits = 7U;
    owner.payload.instruction.inst = inst;
    inst->opcode = 42U;
    inst->encoding = UINT32_C(0x11223344);
    inst->target_form = 8U;
    inst->count = 3U;
    inst->target_payload = malloc(8U);
    inst->dispose_target_payload = free;
    inst->operands[0].kind = MINIAS_MC_REGISTER;
    inst->operands[0].reg = 11U;
    inst->operands[0].reg_class = MINIAS_MC_REG_VECTOR;
    inst->operands[0].target_reg_bank = 5U;
    inst->operands[1].kind = MINIAS_MC_BOUND_SYMBOL;
    inst->operands[1].bound_symbol = malloc(sizeof(MiniAsSymbolRef));
    inst->operands[2].kind = MINIAS_MC_MEMORY;
    inst->operands[2].memory = malloc(4U);
    inst->dispose_memory = free;
    if (!inst->target_payload || !inst->operands[1].bound_symbol ||
        !inst->operands[2].memory) return fail("nested-allocation");
    *inst->operands[1].bound_symbol = (MiniAsSymbolRef){.name_id=17U,.addend=-3};
    memset(operands, 0xa5, sizeof(operands));
    if (!minias_project_instruction(&owner,&view,&encoded,operands,4U))
        return fail("initial-projection");
    if (view.inst != &encoded || encoded.operands != operands ||
        view.source_site != 9U || view.op != name || view.line != 19U ||
        view.section != 3 || view.subsection != 2U ||
        view.offset != 16U || view.size != 4U ||
        view.site.mode_bits != 7U || encoded.opcode != 42U ||
        encoded.encoding != UINT32_C(0x11223344) ||
        encoded.target_form != 8U || encoded.count != 3U ||
        encoded.target_payload != inst->target_payload ||
        operands[0].kind != MINIAS_MC_REGISTER ||
        operands[0].reg != 11U || operands[0].target_reg_bank != 5U ||
        operands[0].memory != NULL ||
        operands[1].bound_symbol != inst->operands[1].bound_symbol ||
        operands[1].bound_symbol->addend != -3 ||
        operands[2].memory != inst->operands[2].memory)
        return fail("field-projection");
    /* Scalars are snapshots, while bound pointers retain exactly one owner. */
    owner.offset = 32U;
    inst->operands[0].reg = 6U;
    if (view.offset != 16U || operands[0].reg != 11U ||
        !minias_project_instruction(&owner,&view,&encoded,operands,4U) ||
        view.offset != 32U || operands[0].reg != 6U)
        return fail("refresh-generation");
    if (minias_project_instruction(&owner,&view,&encoded,operands,2U))
        return fail("undersized-view-storage");
    inst->count = 5U;
    if (minias_project_instruction(&owner,&view,&encoded,operands,4U))
        return fail("count-exceeds-owned-capacity");
    inst->count = 3U;
    owner.fragment_kind = MINIAS_FRAGMENT_BYTES;
    if (minias_project_instruction(&owner,&view,&encoded,operands,4U))
        return fail("non-instruction-variant");
    owner.fragment_kind = MINIAS_FRAGMENT_INSTRUCTION;
    minias_mcinst_destroy(inst);

    inst = minias_mcinst_alloc(0U);
    if (!inst) return fail("zero-allocation");
    owner.payload.instruction.inst = inst;
    if (!minias_project_instruction(&owner,&view,&encoded,NULL,0U) ||
        encoded.count != 0U)
        return fail("zero-operand-projection");
    minias_mcinst_destroy(inst);
    puts("MINIAS_MCINST_READONLY_VIEW=PASS fields=typed phase=stack "
         "nested=borrowed count=checked refresh=live zero=valid");
    return 0;
}
