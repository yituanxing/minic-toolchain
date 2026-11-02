#include "minias_common_emission_private.h"
#include "minias_common_context.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The encoder's temporary result is an MC fragment proposal.  It does not
 * own a final ELF symbol index, Section cursor or global Fixup array slot.
 * Target failures discard the entire proposal before Common has modified the
 * object. A Common commit failure aborts assembly without writing an ELF.
 */
static MiniAsEncodeResult *active_result(MiniAs *as, const char *operation) {
    if (as == NULL || as->emission==NULL || !as->emission->emitting_fragment ||
        as->emission->active_encode_result == NULL ||
        as->emission->emitting_fragment_id >= as->stmt_count) {
        if (as != NULL)
            minias_set_error(as, "encode-result-without-fragment:%s",
                             operation);
        return NULL;
    }
    return as->emission->active_encode_result;
}

/* The sole EncodeResult Fixup vector owner. MCData, ordinary target SymbolRef
 * and ELF STN_UNDEF=0 marker requests have distinct legality rules, but all
 * of them stage one value and share the same allocation/failure contract. */
static bool minias_encode_result_stage_fixup(MiniAs *as,
                                      const MiniAsEncodeFixupRequest *request,
                                      const char *allocation_context) {
    MiniAsEncodeResult *result=active_result(as,"fixup-stage");
    if(result==NULL || request==NULL)return false;
    if(result->fixup_count==SIZE_MAX ||
       !minias_grow_array((void **)&result->fixups,
                          &result->fixup_capacity,
                          sizeof(*result->fixups),result->fixup_count+1U)) {
        minias_set_error(as,"out-of-memory:%s",allocation_context);
        return false;
    }
    result->fixups[result->fixup_count++]=*request;
    return true;
}

/* MCData staging is now owned by the sole EncodeResult request vector.
 * The generic append stays private; Common only invokes semantic MCData
 * entry points and retains final Object Fixup/relocation ownership. */
bool minias_fixup_field_width(MiniAsFixupKind kind,unsigned width) {
    if(kind==MINIAS_FIXUP_LEB_ADD || kind==MINIAS_FIXUP_LEB_SUB ||
       kind==MINIAS_FIXUP_LEB_SET)return width==0U;
    return width==1U || width==2U || width==3U ||
           width==4U || width==8U;
}

/* MCData fixups used to bypass the instruction transaction and publish
 * Section-owned symbol references as the DATA loop ran. Stage them under the
 * same persistent source Fragment, keeping the original SymbolID or a
 * stable NameId; the commit phase alone creates final Fixup records. */
static bool minias_stage_common_fixup(MiniAs *as,uint64_t offset,
                                      MiniAsFixupKind kind,unsigned width,
                                      const char *name,size_t symbol_index,
                                      int64_t addend) {
    MiniAsEncodeResult *result=as->emission->active_encode_result;
    const MiniAsStmt *owner;
    MiniAsEncodeFixupRequest request={0};
    MiniAsSymbolNameId name_id=0U;
    uint64_t relative;
    int section;
    if(result==NULL || !as->emission->emitting_fragment ||
       as->emission->emitting_fragment_id>=as->stmt_count) {
        minias_set_error(as,"mcdata-fixup-without-fragment");
        return false;
    }
    owner=&as->stmts[as->emission->emitting_fragment_id];
    section=owner->section; /* MCData cannot select another Section. */
    if(section<0 ||
       (size_t)section>=as->section_count ||
       as->sections[(size_t)section].type==MINIAS_SECTION_KIND_NOBITS ||
       offset<owner->offset || !minias_fixup_field_width(kind,width) ||
       (name==NULL && symbol_index>=minias_common_context_symbol_count(as))) {
        minias_set_error(as,"invalid-mcdata-fixup:line=%zu",owner->line);
        return false;
    }
    relative=offset-owner->offset;
    if(relative>UINT32_MAX || relative>owner->size ||
       width>owner->size-relative ||
       result->fixup_count==SIZE_MAX) {
        minias_set_error(as,"mcdata-fixup-outside-fragment:line=%zu",owner->line);
        return false;
    }
    if(name!=NULL && !minias_symbol_name_intern(as,name,&name_id))
        return false;
    request.fragment_offset=(uint32_t)relative;
    request.kind=kind;
    request.width=width;
    request.has_symbol_index=name==NULL;
    request.symbol_index=symbol_index;
    request.symbol_ref.name_id=name_id;
    request.symbol_ref.addend=addend;
    return minias_encode_result_stage_fixup(as,&request,"mcdata-fixup");
}

bool minias_add_fixup(MiniAs *as, uint64_t offset,
                      MiniAsFixupKind kind, unsigned width,
                      const char *symbol_name, int64_t addend) {
    return minias_stage_common_fixup(as,offset,kind,width,
                                     symbol_name,SIZE_MAX,addend);
}

/* Fixed-width MCData already retains an actual SymbolID from its lexical
 * parse. Preserve that identity through emission, including the shared
 * undefined-local binding promotion and original Fixup append order. */
bool minias_add_fixup_symbol_id(MiniAs *as, uint64_t offset,
                                MiniAsFixupKind kind, unsigned width,
                                size_t symbol_index, int64_t addend) {
    return minias_stage_common_fixup(as,offset,kind,width,
                                     NULL,symbol_index,addend);
}

/* Target SymbolRefs and STN_UNDEF=0 markers differ only in their feature
 * legality and GNU diagnostic spelling. Centralize the shared instruction
 * fragment/field checks, keeping the two public semantic entry points. */
static bool stage_instruction_fixup(MiniAs *as,uint64_t offset,
                                    MiniAsFixupKind kind,unsigned width,
                                    MiniAsSymbolRef ref,
                                    bool prefer_section_symbol,
                                    bool absolute_zero_symbol) {
    MiniAsEncodeResult *result=active_result(
        as,absolute_zero_symbol?"fixup-abs0":"fixup-ref");
    MiniAsEncodeFixupRequest request={0};
    const MiniAsStmt *owner;
    int section;
    uint64_t relative;
    if(result==NULL)return false;
    owner=&as->stmts[as->emission->emitting_fragment_id];
    section=owner->section; /* Target cannot select another Section. */
    if(owner->fragment_kind!=MINIAS_FRAGMENT_INSTRUCTION ||
       (!absolute_zero_symbol &&
        (ref.name_id>=as->symbol_name_count ||
         as->symbol_names[ref.name_id].name==NULL)) ||
       section<0 ||
       (size_t)section>=as->section_count ||
       as->sections[(size_t)section].type==MINIAS_SECTION_KIND_NOBITS ||
       offset<owner->offset ||
       (absolute_zero_symbol ? width!=4U :
        (width!=2U && width!=4U && width!=8U))) {
        minias_set_error(as,absolute_zero_symbol?
                         "invalid-encoded-abs0-fixup:line=%zu":
                         "invalid-encoded-fixup-ref:line=%zu",owner->line);
        return false;
    }
    relative=offset-owner->offset;
    if(relative>UINT32_MAX || relative>owner->size ||
       width>owner->size-relative) {
        minias_set_error(as,absolute_zero_symbol?
                         "encoded-abs0-outside-fragment:line=%zu":
                         "encoded-fixup-ref-outside-fragment:line=%zu",
                         owner->line);
        return false;
    }
    request.fragment_offset=(uint32_t)relative;
    request.kind=kind;
    request.width=width;
    request.symbol_ref=ref;
    request.prefer_section_symbol=prefer_section_symbol;
    request.absolute_zero_symbol=absolute_zero_symbol;
    return minias_encode_result_stage_fixup(
        as,&request,absolute_zero_symbol?"abs0-fixup":"encode-fixup-ref");
}

/* A SymbolRef is a pure parser-name token. This staging path neither looks
 * up nor creates an Object SymbolID; only a successful Common fragment
 * commit can give the reference an ELF symbol-table identity. */
bool minias_encode_add_fixup_ref(MiniAs *as,uint64_t offset,
                                 MiniAsFixupKind kind,unsigned width,
                                 MiniAsSymbolRef ref,
                                 bool prefer_section_symbol) {
    return stage_instruction_fixup(as,offset,kind,width,ref,
                                   prefer_section_symbol,false);
}

/* ELF STN_UNDEF=0 relaxation markers have no SymbolRef, but retain the same
 * fragment-owned atomic commit and byte bounds as normal target fixups. */
bool minias_encode_add_fixup_abs0(MiniAs *as,uint64_t offset,
                                  MiniAsFixupKind kind,unsigned width) {
    MiniAsSymbolRef absent={0};
    return stage_instruction_fixup(as,offset,kind,width,absent,
                                   false,true);
}

/* A pending anchor exists only as a pure NameId and an EncodeResult-local
 * definition request.  Common creates its Object Symbol only at commit,
 * before any associated fixup is recorded.  This preserves old GNU/ELF
 * symbol encounter order without duplicating an owned name string. */
bool minias_encode_request_anchor_ref(MiniAs *as,const char *prefix,
                                      MiniAsSymbolRef *ref) {
    MiniAsEncodeResult *result=active_result(as,"anchor-ref");
    MiniAsEncodeAnchorRequest *request;
    const MiniAsStmt *stmt;
    char name[96];
    size_t ordinal;
    MiniAsSymbolNameId name_id;
    if(result==NULL)return false;
    stmt=&as->stmts[as->emission->emitting_fragment_id];
    if(ref==NULL ||
       stmt->fragment_kind!=MINIAS_FRAGMENT_INSTRUCTION ||
       stmt->section<0 || (size_t)stmt->section>=as->section_count ||
       prefix==NULL || *prefix=='\0' ||
       result->anchor_count>=SIZE_MAX-as->emission->synthetic_anchor_counter) {
        minias_set_error(as,"invalid-encoded-anchor-request");
        return false;
    }
    ordinal=as->emission->synthetic_anchor_counter+result->anchor_count+1U;
    if(!minias_make_private_symbol_name(name,sizeof(name),prefix,ordinal)) {
        minias_set_error(as,"encoded-anchor-name-overflow");
        return false;
    }
    MiniAsSymbolView existing;
    if(minias_common_context_find_symbol_view(as,name,&existing)) {
        minias_set_error(as,"duplicate-encoded-anchor");
        return false;
    }
    if(!minias_symbol_name_intern(as,name,&name_id))return false;
    if(result->anchor_count==SIZE_MAX ||
       !minias_grow_array((void **)&result->anchors,
                          &result->anchor_capacity,
                          sizeof(*result->anchors),
                          result->anchor_count+1U)) {
        minias_set_error(as,"out-of-memory:encode-anchor");
        return false;
    }
    request=&result->anchors[result->anchor_count++];
    request->name_id=name_id;
    ref->name_id=name_id;
    ref->addend=0;
    return true;
}

bool minias_encode_add_elf_flags(MiniAs *as, uint32_t flags) {
    MiniAsEncodeResult *result = active_result(as, "elf-flags");
    if (result == NULL) return false;
    result->elf_flags_or |= flags;
    return true;
}

void minias_encode_result_destroy(MiniAsEncodeResult *result) {
    if (result == NULL) return;
    free(result->bytes);
    free(result->writes);
    free(result->fixups);
    free(result->anchors);
    memset(result, 0, sizeof(*result));
}
