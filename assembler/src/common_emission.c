#include "minias_common_emission_private.h"

#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

bool minias_common_emission_init(MiniAs *as) {
    if(as==NULL)return false;
    if(as->emission!=NULL) {
        minias_set_error(as,"emission-state-already-initialized");
        return false;
    }
    as->emission=calloc(1U,sizeof(*as->emission));
    if(as->emission==NULL) {
        minias_set_error(as,"out-of-memory:emission-state");
        return false;
    }
    return true;
}

void minias_common_emission_destroy(MiniAs *as) {
    MiniAsEmissionState *state;
    if(as==NULL || as->emission==NULL)return;
    state=as->emission;
    free(state->fixups);
    if(state->active_encode_result!=NULL) {
        minias_encode_result_destroy(state->active_encode_result);
        free(state->active_encode_result);
    }
    free(state);
    as->emission=NULL;
}

MiniAsFixupSpan minias_emission_fixups(const MiniAs *as) {
    const MiniAsEmissionState *state=as!=NULL ? as->emission : NULL;
    const MiniAsFixupSpan empty={NULL,0U};
    if(state==NULL)return empty;
    return (MiniAsFixupSpan){state->fixups,state->fixup_count};
}

bool minias_emission_mcdata_cursor(MiniAs *as,const MiniAsStmt *stmt,
                                  uint64_t *cursor) {
    const MiniAsEmissionState *state=as!=NULL ? as->emission : NULL;
    if(state==NULL || stmt==NULL || cursor==NULL ||
       !state->emitting_fragment || state->active_encode_result==NULL ||
       state->emitting_fragment_id>=as->stmt_count ||
       &as->stmts[state->emitting_fragment_id]!=stmt ||
       stmt->offset>UINT64_MAX-state->active_encode_result->byte_count) {
        if(as!=NULL)minias_set_error(as,"mcdata-emission-without-fragment");
        return false;
    }
    *cursor=stmt->offset+state->active_encode_result->byte_count;
    return true;
}

/* Keep staged MCData Fixup widths identical to the committed Section
 * contract. LoongArch uses 8-bit and 24-bit ADD/SUB pairs as well as
 * 16/32/64-bit relocations; staging must not reject a legal typed Fixup. */
static bool minias_record_fixup(MiniAs *as,
                      int section,
                      uint64_t offset,
                      MiniAsFixupKind kind,
                      unsigned width,
                      const char *symbol_name,
                      size_t existing_symbol_index,
                      int64_t addend,
                      MiniAsFixupOrigin origin,bool absolute_zero_symbol,
                      bool prefer_section_symbol,
                      size_t retained_fragment_id) {
    MiniAsSymbol *symbol=NULL;
    MiniAsFixup *fixup;
    size_t symbol_index;
    uint32_t relative_offset = 0U;
    size_t fragment_id = SIZE_MAX;
    if(as==NULL || as->emission==NULL) {
        if(as!=NULL)minias_set_error(as,"fixup-without-emission-state");
        return false;
    }
    if (origin == MINIAS_FIXUP_ORIGIN_FRAGMENT) {
        const MiniAsStmt *owner;
        uint64_t delta;
        if ((as->emission->emitting_fragment && retained_fragment_id!=SIZE_MAX) ||
            (!as->emission->emitting_fragment && retained_fragment_id==SIZE_MAX)) {
            minias_set_error(as, "fixup-without-fragment-owner");
            return false;
        }
        fragment_id = retained_fragment_id!=SIZE_MAX ? retained_fragment_id :
                     as->emission->emitting_fragment_id;
        if(fragment_id>=as->stmt_count) {
            minias_set_error(as,"invalid-retained-fixup-fragment-id");
            return false;
        }
        owner = &as->stmts[fragment_id];
        if (owner->section != section || offset < owner->offset) {
            minias_set_error(as, "fixup-fragment-section-mismatch:line=%zu",
                             owner->line);
            return false;
        }
        delta = offset - owner->offset;
        if (delta > UINT32_MAX || delta > owner->size ||
            width > owner->size - delta) {
            minias_set_error(as, "fixup-outside-fragment:line=%zu",
                             owner->line);
            return false;
        }
        relative_offset = (uint32_t)delta;
    } else if ((origin != MINIAS_FIXUP_ORIGIN_EXPLICIT_SECTION &&
                origin != MINIAS_FIXUP_ORIGIN_SYNTHETIC_SECTION) ||
               as->emission->emitting_fragment) {
        minias_set_error(as, "invalid-section-fixup-owner");
        return false;
    }
    /* Keep semantic fixups typed until finalization. The object builder
     * must decide whether this field needs an ELF relocation at all. */
    if (section < 0 || (size_t)section >= as->section_count ||
        as->sections[(size_t)section].type==MINIAS_SHT_NOBITS ||
        !minias_fixup_field_width(kind,width) ||
        offset > UINT64_MAX - width) {
        minias_set_error(as, "invalid-fixup-range:%s",
                         symbol_name != NULL ? symbol_name : "<symbol-id>");
        return false;
    }
    if(absolute_zero_symbol) {
        if((origin!=MINIAS_FIXUP_ORIGIN_FRAGMENT &&
            origin!=MINIAS_FIXUP_ORIGIN_EXPLICIT_SECTION) ||
           symbol_name!=NULL ||
           existing_symbol_index!=SIZE_MAX || addend!=0) {
            minias_set_error(as,"invalid-abs0-fixup-owner");
            return false;
        }
        symbol_index=SIZE_MAX;
    } else if (existing_symbol_index != SIZE_MAX) {
        if (existing_symbol_index >= as->symbol_count) {
            minias_set_error(as, "invalid-fixup-symbol-id:%zu",
                             existing_symbol_index);
            return false;
        }
        symbol = &as->symbols[existing_symbol_index];
    } else {
        if (symbol_name == NULL) {
            minias_set_error(as, "missing-fixup-symbol");
            return false;
        }
        symbol = minias_get_symbol(as, symbol_name, true);
    }
    if (!absolute_zero_symbol && symbol == NULL) return false;
    if (!absolute_zero_symbol)symbol_index = (size_t)(symbol - as->symbols);
    if (!absolute_zero_symbol && !symbol->defined && symbol->bind == MINIAS_STB_LOCAL &&
        !symbol->explicit_local_binding &&
        strncmp(symbol->name, ".L", 2U) != 0)
        symbol->bind = MINIAS_STB_GLOBAL;

    if (!minias_grow_array((void **)&as->emission->fixups,
                    &as->emission->fixup_capacity,
                    sizeof(*as->emission->fixups),
                    as->emission->fixup_count + 1U)) {
        minias_set_error(as, "out-of-memory:fixup");
        return false;
    }
    fixup = &as->emission->fixups[as->emission->fixup_count++];
    fixup->section = section;
    fixup->origin = origin;
    fixup->fragment_id = fragment_id;
    fixup->fragment_offset = relative_offset;
    fixup->offset = origin == MINIAS_FIXUP_ORIGIN_FRAGMENT ? 0U : offset;
    fixup->kind = kind;
    fixup->width = width;
    fixup->symbol_index = symbol_index;
    fixup->addend = addend;
    fixup->prefer_section_symbol = prefer_section_symbol;
    return true;
}

bool minias_add_section_fixup(MiniAs *as, MiniAsFixupOrigin origin,
                             int section, uint64_t offset,
                             MiniAsFixupKind kind, unsigned width,
                             const char *symbol_name, int64_t addend,
                             bool prefer_section_symbol) {
    return minias_record_fixup(as, section, offset, kind, width,
                               symbol_name, SIZE_MAX, addend, origin,false,
                               prefer_section_symbol,
                               SIZE_MAX);
}

bool minias_add_explicit_section_abs0_fixup(MiniAs *as,int section,
                                            uint64_t offset,
                                            MiniAsFixupKind kind,
                                            unsigned width) {
    return minias_record_fixup(as,section,offset,kind,width,
                               NULL,SIZE_MAX,0,
                               MINIAS_FIXUP_ORIGIN_EXPLICIT_SECTION,
                               true,false,SIZE_MAX);
}

/* The final linker-relaxation predicate needs all Target MCInst fixups,
 * including instructions parsed *after* the ULEB source line. A second
 * retained-fragment commit window allows its paired relocation records to
 * be added after full emission without pretending they are .reloc or DWARF
 * section-owned fixups. The encoded ULEB bytes remain the fragment's own. */
bool minias_add_late_fragment_fixup(MiniAs *as,size_t fragment_id,
                                   MiniAsFixupKind kind,size_t symbol_index,
                                   int64_t addend) {
    const MiniAsStmt *stmt;
    if(as==NULL || as->emission==NULL ||
       as->emission->emitting_fragment || as->emission->committing_fragment ||
       fragment_id>=as->stmt_count || symbol_index>=as->symbol_count) {
        if(as!=NULL)minias_set_error(as,"invalid-late-fragment-fixup");
        return false;
    }
    stmt=&as->stmts[fragment_id];
    if(stmt->fragment_kind!=MINIAS_FRAGMENT_BYTES || stmt->payload.bytes.leb_mode!=1U ||
       (kind!=MINIAS_FIXUP_LEB_ADD && kind!=MINIAS_FIXUP_LEB_SUB &&
        kind!=MINIAS_FIXUP_LEB_SET)) {
        minias_set_error(as,"late-fixup-non-uleb-fragment:line=%zu",
                         stmt->line);
        return false;
    }
    /* GNU GAS keeps local label-difference anchors as canonical
     * section-symbol relocations. Record that identity at creation. */
    if(!minias_record_fixup(as,stmt->section,stmt->offset,kind,0U,
                            NULL,symbol_index,addend,
                            MINIAS_FIXUP_ORIGIN_FRAGMENT,false,true,
                            fragment_id))
        return false;
    return true;
}

static bool minias_stage_fragment_bytes(MiniAs *,int,const void *,size_t,bool);
static bool minias_section_append_pattern(MiniAs *,int,const unsigned char *,
                                          size_t,size_t,bool);
static bool minias_section_append_fill(MiniAs *,int,size_t,uint8_t,bool);

/* Commit a complete source Fragment's result after its measured length has
 * passed the common invariant. Both target code and typed common data/fills
 * take this path. Explicit .reloc and generated DWARF remain section-owned. */
/* Every source fragment, including typed MCData and target instruction,
 * commits through the same length/fixup transaction. No Target code runs
 * during this phase. Section-owned .reloc and DWARF retain their own APIs.
 *
 * On allocation failure assembly aborts without writing an output object;
 * semantic state never becomes visible to a subsequent Target invocation.
 */
static bool minias_commit_encode_result(MiniAs *as, const MiniAsStmt *owner) {
    MiniAsEncodeResult *result = as->emission->active_encode_result;
    size_t i,logical_size=0U;
    if (result == NULL || !as->emission->emitting_fragment ||
        owner != &as->stmts[as->emission->emitting_fragment_id]) {
        minias_set_error(as, "encode-commit-without-fragment");
        return false;
    }
    if (result->byte_count != owner->size ||
        owner->offset > UINT64_MAX - owner->size ||
        owner->section < 0 || (size_t)owner->section >= as->section_count) {
        minias_set_error(as, "invalid-complete-encode-result:line=%zu",
                         owner->line);
        return false;
    }
    for(i=0U;i<result->write_count;++i) {
        const MiniAsFragmentWrite *write=&result->writes[i];
        if(write->size>owner->size-logical_size ||
           (write->kind==MINIAS_WRITE_COPIED_BYTES &&
            (write->copied_offset>result->byte_storage_size ||
             write->size>result->byte_storage_size-write->copied_offset)) ||
           (write->kind==MINIAS_WRITE_BORROWED_BYTES &&
            write->borrowed_bytes==NULL) ||
           (write->kind==MINIAS_WRITE_PATTERN &&
            (write->repeat_width==0U || write->repeat_width>8U ||
             write->repeat_count!=write->size/write->repeat_width ||
             write->size%write->repeat_width!=0U)) ||
           write->kind>MINIAS_WRITE_PATTERN) {
            minias_set_error(as,"invalid-fragment-write:line=%zu",owner->line);
            return false;
        }
        logical_size+=write->size;
    }
    if(logical_size!=result->byte_count) {
        minias_set_error(as,"incomplete-fragment-writes:line=%zu",owner->line);
        return false;
    }
    /* Validate requests before the first global mutation. */
    if (result->anchor_count > SIZE_MAX - as->emission->synthetic_anchor_counter ||
        result->fixup_count > SIZE_MAX - as->emission->fixup_count) {
        minias_set_error(as, "encoded-request-count-overflow");
        return false;
    }
    for (i = 0U; i < result->anchor_count; ++i) {
        const MiniAsEncodeAnchorRequest *request = &result->anchors[i];
        const char *name=request->name_id < as->symbol_name_count
            ? as->symbol_names[request->name_id].name : NULL;
        if (name==NULL ||
            minias_get_symbol(as,name,false) != NULL) {
            minias_set_error(as, "invalid-encoded-anchor:line=%zu",
                             owner->line);
            return false;
        }
    }
    for (i = 0U; i < result->fixup_count; ++i) {
        const MiniAsEncodeFixupRequest *request = &result->fixups[i];
        if (request->fragment_offset > owner->size ||
            request->width > owner->size - request->fragment_offset ||
            (!request->absolute_zero_symbol && (request->has_symbol_index ?
             request->symbol_index>=as->symbol_count :
             request->symbol_ref.name_id>=as->symbol_name_count ||
             as->symbol_names[request->symbol_ref.name_id].name==NULL))) {
            minias_set_error(as, "invalid-encoded-fixup-commit:line=%zu",
                             owner->line);
            return false;
        }
    }
    /* Reserve the Fixup backing storage before defining synthetic symbols.
     * Their final indices and relocation bindings still belong to Common. */
    if (!minias_grow_array((void **)&as->emission->fixups, &as->emission->fixup_capacity,
                           sizeof(*as->emission->fixups),
                           as->emission->fixup_count + result->fixup_count)) {
        minias_set_error(as, "out-of-memory:encoded-fixup-commit");
        return false;
    }
    for (i = 0U; i < result->anchor_count; ++i) {
        const MiniAsEncodeAnchorRequest *request = &result->anchors[i];
        const char *name=as->symbol_names[request->name_id].name;
        MiniAsSymbol *anchor = minias_get_symbol(as, name, true);
        if (anchor == NULL) return false;
        anchor->defined = true;
        anchor->section = owner->section;
        anchor->subsection = owner->subsection;
        anchor->value = owner->offset;
        anchor->bind = MINIAS_STB_LOCAL;
    }
    for (i = 0U; i < result->fixup_count; ++i) {
        const MiniAsEncodeFixupRequest *request = &result->fixups[i];
        const char *name=request->absolute_zero_symbol || request->has_symbol_index ? NULL :
            as->symbol_names[request->symbol_ref.name_id].name;
        if (!minias_record_fixup(as, owner->section,
                                 owner->offset + request->fragment_offset,
                                 request->kind, request->width,
                                 name,
                                 request->has_symbol_index ?
                                     request->symbol_index : SIZE_MAX,
                                 request->absolute_zero_symbol ? 0 : request->symbol_ref.addend,
                                 MINIAS_FIXUP_ORIGIN_FRAGMENT,
                                 request->absolute_zero_symbol,
                                 request->prefer_section_symbol,SIZE_MAX))
            return false;
    }
    /* One ordered commit protocol handles any mixture of fixed DATA fields,
     * deferred relocations, borrowed source bytes and compact repeated fills.
     * There is no alternate direct-append path for a source Fragment. */
    as->emission->committing_fragment=true;
    for(i=0U;i<result->write_count;++i) {
        const MiniAsFragmentWrite *write=&result->writes[i];
        bool materialized;
        if(write->kind==MINIAS_WRITE_PATTERN)
            materialized=minias_section_append_pattern(
                as,owner->section,write->pattern,write->repeat_width,
                write->repeat_count,write->ignore_nobits_fill);
        else
            materialized=minias_section_append(
                as,owner->section,
                write->kind==MINIAS_WRITE_BORROWED_BYTES ?
                    write->borrowed_bytes : result->bytes+write->copied_offset,
                write->size);
        if(!materialized) {
            as->emission->committing_fragment=false;
            return false;
        }
    }
    as->emission->committing_fragment=false;
    as->emission->synthetic_anchor_counter += result->anchor_count;
    as->elf_flags |= result->elf_flags_or;
    minias_encode_result_destroy(result);
    return true;
}

/* Target-owned executable alignment NOP bytes and the private fragment
 * transaction must use one policy. A fill like x86's 0x90 can explicitly
 * request canonical NOPs while other fill bytes remain literal DATA. */
static bool minias_alignment_emits_target_nops(const MiniAs *as,
                                               const MiniAsStmt *stmt,
                                               const MiniAsSection *sec) {
    return stmt->fragment_kind==MINIAS_FRAGMENT_ALIGNMENT &&
           (sec->flags & MINIAS_SHF_EXECINSTR)!=0U &&
           (!stmt->payload.alignment.has_fill ||
            (as->target->explicit_alignment_fill_is_code_nop &&
             as->target->explicit_alignment_fill_is_code_nop(
                 stmt->payload.alignment.fill)));
}

static bool minias_stage_instruction_bytes(MiniAs *as,
                                           const void *data, size_t size) {
    MiniAsSection *sec;
    int section_index;
    MiniAsEncodeResult *result = as->emission->active_encode_result;
    if (result == NULL || !as->emission->emitting_fragment) {
        minias_set_error(as, "target-emission-outside-instruction");
        return false;
    }
    const MiniAsStmt *owner;
    if (as->emission->emitting_fragment_id >= as->stmt_count) {
        minias_set_error(as, "instruction-bytes-without-fragment");
        return false;
    }
    owner = &as->stmts[as->emission->emitting_fragment_id];
    /* Machine-code bytes can only enter the current owned Fragment. */
    section_index=owner->section;
    if(section_index<0 || (size_t)section_index>=as->section_count) {
        minias_set_error(as,"invalid-target-emission-section");
        return false;
    }
    sec=&as->sections[(size_t)section_index];
    /* Instruction and executable-padding fragments use the same private
     * byte transaction.  A Target cannot append alignment NOPs directly to
     * the final Section, where a failed encoder would leave partial bytes. */
    if ((owner->fragment_kind != MINIAS_FRAGMENT_INSTRUCTION &&
         !minias_alignment_emits_target_nops(as,owner,sec)) ||
        sec->type == MINIAS_SHT_NOBITS ||
        size > SIZE_MAX - sec->size) {
        minias_set_error(as, "instruction-emission-section-or-size");
        return false;
    }
    if(!minias_stage_fragment_bytes(as,section_index,data,size,true))return false;
    /* The CodeEmitter owns bytes until the transaction commits. A Target
     * never updates a final Section size or cursor while encoding. */
    return true;

}

struct MiniAsEncodeContext {
    MiniAs *as; /* Only Common constructs and dereferences this capability. */
};

bool minias_encode_ctx_emit(MiniAsEncodeContext *ctx,
                            const void *bytes,size_t size) {
    return minias_stage_instruction_bytes(ctx->as,bytes,size);
}

bool minias_encode_ctx_fixup_ref(MiniAsEncodeContext *ctx,uint64_t offset,
                                 MiniAsFixupKind kind,unsigned width,
                                 MiniAsSymbolRef ref,bool section_symbol) {
    return minias_encode_add_fixup_ref(ctx->as,offset,kind,width,ref,
                                       section_symbol);
}

bool minias_encode_ctx_fixup_abs0(MiniAsEncodeContext *ctx,uint64_t offset,
                                  MiniAsFixupKind kind,unsigned width) {
    return minias_encode_add_fixup_abs0(ctx->as,offset,kind,width);
}

bool minias_encode_ctx_anchor_ref(MiniAsEncodeContext *ctx,const char *prefix,
                                  MiniAsSymbolRef *ref) {
    return minias_encode_request_anchor_ref(ctx->as,prefix,ref);
}

bool minias_encode_ctx_elf_flags(MiniAsEncodeContext *ctx,uint32_t flags) {
    return minias_encode_add_elf_flags(ctx->as,flags);
}

void minias_encode_ctx_error(MiniAsEncodeContext *ctx,const char *format,...) {
    va_list ap;
    va_start(ap,format);
    minias_set_error_va(ctx->as,format,ap);
    va_end(ap);
}

bool minias_encode_ctx_symbol_lookup(MiniAsEncodeContext *ctx,
                                     MiniAsSymbolRef ref,
                                     MiniAsSymbolView *out) {
    return minias_project_symbol(minias_symbol_ref_lookup(ctx->as,ref),out);
}

bool minias_encode_ctx_resolve_difference(
    MiniAsEncodeContext *ctx,const MiniAsSymbolDifferenceRef *difference,
    int64_t *value) {
    return minias_resolve_symbol_difference_ref(ctx->as,difference,value);
}

bool minias_encode_ctx_resolve_difference_field(
    MiniAsEncodeContext *ctx,const MiniAsSymbolDifferenceRef *difference,
    int section,uint64_t offset,int64_t *value,bool *has_reloc,
    MiniAsSymbolRef *ref) {
    return minias_resolve_symbol_difference_field(ctx->as,difference,section,
                                                  offset,value,has_reloc,ref);
}

bool minias_encode_ctx_symbol_is_dot(MiniAsEncodeContext *ctx,
                                     MiniAsSymbolRef ref) {
    const MiniAs *as=ctx->as;
    return ref.name_id<as->symbol_name_count &&
           strcmp(as->symbol_names[ref.name_id].name,".")==0;
}

const void *minias_encode_ctx_target_state(const MiniAsEncodeContext *ctx) {
    return ctx->as->target_state;
}

bool minias_encode_ctx_is_elf32(const MiniAsEncodeContext *ctx) {
    return ctx->as->output_elf32;
}

struct MiniAsCodeAlignSink {
    MiniAs *as; /* Only Common constructs and dereferences this capability. */
};

bool minias_code_align_emit(MiniAsCodeAlignSink *sink,
                            const void *bytes,size_t size) {
    return sink!=NULL && minias_stage_instruction_bytes(sink->as,bytes,size);
}

bool minias_code_align_add_elf_flags(MiniAsCodeAlignSink *sink,uint32_t flags) {
    return sink!=NULL && minias_encode_add_elf_flags(sink->as,flags);
}

void minias_code_align_error(MiniAsCodeAlignSink *sink,const char *format,...) {
    va_list ap;
    if(sink==NULL)return;
    va_start(ap,format);
    minias_set_error_va(sink->as,format,ap);
    va_end(ap);
}

const void *minias_code_align_target_state(const MiniAsCodeAlignSink *sink) {
    return sink!=NULL?sink->as->target_state:NULL;
}

bool minias_code_align_is_elf32(const MiniAsCodeAlignSink *sink) {
    return sink!=NULL && sink->as->output_elf32;
}

bool minias_code_align_has_active_fragment(const MiniAsCodeAlignSink *sink) {
    const MiniAs *as=sink!=NULL?sink->as:NULL;
    return as!=NULL && as->emission!=NULL && as->emission->emitting_fragment &&
           as->emission->emitting_fragment_id<as->stmt_count;
}

/* Source-order and final position are Common-owned properties, not private
 * target parser data.  The x86 padding policy queries this site predicate
 * without reaching through MiniAs into neighboring retained statements. */
bool minias_code_align_preceded_by_adjacent_data(const MiniAsCodeAlignSink *sink) {
    const MiniAs *as=sink!=NULL?sink->as:NULL;
    const MiniAsStmt *owner, *previous;
    if(as==NULL || as->emission==NULL || !as->emission->emitting_fragment ||
       as->emission->emitting_fragment_id==0U ||
       as->emission->emitting_fragment_id>=as->stmt_count)return false;
    owner=&as->stmts[as->emission->emitting_fragment_id];
    previous=&as->stmts[as->emission->emitting_fragment_id-1U];
    /* Active Fragment owns Section; only predecessor needs comparison. */
    return owner->fragment_kind==MINIAS_FRAGMENT_ALIGNMENT &&
           (previous->fragment_kind==MINIAS_FRAGMENT_FIXED_DATA ||
            previous->fragment_kind==MINIAS_FRAGMENT_FILL ||
            previous->fragment_kind==MINIAS_FRAGMENT_SPACE ||
            previous->fragment_kind==MINIAS_FRAGMENT_BYTES) &&
           previous->section==owner->section &&
           previous->subsection==owner->subsection &&
           previous->offset<=UINT64_MAX-previous->size &&
           previous->offset+previous->size==owner->offset;
}

/* Materialized Section data uses the same checked capacity owner as every
 * Common vector. NOBITS remains each caller's semantic contract. */
static bool minias_section_reserve(MiniAs *as,MiniAsSection *sec,size_t need) {
    if(!minias_grow_array((void **)&sec->data,&sec->capacity,1U,need)) {
        minias_set_error(as,"out-of-memory:section-data");
        return false;
    }
    return true;
}

/* Staging is the only byte-producing path inside a source Fragment.
 * Raw data stays in the owned result; huge immutable byte snapshots are
 * borrowed from the retained MCStmt; fills/patterns use constant storage.
 * Section-owned CFI/DWARF calls, made after all Fragments, continue using
 * the direct materializer below. */
static bool minias_stage_fragment_bytes(MiniAs *as,int section,
                                         const void *data,size_t size,
                                         bool target_bytes) {
    MiniAsEncodeResult *result=as->emission->active_encode_result;
    const MiniAsStmt *owner;
    MiniAsSection *sec=&as->sections[(size_t)section];
    const unsigned char *bytes=(const unsigned char *)data;
    MiniAsFragmentWrite *write;
    size_t i,needed;
    bool borrow;
    if(result==NULL || as->emission->emitting_fragment_id>=as->stmt_count ||
       (size!=0U && data==NULL)) {
        minias_set_error(as,"invalid-fragment-bytes");
        return false;
    }
    owner=&as->stmts[as->emission->emitting_fragment_id];
    if(owner->section!=section ||
       (target_bytes ?
        (owner->fragment_kind!=MINIAS_FRAGMENT_INSTRUCTION &&
         !minias_alignment_emits_target_nops(as,owner,sec)) :
        (owner->fragment_kind==MINIAS_FRAGMENT_INSTRUCTION ||
         minias_alignment_emits_target_nops(as,owner,sec))) ||
       result->byte_count>owner->size ||
       size>owner->size-result->byte_count) {
        minias_set_error(as,"fragment-byte-stage-conflict:line=%zu",owner->line);
        return false;
    }
    if(sec->type==MINIAS_SHT_NOBITS)
        for(i=0U;i<size;++i)
            if(bytes[i]!=0U) {
                minias_set_error(as,"nobits-nonzero-data:%s",sec->name);
                return false;
            }
    if(size==0U)return true;
    borrow=!target_bytes && owner->fragment_kind==MINIAS_FRAGMENT_BYTES &&
           data==owner->payload.bytes.data && size==owner->payload.bytes.data_size;
    if(!borrow) {
        if(size>SIZE_MAX-result->byte_storage_size) {
            minias_set_error(as,"fragment-copied-size-overflow");
            return false;
        }
        needed=result->byte_storage_size+size;
        if(!minias_grow_array((void **)&result->bytes,
                             &result->byte_capacity,sizeof(unsigned char),
                             needed)) {
            minias_set_error(as,"out-of-memory:fragment-bytes");
            return false;
        }
        memcpy(result->bytes+result->byte_storage_size,bytes,size);
    }
    if(!minias_grow_array((void **)&result->writes,
                          &result->write_capacity,sizeof(*result->writes),
                          result->write_count+1U)) {
        minias_set_error(as,"out-of-memory:fragment-writes");
        return false;
    }
    write=&result->writes[result->write_count++];
    memset(write,0,sizeof(*write));
    write->kind=borrow ? MINIAS_WRITE_BORROWED_BYTES : MINIAS_WRITE_COPIED_BYTES;
    write->size=size;
    if(borrow)write->borrowed_bytes=bytes;
    else {
        write->copied_offset=result->byte_storage_size;
        result->byte_storage_size+=size;
    }
    result->byte_count+=size;
    return true;
}

static bool minias_stage_fragment_pattern(MiniAs *as,int section,
                                           const unsigned char *pattern,
                                           size_t width,size_t count,
                                           bool ignore_nobits_fill) {
    MiniAsEncodeResult *result=as->emission->active_encode_result;
    const MiniAsStmt *owner;
    MiniAsSection *sec=&as->sections[(size_t)section];
    MiniAsFragmentWrite *write;
    size_t size,i;
    if(result==NULL || as->emission->emitting_fragment_id>=as->stmt_count ||
       width==0U || width>8U || count>SIZE_MAX/width) {
        minias_set_error(as,"invalid-fragment-pattern");
        return false;
    }
    owner=&as->stmts[as->emission->emitting_fragment_id];
    size=width*count;
    if(owner->section!=section ||
       owner->fragment_kind==MINIAS_FRAGMENT_INSTRUCTION ||
       minias_alignment_emits_target_nops(as,owner,sec) ||
       result->byte_count>owner->size || size>owner->size-result->byte_count) {
        minias_set_error(as,"fragment-pattern-stage-conflict:line=%zu",
                         owner->line);
        return false;
    }
    if(sec->type==MINIAS_SHT_NOBITS && !ignore_nobits_fill)
        for(i=0U;i<width;++i)
            if(pattern[i]!=0U) {
                minias_set_error(as,"nobits-nonzero-data:%s",sec->name);
                return false;
            }
    if(size==0U)return true;
    if(!minias_grow_array((void **)&result->writes,
                          &result->write_capacity,sizeof(*result->writes),
                          result->write_count+1U)) {
        minias_set_error(as,"out-of-memory:fragment-writes");
        return false;
    }
    write=&result->writes[result->write_count++];
    memset(write,0,sizeof(*write));
    write->kind=MINIAS_WRITE_PATTERN;
    write->size=size;
    memcpy(write->pattern,pattern,width);
    write->repeat_width=(unsigned)width;
    write->repeat_count=count;
    write->ignore_nobits_fill=ignore_nobits_fill;
    result->byte_count+=size;
    return true;
}

bool minias_section_append(MiniAs *as, int section_index, const void *data, size_t size) {
    MiniAsSection *sec;
    size_t need;
    if (section_index < 0 || (size_t)section_index >= as->section_count) {
        minias_set_error(as, "internal:bad-section");
        return false;
    }
    sec = &as->sections[(size_t)section_index];
    if(as->emission!=NULL && as->emission->emitting_fragment &&
       !as->emission->committing_fragment)
        return minias_stage_fragment_bytes(as,section_index,data,size,false);
    if (sec->type == MINIAS_SHT_NOBITS) {
        const unsigned char *bytes=(const unsigned char *)data;
        size_t i;
        if(size>SIZE_MAX-sec->size) {
            minias_set_error(as,"section-size-overflow:%s",sec->name);
            return false;
        }
        /* GNU rejects real nonzero data in SHT_NOBITS. Previously this
         * branch silently threw it away and produced a success ELF. */
        for(i=0U;i<size;++i) {
            if(bytes[i]!=0U) {
                minias_set_error(as,"nobits-nonzero-data:%s",sec->name);
                return false;
            }
        }
        sec->size+=size;
        return true;
    }
    if (size > SIZE_MAX - sec->size) {
        minias_set_error(as, "section-size-overflow:%s", sec->name);
        return false;
    }
    need = sec->size + size;
    if(!minias_section_reserve(as,sec,need))return false;
    if (size != 0U) {
        memcpy(sec->data + sec->size, data, size);
    }
    sec->size = need;
    return true;
}

/* All repeated fills have one materialization path, including fragment
 * transaction commits. GNU .space/.skip may ignore the fill in NOBITS;
 * literal DATA/.fill may not silently discard nonzero bytes. */
static bool minias_section_append_pattern(MiniAs *as,int section_index,
                                           const unsigned char *pattern,
                                           size_t width,size_t repeat,
                                           bool ignore_nobits_fill) {
    MiniAsSection *sec;
    size_t bytes,filled,i;
    unsigned char *output;
    if(section_index<0 || (size_t)section_index>=as->section_count ||
       width==0U || width>8U || repeat>SIZE_MAX/width) {
        minias_set_error(as,"invalid-data-pattern");
        return false;
    }
    if(as->emission!=NULL && as->emission->emitting_fragment &&
       !as->emission->committing_fragment)
        return minias_stage_fragment_pattern(as,section_index,pattern,width,
                                             repeat,ignore_nobits_fill);
    sec=&as->sections[(size_t)section_index];
    bytes=width*repeat;
    if(bytes>SIZE_MAX-sec->size) {
        minias_set_error(as,"section-size-overflow:%s",sec->name);
        return false;
    }
    if(sec->type==MINIAS_SHT_NOBITS) {
        if(!ignore_nobits_fill)
            for(i=0U;i<width;++i)
                if(pattern[i]!=0U) {
                    minias_set_error(as,"nobits-nonzero-data:%s",sec->name);
                    return false;
                }
        sec->size+=bytes;
        return true;
    }
    if(!minias_section_reserve(as,sec,sec->size+bytes))return false;
    if(bytes!=0U) {
        output=sec->data+sec->size;
        /* Seed once and double the already written prefix, O(log repeat). */
        memcpy(output,pattern,width);
        filled=width;
        while(filled<bytes) {
            size_t chunk=filled<bytes-filled?filled:bytes-filled;
            memcpy(output+filled,output,chunk);
            filled+=chunk;
        }
    }
    sec->size+=bytes;
    return true;
}

static bool minias_section_append_fill(MiniAs *as,int section_index,
                                        size_t size,uint8_t fill,
                                        bool ignore_nobits_fill) {
    return minias_section_append_pattern(as,section_index,&fill,1U,size,
                                         ignore_nobits_fill);
}

bool minias_section_append_zero(MiniAs *as,int section_index,size_t size) {
    return minias_section_append_fill(as,section_index,size,0U,false);
}


static bool emit_common_pattern(MiniAs *as,int section,
                                const unsigned char *element,
                                size_t width,size_t repeat) {
    return minias_section_append_pattern(as,section,element,width,repeat,false);
}


static void minias_store_le32(unsigned char bytes[4],uint32_t value) {
    unsigned i;
    for(i=0U;i<4U;++i)bytes[i]=(unsigned char)(value>>(i*8U));
}

bool minias_section_append_le32(MiniAs *as,int sec,uint32_t value) {
    unsigned char bytes[4];
    minias_store_le32(bytes,value);
    return minias_section_append(as,sec,bytes,4U);
}

/* CFI and DWARF-line record headers are patched after their final size is
 * known. Check subtraction bounds before touching the Common Section bytes. */
bool minias_section_patch_le32(MiniAs *as,int sec,size_t at,uint64_t value) {
    MiniAsSection *section;
    if(as==NULL || sec<0 || (size_t)sec>=as->section_count ||
       value>UINT32_MAX)return false;
    section=&as->sections[(size_t)sec];
    if(section->data==NULL || at>section->size ||
       section->size-at<4U)return false;
    minias_store_le32(section->data+at,(uint32_t)value);
    return true;
}

bool minias_section_append_uleb(MiniAs *as,int sec,uint64_t value) {
    unsigned char bytes[10];
    size_t length;
    return minias_encode_uleb_integer(value,bytes,&length) &&
           minias_section_append(as,sec,bytes,length);
}

bool minias_section_append_sleb(MiniAs *as,int sec,int64_t value) {
    unsigned char bytes[10];
    size_t length;
    return minias_encode_leb_integer(value,true,bytes,&length) &&
           minias_section_append(as,sec,bytes,length);
}


static bool minias_emit_data_fragment(MiniAs *as, const MiniAsStmt *stmt) {
    /* Source spelling is intentionally absent from this dispatcher.
     * Every data producer, including synthesized literal pools, installs
     * its semantic FragmentKind while parsing, once and for all. */
    switch(stmt->fragment_kind) {
    case MINIAS_FRAGMENT_FILL: {
        uint64_t repeat=stmt->payload.variable.repeat;
        uint64_t value=stmt->payload.variable.value;
        unsigned width_bytes=stmt->payload.variable.element_width,j;
        unsigned char element[8];
        if(!stmt->payload.variable.resolved || width_bytes>8U ||
           (width_bytes!=0U &&
            (repeat>UINT32_MAX/width_bytes ||
             repeat*width_bytes!=(uint64_t)stmt->size))) {
            minias_set_error(as,"invalid-typed-fill-fragment:line=%zu",
                             stmt->line);
            return false;
        }
        if(width_bytes==0U)return stmt->size==0U;
        for(j=0U;j<width_bytes;++j)
            element[j]=(unsigned char)(value>>(8U*j));
        return emit_common_pattern(as,stmt->section,element,width_bytes,
                                   (size_t)repeat);
    }
    case MINIAS_FRAGMENT_BYTES:
        if(stmt->payload.bytes.data_size!=stmt->size ||
           (stmt->size!=0U && !stmt->payload.bytes.data)) {
            minias_set_error(as,"immutable-data-fragment-size:line=%zu",
                             stmt->line);
            return false;
        }
        /* DATA emission consumes the owned byte snapshot (lexical bytes or
         * fixed-point-resolved LEB), never reinterpreting source text. */
        return minias_section_append(as,stmt->section,
                                     stmt->payload.bytes.data,stmt->payload.bytes.data_size);
    case MINIAS_FRAGMENT_SPACE:
        if(!stmt->payload.variable.resolved ||
           stmt->payload.variable.element_width!=1U ||
           stmt->payload.variable.repeat!=(uint64_t)stmt->size ||
           stmt->payload.variable.value>255U) {
            minias_set_error(as,"invalid-typed-space-fragment:line=%zu",
                             stmt->line);
            return false;
        }
        return minias_section_append_fill(as,stmt->section,stmt->size,
                                          (uint8_t)stmt->payload.variable.value,true);
    case MINIAS_FRAGMENT_FIXED_DATA:
        return minias_emit_mc_data(as,stmt);
    default:
        minias_set_error(as,"unexpected-data-fragment-kind:%u:line=%zu",
                         (unsigned)stmt->fragment_kind,stmt->line);
        return false;
    }
}


typedef struct MiniAsEmitRef {
    MiniAsStmt *stmt;
    size_t order;
} MiniAsEmitRef;

static int compare_emit_refs(const void *lhs_ptr, const void *rhs_ptr) {
    const MiniAsEmitRef *lhs = lhs_ptr;
    const MiniAsEmitRef *rhs = rhs_ptr;

    if (lhs->stmt->section != rhs->stmt->section) {
        return lhs->stmt->section < rhs->stmt->section ? -1 : 1;
    }
    if (lhs->stmt->offset != rhs->stmt->offset) {
        return lhs->stmt->offset < rhs->stmt->offset ? -1 : 1;
    }
    /* Equal final offsets occur at subsection boundaries. GNU emits all
     * zero-size origin/label fragments in subsection 0 BEFORE instructions
     * in subsection 1 even if the latter appeared earlier in source. A
     * source-order tiebreak incorrectly wrote a subsection-1 .inst first,
     * making the preceding .org appear to overlap its four bytes (real
     * Linux arm64 KVM hyp-entry). Order by output subsection ownership. */
    if (lhs->stmt->subsection != rhs->stmt->subsection) {
        return lhs->stmt->subsection < rhs->stmt->subsection ? -1 : 1;
    }
    if (lhs->order != rhs->order) {
        return lhs->order < rhs->order ? -1 : 1;
    }
    return 0;
}

bool minias_emit_sections(MiniAs *as) {
    size_t i;
    MiniAsEmitRef *refs = NULL;

    if(as==NULL || as->emission==NULL) {
        if(as!=NULL)minias_set_error(as,"emit-without-emission-state");
        return false;
    }
    if(!minias_validate_fragment_semantics(as))return false;
    if (as->emission->active_encode_result != NULL) {
        minias_set_error(as, "encode-result-reentered");
        return false;
    }
    as->emission->active_encode_result = calloc(1U, sizeof(*as->emission->active_encode_result));
    if (as->emission->active_encode_result == NULL) {
        minias_set_error(as, "out-of-memory:encode-result");
        return false;
    }

    for (i = 0U; i < as->section_count; ++i) {
        as->sections[i].size = 0U;
    }
    if (as->stmt_count != 0U) {
        refs = malloc(as->stmt_count * sizeof(*refs));
        if (refs == NULL) {
            minias_set_error(as, "out-of-memory:emit-order");
            return false;
        }
        for (i = 0U; i < as->stmt_count; ++i) {
            refs[i].stmt = &as->stmts[i];
            refs[i].order = i;
        }
        qsort(refs, as->stmt_count, sizeof(*refs), compare_emit_refs);
    }

    for (i = 0U; i < as->stmt_count; ++i) {
        MiniAsStmt *stmt = refs[i].stmt;
        MiniAsSection *section = &as->sections[(size_t)stmt->section];
        if ((uint64_t)section->size > stmt->offset) {
            /* The offset invariant must fail CLOSED, but record the
             * exact preceding fragment to locate the real producer that
             * emitted > its measured size. Never hide overlap by
             * truncating the object or rewriting .org into a no-op. */
            const MiniAsStmt *previous=i!=0U?refs[i-1U].stmt:NULL;
            minias_set_error(as,
                "internal-offset:%s:line=%zu:expected=%llu:actual=%llu:"
                "previous=%s:previous_offset=%llu:previous_size=%u:"
                "previous_line=%zu:previous_source=%.35s",
                stmt->op,stmt->line,
                (unsigned long long)stmt->offset,
                (unsigned long long)section->size,
                previous?previous->op:"none",
                previous?(unsigned long long)previous->offset:0ULL,
                previous?previous->size:0U,
                previous?previous->line:0U,
                previous?previous->args:"none");
            free(refs);
            return false;
        }
        if ((uint64_t)section->size < stmt->offset) {
            uint64_t gap = stmt->offset - (uint64_t)section->size;
            if (gap > SIZE_MAX ||
                !minias_section_append_zero(as, stmt->section, (size_t)gap)) {
                free(refs);
                return false;
            }
        }
        /* The engine owns the complete fragment transaction. No direct
         * DATA/ALIGN/FILL append path survives inside source emission. */
        as->emission->emitting_fragment = true;
        as->emission->emitting_fragment_id = refs[i].order;
        as->emission->committing_fragment=false;
        if (stmt->fragment_kind == MINIAS_FRAGMENT_ALIGNMENT ||
            stmt->fragment_kind == MINIAS_FRAGMENT_ORIGIN) {
            const bool has_fill=stmt->fragment_kind==MINIAS_FRAGMENT_ALIGNMENT ?
                stmt->payload.alignment.has_fill:stmt->payload.origin.has_fill;
            const unsigned char fill=stmt->fragment_kind==MINIAS_FRAGMENT_ALIGNMENT ?
                stmt->payload.alignment.fill:stmt->payload.origin.fill;
            bool code_nop_align=minias_alignment_emits_target_nops(
                as,stmt,section);
            MiniAsCodeAlignSink alignment_sink={as};
            const MiniAsCodeAlignmentView site={
                .line=stmt->line,
                .site=stmt->site
            };
            if (!(code_nop_align
                      ? as->target->append_code_alignment(&alignment_sink,&site,stmt->size)
                      : has_fill
                      ? minias_section_append_fill(as,stmt->section,stmt->size,
                                                   fill,true)
                      : minias_section_append_zero(as,stmt->section,stmt->size))) {
                if (as->error[0] == '\0') {
                    minias_set_error(as,
                                     "emit-align-failed:%s:line=%zu",
                                     stmt->op,
                                     stmt->line);
                }
                free(refs);
                return false;
            }
        } else if (stmt->fragment_kind == MINIAS_FRAGMENT_FIXED_DATA ||
                   stmt->fragment_kind == MINIAS_FRAGMENT_FILL ||
                   stmt->fragment_kind == MINIAS_FRAGMENT_SPACE ||
                   stmt->fragment_kind == MINIAS_FRAGMENT_BYTES) {
            if (!minias_emit_data_fragment(as, stmt)) {
                if (as->error[0] == '\0') {
                    minias_set_error(as,
                                     "emit-data-failed:%s:%s:line=%zu",
                                     stmt->op,
                                     stmt->args,
                                     stmt->line);
                }
                free(refs);
                return false;
            }
        } else {
            const MiniAsMCInst *source=stmt->payload.instruction.inst;
            const size_t count=source && source->count<=source->capacity?
                               source->count:0U;
            MiniAsMCOperandView operands[count?count:1U];
            MiniAsMCInstView instruction;
            MiniAsInstructionView snapshot;
            if (!minias_project_instruction(stmt,&snapshot,&instruction,
                                            operands,count) ||
                !as->target->encode(&(MiniAsEncodeContext){as},&snapshot)) {
                if (as->error[0] == '\0') {
                    minias_set_error(as,
                                     "encode-failed:%s:%s:line=%zu",
                                     stmt->op,stmt->args,stmt->line);
                }
                free(refs);
                return false;
            }
        }
        /* Every source Fragment uses the same measured-size validation
         * and commit. No fixed DATA, ALIGN, FILL or SPACE bypass remains. */
        if(as->emission->active_encode_result->byte_count!=stmt->size ||
           stmt->offset>SIZE_MAX) {
            minias_set_error(as,
                "fragment-emitted-size-mismatch:line=%zu:expected=%u:actual=%zu",
                stmt->line,stmt->size,
                as->emission->active_encode_result->byte_count);
            free(refs);
            return false;
        }
        if(!minias_commit_encode_result(as,stmt)) {
            free(refs);
            return false;
        }
        as->emission->emitting_fragment = false;
    }
    free(refs);

    for (i = 0U; i < as->section_count; ++i) {
        MiniAsSection *section = &as->sections[i];

        if ((uint64_t)section->size > section->layout_size) {
            minias_set_error(as, "internal:section-layout-size:%s", section->name);
            return false;
        }
        if ((uint64_t)section->size < section->layout_size) {
            uint64_t gap = section->layout_size - (uint64_t)section->size;
            if (gap > SIZE_MAX ||
                !minias_section_append_zero(as, (int)i, (size_t)gap)) {
                if (as->error[0] == '\0') {
                    minias_set_error(as,
                                     "emit-section-pad-failed:%s",
                                     section->name);
                }
                return false;
            }
        }
    }
    return minias_emit_deferred_leb_relocations(as) &&
           minias_cfi_emit(as) && minias_debug_emit(as);
}
