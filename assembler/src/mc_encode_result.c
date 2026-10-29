#include "minias_internal.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The encoder's temporary result is an MC fragment proposal.  It does not
 * own a final ELF symbol index, Section cursor or global Fixup array slot.
 * Target failures discard the entire proposal before Common has modified the
 * object. A Common commit failure aborts assembly without writing an ELF.
 */
static MiniAsEncodeResult *active_result(MiniAs *as, const char *operation) {
    if (as == NULL || !as->emitting_fragment ||
        !as->capture_instruction_bytes || as->active_encode_result == NULL ||
        as->emitting_fragment_id >= as->stmt_count) {
        if (as != NULL)
            minias_set_error(as, "encode-result-without-fragment:%s",
                             operation);
        return NULL;
    }
    return as->active_encode_result;
}

/* A SymbolRef is a pure parser-name token. This staging path neither looks
 * up nor creates an Object SymbolID; only a successful Common fragment
 * commit can give the reference an ELF symbol-table identity. */
bool minias_encode_add_fixup_ref(MiniAs *as,int section,uint64_t offset,
                                 MiniAsFixupKind kind,unsigned width,
                                 MiniAsSymbolRef ref,
                                 bool prefer_section_symbol) {
    MiniAsEncodeResult *result=active_result(as,"fixup-ref");
    MiniAsEncodeFixupRequest *request;
    const MiniAsStmt *owner;
    uint64_t relative;
    if(result==NULL)return false;
    owner=&as->stmts[as->emitting_fragment_id];
    if(owner->kind!=MINIAS_STMT_INSN ||
       ref.name_id>=as->symbol_name_count ||
       as->symbol_names[ref.name_id].name==NULL ||
       section!=owner->section || section<0 ||
       (size_t)section>=as->section_count ||
       as->sections[(size_t)section].type==MINIAS_SHT_NOBITS ||
       offset<owner->offset ||
       (width!=2U && width!=4U && width!=8U)) {
        minias_set_error(as,"invalid-encoded-fixup-ref:line=%zu",owner->line);
        return false;
    }
    relative=offset-owner->offset;
    if(relative>UINT32_MAX || relative>owner->size ||
       width>owner->size-relative) {
        minias_set_error(as,"encoded-fixup-ref-outside-fragment:line=%zu",
                         owner->line);
        return false;
    }
    if(result->fixup_count==SIZE_MAX ||
       !minias_grow_array((void **)&result->fixups,
                          &result->fixup_capacity,
                          sizeof(*result->fixups),result->fixup_count+1U)) {
        minias_set_error(as,"out-of-memory:encode-fixup-ref");
        return false;
    }
    request=&result->fixups[result->fixup_count++];
    memset(request,0,sizeof(*request));
    request->section=section;
    request->fragment_offset=(uint32_t)relative;
    request->kind=kind;
    request->width=width;
    request->symbol_ref=ref;
    request->prefer_section_symbol=prefer_section_symbol;
    return true;
}

/* A pending anchor exists only as a pure NameId and an EncodeResult-local
 * definition request.  Common creates its Object Symbol only at commit,
 * before any associated fixup is recorded.  This preserves old GNU/ELF
 * symbol encounter order without duplicating an owned name string. */
bool minias_encode_request_anchor_ref(MiniAs *as,const MiniAsStmt *stmt,
                                      const char *prefix,
                                      MiniAsSymbolRef *ref) {
    MiniAsEncodeResult *result=active_result(as,"anchor-ref");
    MiniAsEncodeAnchorRequest *request;
    char name[96];
    size_t ordinal;
    MiniAsSymbolNameId name_id;
    if(result==NULL)return false;
    if(ref==NULL || stmt==NULL ||
       stmt!=&as->stmts[as->emitting_fragment_id] ||
       stmt->kind!=MINIAS_STMT_INSN ||
       stmt->section<0 || (size_t)stmt->section>=as->section_count ||
       prefix==NULL || *prefix=='\0' ||
       result->anchor_count>=SIZE_MAX-as->synthetic_anchor_counter) {
        minias_set_error(as,"invalid-encoded-anchor-request");
        return false;
    }
    ordinal=as->synthetic_anchor_counter+result->anchor_count+1U;
    if(!minias_make_private_symbol_name(name,sizeof(name),prefix,ordinal)) {
        minias_set_error(as,"encoded-anchor-name-overflow");
        return false;
    }
    if(minias_get_symbol(as,name,false)!=NULL) {
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
    request->section=stmt->section;
    request->subsection=stmt->subsection;
    request->offset=stmt->offset;
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
    free(result->fixups);
    free(result->anchors);
    memset(result, 0, sizeof(*result));
}
