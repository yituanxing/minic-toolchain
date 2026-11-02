#include "minias_object_model.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct Fixture {
    const MiniAsSymbol *symbols;
    unsigned char data[16];
    unsigned char metadata[4];
    bool fragment_valid;
    uint64_t fragment_offset;
} Fixture;

static const MiniAsSymbol *find_symbol(const void *reader,const char *name) {
    const Fixture *fixture=reader;
    size_t i;
    for(i=0U;i<2U;++i)
        if(strcmp(fixture->symbols[i].name,name)==0)
            return &fixture->symbols[i];
    return NULL;
}

static bool numeric_reference(const char *text) {
    return strcmp(text,"0")==0;
}

static bool fragment_location(const void *reader,size_t index,
                              MiniAsObjectFragmentLocation *out) {
    const Fixture *fixture=reader;
    if(index!=7U || !fixture->fragment_valid)return false;
    *out=(MiniAsObjectFragmentLocation){
        .section=0,.offset=fixture->fragment_offset,.size=8U,.line=23U
    };
    return true;
}

static bool section_bytes(const void *reader,int section,
                          const unsigned char **data,size_t *size) {
    const Fixture *fixture=reader;
    if(section==0) {
        *data=fixture->data;
        *size=sizeof(fixture->data);
    }else if(section==1) {
        *data=fixture->metadata;
        *size=sizeof(fixture->metadata);
    }else return false;
    return true;
}

static bool plan_failed(MiniAsObjectModelView *view,const char *diagnostic) {
    MiniAsObjectPlan plan={0};
    char error[MINIAS_MAX_ERROR]={0};
    bool result=minias_object_plan(view,&plan,error,sizeof(error));
    assert(!result && strstr(error,diagnostic)!=NULL);
    assert(plan.sections==NULL && plan.fixups==NULL);
    return true;
}

int main(void) {
    MiniAsSection sections[2]={
        {.name=".text",.align=4U},
        {.name=".meta",.link_order_reference="anchor",
         .group_signature="sig",.align=1U}
    };
    MiniAsSymbol symbols[2]={
        {.name="anchor",.defined=true,.section=0},
        {.name="sig",.defined=false,.section=MINIAS_SECTION_UNDEF}
    };
    MiniAsFixup fixups[1]={
        {.origin=MINIAS_FIXUP_ORIGIN_FRAGMENT,.section=0,
         .fragment_id=7U,.fragment_offset=2U,.width=4U,
         .symbol_index=0U}
    };
    Fixture fixture={.symbols=symbols,.fragment_valid=true,
                     .fragment_offset=4U};
    MiniAsObjectModelView view={
        .sections=sections,.section_count=2U,
        .symbols=symbols,.symbol_count=2U,
        .fixups=fixups,.fixup_count=1U,
        .reader=&fixture,.find_symbol=find_symbol,
        .numeric_reference=numeric_reference,
        .fragment_location=fragment_location,.section_bytes=section_bytes
    };
    MiniAsObjectPlan plan={0};
    char error[MINIAS_MAX_ERROR]={0};

    assert(minias_object_plan(&view,&plan,error,sizeof(error)));
    assert(error[0]=='\0' && plan.sections[0].bytes==fixture.data);
    assert(plan.sections[0].size==sizeof(fixture.data));
    assert(plan.sections[0].signature_symbol==SIZE_MAX);
    assert(plan.sections[1].signature_symbol==1U);
    assert(plan.sections[1].linked_section==0U);
    assert(plan.fixups[0].final_offset==6U);
    minias_object_dispose_plan(&plan);
    assert(plan.sections==NULL && plan.fixups==NULL);

    sections[1].link_order_reference=".text";
    assert(minias_object_plan(&view,&plan,error,sizeof(error)));
    assert(plan.sections[1].linked_section==0U);
    minias_object_dispose_plan(&plan);
    sections[1].link_order_reference="0";
    assert(minias_object_plan(&view,&plan,error,sizeof(error)));
    assert(plan.sections[1].linked_section==SIZE_MAX);
    minias_object_dispose_plan(&plan);

    sections[1].link_order_reference="not_defined";
    assert(plan_failed(&view,"undefined-link-order-reference"));
    sections[1].link_order_reference="anchor";
    sections[1].group_signature="missing_sig";
    assert(plan_failed(&view,"missing-group-signature"));
    sections[1].group_signature="sig";
    fixture.fragment_valid=false;
    assert(plan_failed(&view,"invalid-fixup-fragment-id"));
    fixture.fragment_valid=true;
    fixture.fragment_offset=15U;
    assert(plan_failed(&view,"fixup-field-out-of-bounds"));
    fixture.fragment_offset=4U;
    fixups[0].fragment_offset=6U;
    assert(plan_failed(&view,"invalid-fixup-fragment-layout"));
    fixups[0].fragment_offset=2U;
    fixups[0].symbol_index=3U;
    assert(plan_failed(&view,"internal:bad-relocation-symbol"));
    fixups[0].symbol_index=0U;
    fixups[0].origin=(MiniAsFixupOrigin)255;
    assert(plan_failed(&view,"invalid-fixup-origin"));

    puts("MINIAS_OBJECT_MODEL_PLAN=PASS logical_group=1 link_order=3 "
         "fragment_fixup=1 byte_borrows=1 failures=7 cleanup=7");
    return 0;
}
