/* Test the SourceGraph replay contract with an ephemeral scalar reader.
 * No Common Fragment, mutable MCInst or Target relaxation implementation is
 * linked; unused physical-source functions are linker-GC'd by the probe. */
#include "source_graph_internal.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct MiniAs {
    MiniAsSourceContext *source;
    char error[256];
};

MiniAsSourceContext *minias_source_session(MiniAs *as) { return as->source; }

void minias_set_error(MiniAs *as,const char *format,...) {
    va_list args;
    if(as->error[0]!='\0')return;
    va_start(args,format);
    (void)vsnprintf(as->error,sizeof(as->error),format,args);
    va_end(args);
}

bool minias_grow_array(void **ptr,size_t *capacity,size_t size,size_t need) {
    size_t next=*capacity==0U?4U:*capacity;
    void *data;
    if(need<=*capacity)return true;
    while(next<need) {
        if(next>SIZE_MAX/2U)return false;
        next*=2U;
    }
    if(size!=0U && next>SIZE_MAX/size)return false;
    data=realloc(*ptr,next*size);
    if(data==NULL)return false;
    *ptr=data;
    *capacity=next;
    return true;
}

char *minias_strdup(const char *text) {
    size_t size=strlen(text)+1U;
    char *copy=malloc(size);
    if(copy!=NULL)memcpy(copy,text,size);
    return copy;
}

typedef struct ReplayFixture {
    MiniAsSourceReplayFragment fragments[3];
    MiniAsForcedOperandShape operands[3][2];
    size_t count;
    bool fail_growth_plan;
    bool fail_operand;
} ReplayFixture;

static bool fragment_read(void *owner,size_t ordinal,
                          MiniAsSourceReplayFragment *out) {
    const ReplayFixture *fixture=owner;
    if(ordinal>=fixture->count)return false;
    *out=fixture->fragments[ordinal];
    return true;
}

static bool operand_read(void *owner,size_t ordinal,unsigned operand_index,
                         MiniAsForcedOperandShape *out) {
    const ReplayFixture *fixture=owner;
    if(fixture->fail_operand || ordinal>=fixture->count ||
       operand_index>=fixture->fragments[ordinal].operand_count ||
       operand_index>=2U)return false;
    *out=fixture->operands[ordinal][operand_index];
    return true;
}

static bool growth_plan_read(void *owner,size_t ordinal,
                             uint32_t *required_size,uint32_t *terminal_size) {
    const ReplayFixture *fixture=owner;
    if(fixture->fail_growth_plan || ordinal>=fixture->count ||
       !fixture->fragments[ordinal].instruction)return false;
    *required_size=5U;
    *terminal_size=6U;
    return true;
}

static MiniAsSourceReplayReader reader_for(ReplayFixture *fixture) {
    return (MiniAsSourceReplayReader){
        .owner=fixture,
        .fragment_count=fixture->count,
        .fragment=fragment_read,
        .operand=operand_read,
        .growth_plan=growth_plan_read
    };
}

static void expect(bool condition,const char *detail) {
    if(!condition) {
        fprintf(stderr,"MINIAS_SOURCE_REPLAY_READER=FAIL %s\n",detail);
        exit(EXIT_FAILURE);
    }
}

int main(void) {
    MiniAsSourceContext context={0};
    MiniAs as={.source=&context};
    ReplayFixture fixture={0};
    MiniAsFragmentGrowthSet growth={0};
    MiniAsSourceReplayReader reader;
    MiniAsSourceReplayFragment branch={0};
    MiniAsForcedOperandShape register_shape={0};
    size_t minimum_size=0U;
    char op[8]="je";

    context.graph=minias_source_graph_create();
    expect(context.graph!=NULL,"graph allocation");
    context.graph->sites=calloc(2U,sizeof(*context.graph->sites));
    expect(context.graph->sites!=NULL,"sites allocation");
    context.graph->site_count=2U;
    context.graph->site_capacity=2U;

    branch=(MiniAsSourceReplayFragment){
        .source_site=1U,.op=op,.line=17U,.size=2U,
        .instruction=true,.opcode=91U,.target_form=11U,
        .mode_bits=32U,.operand_count=1U
    };
    register_shape=(MiniAsForcedOperandShape){
        .kind=MINIAS_MC_REGISTER,.reg_class=MINIAS_MC_REG_GPR,
        .target_reg_bank=1U,.reg=8U,.register_64=true
    };
    fixture.fragments[0]=branch;
    fixture.operands[0][0]=register_shape;
    fixture.count=1U;
    reader=reader_for(&fixture);
    expect(minias_source_graph_validate_fragment_sites(&as,&reader),
           "initial sites valid");

    growth.ids=malloc(sizeof(*growth.ids));
    expect(growth.ids!=NULL,"growth allocation");
    growth.ids[0]=0U;growth.count=1U;growth.capacity=1U;
    fixture.fail_growth_plan=true;
    expect(!minias_source_graph_capture_forced_branches(&as,&reader,&growth),
           "growth plan failure must reject capture");
    expect(strstr(as.error,"invalid-forced-branch-growth-plan:line=17")!=NULL,
           "silent growth callback must receive a diagnostic");
    expect(context.graph->forced_branch_count==0U &&
           context.graph->sites[0].forced_branch_index==0U,
           "failed growth plan must not retain a branch");
    fixture.fail_growth_plan=false;
    as.error[0]='\0';
    expect(minias_source_graph_capture_forced_branches(&as,&reader,&growth),
           "capture typed branch shape");
    expect(context.graph->forced_branch_count==1U &&
           context.graph->forced_branches[0].minimum_size==6U &&
           context.graph->sites[0].forced_branch_index==1U,
           "terminal size and stable source identity retained");
    expect(context.graph->forced_branches[0].op!=op,
           "borrowed op copied only into stable forced-shape record");

    fixture.fragments[0]=(MiniAsSourceReplayFragment){.source_site=2U};
    fixture.fragments[1]=branch;
    fixture.operands[1][0]=register_shape;
    fixture.count=2U;
    reader=reader_for(&fixture);
    expect(minias_source_graph_validate_fragment_sites(&as,&reader),
           "inserted fragment has stable site");
    expect(minias_source_graph_rebind_forced_branches(&as,&reader,&growth) &&
           growth.count==1U && growth.ids[0]==1U,
           "branch rebinds by SiteID after ordinal insertion");
    expect(minias_source_graph_lookup_forced_branch(&as,&reader,1U,&minimum_size)
           && minimum_size==6U,"forced terminal size survives rebind");

    fixture.fragments[1].opcode++;
    expect(!minias_source_graph_lookup_forced_branch(&as,&reader,1U,&minimum_size)
           && strstr(as.error,"changed-forced-branch-shape:")!=NULL,
           "changed opcode must fail closed");
    fixture.fragments[1].opcode--;
    fixture.operands[1][0].reg++;
    expect(!minias_source_graph_lookup_forced_branch(&as,&reader,1U,&minimum_size),
           "changed typed operand register must fail closed");
    fixture.operands[1][0].reg--;
    fixture.fail_operand=true;
    expect(!minias_source_graph_lookup_forced_branch(&as,&reader,1U,&minimum_size),
           "failed operand projection must fail closed");
    fixture.fail_operand=false;
    (void)strcpy(op,"jne");
    expect(!minias_source_graph_lookup_forced_branch(&as,&reader,1U,&minimum_size),
           "changed borrowed mnemonic must fail closed");
    (void)strcpy(op,"je");

    fixture.fail_growth_plan=true;
    (void)strcpy(as.error,"target-owned-growth-error");
    expect(!minias_source_graph_capture_forced_branches(&as,&reader,&growth) &&
           context.graph->forced_branches[0].minimum_size==6U &&
           strcmp(as.error,"target-owned-growth-error")==0,
           "failed growth plan preserves Target diagnostic and prior bound");
    fixture.fail_growth_plan=false;
    as.error[0]='\0';

    fixture.count=1U;
    reader=reader_for(&fixture);
    expect(!minias_source_graph_rebind_forced_branches(&as,&reader,&growth) &&
           strstr(as.error,"lost-or-ambiguous-forced-source-site:")!=NULL,
           "vanished branch cannot rebind by stale ordinal");

    fixture.count=3U;
    fixture.fragments[2]=branch;
    fixture.operands[2][0]=register_shape;
    reader=reader_for(&fixture);
    expect(!minias_source_graph_rebind_forced_branches(&as,&reader,&growth),
           "duplicate instructions with one SourceSite rejected");

    free(growth.ids);
    minias_source_graph_destroy(context.graph);
    puts("MINIAS_SOURCE_REPLAY_READER=PASS ordinal_insertion=1 typed_shape_drift=3 callback_failure=2 vanished=1 ambiguous=1 terminal_growth=6");
    return EXIT_SUCCESS;
}
