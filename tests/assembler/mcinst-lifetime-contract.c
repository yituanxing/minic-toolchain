#include "minias_internal.h"

#include <stdio.h>
#include <stdlib.h>

static unsigned released;
static void release_target_payload(void *p) {
    ++released;
    free(p);
}
static int fail(const char *what) {
    fprintf(stderr,"MINIAS_MCINST_LIFETIME=FAIL %s\n",what);
    return 1;
}
int main(void) {
    MiniAsMCInst *m=minias_mcinst_alloc(4U);
    MiniAsMCInst *empty;
    if(!m || m->capacity!=4U)return fail("capacity");
    /* Parser failed with count=1, but constructed heap operands in
     * slots 2 and 3: a destructor must walk capacity, never count. */
    m->count=1U;
    m->operands[2].kind=MINIAS_MC_BOUND_SYMBOL;
    m->operands[2].bound_symbol=malloc(sizeof(MiniAsSymbolRef));
    m->operands[3].kind=MINIAS_MC_MEMORY;
    m->operands[3].memory=malloc(40U);
    m->dispose_memory=release_target_payload;
    m->target_payload=malloc(20U);
    m->dispose_target_payload=release_target_payload;
    if(!m->operands[2].bound_symbol || !m->operands[3].memory ||
       !m->target_payload)return fail("allocation");
    minias_mcinst_destroy(m);
    if(released!=2U)return fail("callback");

    /* A zero-operand instruction has a real zero capacity, not a
     * special legacy count fallback. */
    empty=minias_mcinst_alloc(0U);
    if(!empty || empty->capacity!=0U)return fail("zero-operand");
    minias_mcinst_destroy(empty);
    puts("MINIAS_MCINST_LIFETIME=PASS capacity=sole_owner partial_parser=3 destructor_callbacks=2 zero_operand=1");
    return 0;
}
