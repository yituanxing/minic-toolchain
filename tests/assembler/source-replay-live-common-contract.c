#include "minias_common_lowering.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* Link the real Common reader with unused lowering code discarded at link
 * time. This stub proves that planning is delegated to MCLayout's existing
 * TargetRelaxPlan query, not reconstructed in the Source replay boundary. */
static unsigned relax_queries;
bool minias_query_target_relax_plan(MiniAs *as,const MiniAsStmt *stmt,
                                   MiniAsTargetRelaxPlan *plan) {
    assert(stmt==&as->stmts[1]);
    ++relax_queries;
    *plan=(MiniAsTargetRelaxPlan){
        .current_size=stmt->size,
        .required_size=stmt->size+2U,
        .terminal_size=stmt->size+4U
    };
    return true;
}

int main(void) {
    MiniAs as={0};
    MiniAsStmt fragments[3]={{0}};
    MiniAsMCInst *inst=calloc(1U,sizeof(*inst)+3U*sizeof(inst->operands[0]));
    MiniAsSourceReplayReader reader=minias_common_source_replay_reader(&as);
    MiniAsSourceReplayReader empty=minias_common_source_replay_reader(NULL);
    MiniAsSourceReplayFragment snapshot;
    MiniAsForcedOperandShape operand;
    uint32_t required_size=0U,terminal_size=0U;

    assert(inst!=NULL);
    assert(empty.owner==NULL && empty.fragment_count==0U);
    assert(reader.owner==&as && reader.fragment_count==0U);
    as.stmts=fragments;
    as.stmt_count=3U;
    reader=minias_common_source_replay_reader(&as);
    assert(reader.fragment_count==3U);
    assert(!reader.fragment(reader.owner,3U,&snapshot));
    assert(!reader.fragment(reader.owner,0U,NULL));
    assert(!reader.operand(reader.owner,0U,0U,&operand));
    assert(!reader.growth_plan(reader.owner,3U,&required_size,&terminal_size));
    assert(relax_queries==0U);

    /* A non-instruction variant contains a deliberately invalid pointer in
     * another union arm. Reading the instruction pointer would be unsafe. */
    fragments[0].fragment_kind=MINIAS_FRAGMENT_FIXED_DATA;
    fragments[0].source_site=1U;
    fragments[0].op=".word";
    fragments[0].line=7U;
    fragments[0].payload.fixed_data.data=(MiniAsMCData *)(uintptr_t)1U;
    assert(reader.fragment(reader.owner,0U,&snapshot));
    assert(!snapshot.instruction && snapshot.source_site==1U);
    assert(snapshot.op==fragments[0].op && snapshot.line==7U);
    assert(!reader.operand(reader.owner,0U,0U,&operand));

    fragments[1].fragment_kind=MINIAS_FRAGMENT_INSTRUCTION;
    fragments[1].source_site=11U;
    fragments[1].op="beq";
    fragments[1].line=29U;
    fragments[1].size=4U;
    fragments[1].site.mode_bits=0x1234U;
    fragments[1].payload.instruction.inst=inst;
    inst->opcode=37U;
    inst->target_form=42U;
    inst->capacity=3U;
    inst->count=2U;
    inst->operands[0].kind=MINIAS_MC_REGISTER;
    inst->operands[0].reg_class=MINIAS_MC_REG_FPR;
    inst->operands[0].reg=17U;
    inst->operands[0].target_reg_bank=0x20U;
    inst->operands[0].register_64=true;
    inst->operands[1].kind=MINIAS_MC_BOUND_SYMBOL;
    inst->operands[1].reg_class=MINIAS_MC_REG_GPR;
    inst->operands[1].reg=3U;
    assert(reader.fragment(reader.owner,1U,&snapshot));
    assert(snapshot.instruction && snapshot.source_site==11U);
    assert(snapshot.op==fragments[1].op && snapshot.line==29U);
    assert(snapshot.size==4U && snapshot.mode_bits==0x1234U);
    assert(snapshot.opcode==37U && snapshot.target_form==42U);
    assert(snapshot.operand_count==2U);
    assert(reader.operand(reader.owner,1U,0U,&operand));
    assert(operand.kind==MINIAS_MC_REGISTER);
    assert(operand.reg_class==MINIAS_MC_REG_FPR);
    assert(operand.reg==17U && operand.target_reg_bank==0x20U);
    assert(operand.register_64);
    assert(!reader.operand(reader.owner,1U,2U,&operand));
    assert(!reader.operand(reader.owner,3U,0U,&operand));
    assert(!reader.operand(reader.owner,1U,0U,NULL));

    /* Snapshots contain value copies: earlier projections do not track
     * the Common vector when a fresh parser changes the instruction. */
    inst->opcode=38U;
    inst->operands[0].reg=18U;
    assert(snapshot.opcode==37U && operand.reg==17U);
    assert(reader.fragment(reader.owner,1U,&snapshot));
    assert(reader.operand(reader.owner,1U,0U,&operand));
    assert(snapshot.opcode==38U && operand.reg==18U);

    assert(reader.growth_plan(reader.owner,1U,&required_size,&terminal_size));
    assert(relax_queries==1U && required_size==6U && terminal_size==8U);

    /* Corrupt flexible-array length is rejected before operand reads. */
    inst->count=4U;
    assert(!reader.fragment(reader.owner,1U,&snapshot));
    assert(!reader.operand(reader.owner,1U,0U,&operand));
    inst->count=0U;
    assert(reader.fragment(reader.owner,1U,&snapshot));
    assert(snapshot.instruction && snapshot.operand_count==0U);
    assert(!reader.operand(reader.owner,1U,0U,&operand));

    /* A pass-local read capability captures its original count. The next
     * replay must request a fresh reader for a changed Common vector. */
    as.stmt_count=2U;
    assert(reader.fragment_count==3U);
    reader=minias_common_source_replay_reader(&as);
    assert(reader.fragment_count==2U);

    free(inst);
    puts("MINIAS_SOURCE_REPLAY_COMMON=PASS real_projector=1 "
         "variant=guarded bounds=checked snapshot=value "
         "reader=refreshed relax_plan=delegated zero_operand=valid");
    return 0;
}
