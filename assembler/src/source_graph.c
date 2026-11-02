#define _POSIX_C_SOURCE 200809L
#include "source_graph_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

MiniAsSourceGraph *minias_source_graph_create(void) {
    return calloc(1U,sizeof(MiniAsSourceGraph));
}

bool minias_source_graph_is_loaded(const MiniAsSourceGraph *graph) {
    return graph!=NULL && graph->file_reads!=0U;
}

size_t minias_source_graph_physical_lines(const MiniAsSourceGraph *graph) {
    return graph->physical.count;
}

/* Trace belongs to the owner of this persistent state, not to Common's
 * Fragment lowerer. Preserve the existing machine-readable diagnostic. */
void minias_source_graph_trace(const MiniAsSourceGraph *graph,
                               size_t replay_passes,FILE *diagnostic) {
    if(diagnostic==NULL || getenv("MINIAS_TRACE_RELAYOUT")==NULL)return;
    fprintf(diagnostic,
            "minias-source-graph:file-reads=%zu sites=%zu expansions=%zu replay-passes=%zu site-drifts=%zu conditional-changes=%zu\n",
            graph->file_reads,graph->site_count,graph->expansion_count,
            replay_passes,graph->ordinal_drifts,graph->conditional_changes);
}

/* The frame owns its lexical lines regardless of which source expansion
 * borrowed its body. The SourceGraph destructor calls this once for the
 * physical input; expansion frames release their own temporary copies. */
void minias_source_lines_destroy(MiniAsSourceLine *lines, size_t count) {
    size_t i;
    for (i = 0U; i < count; ++i) free(lines[i].text);
    free(lines);
}

/* Validate input before lexical expansion; the private SymbolID marker must
 * never be accepted from user source outside a quoted string. */
static bool validate_user_source_private_marker(MiniAs *as,
                                                const char *line,
                                                size_t source_line) {
    MiniAsSourceQuoteState quotes={0};
    const unsigned char *p=(const unsigned char *)line;
    for(;*p!='\0';++p) {
        if(minias_source_quote_consumes(&quotes,(char)*p))continue;
        if(*p==1U) {
            minias_set_error(as,"private-symbol-marker-in-source:line=%zu",
                             source_line);
            return false;
        }
    }
    return true;
}

/* GAS block comments are a physical source lexical state, independent of
 * ISA operand parsing and stable across physical input lines. */
static void strip_gnu_c_comments(char comment_prefix, char *line,
                                 bool *comment_active) {
    MiniAsSourceQuoteState quotes={0};
    size_t i;
    for(i=0U;line[i]!='\0';++i) {
        if(*comment_active) {
            if(line[i]=='*' && line[i+1U]=='/') {
                line[i++]=' ';line[i]=' ';
                *comment_active=false;
            }else if(line[i]!='\n' && line[i]!='\r')line[i]=' ';
            continue;
        }
        if(minias_source_quote_consumes(&quotes,line[i]))continue;
        if((comment_prefix=='/' &&
            line[i]=='/' && line[i+1U]=='/') ||
           (comment_prefix!='/' &&
            line[i]==comment_prefix))
            return;
        if(line[i]=='/' && line[i+1U]=='*') {
            *comment_active=true;
            line[i++]=' ';line[i]=' ';
        }
    }
}

/* SourceGraph is the only owner of the normalized physical SourceFrame.
 * A layout-sensitive reparse reuses it; no second FILE reader is involved. */
bool minias_source_graph_load_physical(MiniAs *as, const char *path,
                                      MiniAsSourceGraph *graph) {
    FILE *file;
    char *linebuf=NULL;
    size_t line_capacity=0U;
    size_t line_no=0U;
    bool ok=true;
    bool comment_active=false;
    MiniAsSourceFrame *frame;
    if(graph==NULL || graph->file_reads!=0U ||
       graph->physical.lines!=NULL) {
        minias_set_error(as,"invalid-source-graph-load");
        return false;
    }
    file=fopen(path,"r");
    if(file==NULL) {
        minias_set_error(as,"input-open:%s",path);
        return false;
    }
    frame=&graph->physical;
    frame->kind=MINIAS_SOURCE_FRAME_FILE;
    /* Reuse one dynamically sized physical-line buffer for the whole file.
     * Fixed 64 KiB fgets chunks rejected otherwise valid GNU source lines. */
    while(getline(&linebuf,&line_capacity,file)>=0) {
        MiniAsSourceLine *line;
        ++line_no;
        if(!validate_user_source_private_marker(as,linebuf,line_no)) {
            ok=false;break;
        }
        strip_gnu_c_comments(minias_source_target_comment_prefix(as),
                             linebuf,&comment_active);
        if(!minias_grow_array((void **)&frame->lines,
                              &frame->capacity,sizeof(*frame->lines),
                              frame->count+1U)) {
            minias_set_error(as,"out-of-memory:source-frame");
            ok=false;break;
        }
        line=&frame->lines[frame->count];
        line->text=minias_strdup(linebuf);
        if(line->text==NULL) {
            minias_set_error(as,"out-of-memory:source-frame-line");
            ok=false;break;
        }
        line->line=line_no;
        line->source_segment=0U;
        ++frame->count;
    }
    /* getline can return -1 on allocation failure without EOF. */
    if(ok && (ferror(file) || !feof(file))) {
        minias_set_error(as,"input-read:%s",path);
        ok=false;
    }
    free(linebuf);
    fclose(file);
    if(ok && comment_active) {
        minias_set_error(as,"unterminated-gnu-block-comment");
        ok=false;
    }
    if(ok)++graph->file_reads;
    return ok;
}


static size_t source_site_hash(MiniAsSourceSiteKey key) {
    size_t hash=(size_t)UINT64_C(0x9e3779b97f4a7c15);
    const size_t values[]={key.parent,key.definition,key.origin_line,
                           key.origin_segment,key.iteration,(size_t)key.kind};
    size_t i;
    for(i=0U;i<sizeof(values)/sizeof(values[0]);++i) {
        size_t item=values[i];
        item^=item>>15U;
        item*=((size_t)UINT32_C(0x85ebca6b));
        item^=item>>13U;
        hash^=item+(size_t)UINT32_C(0x9e3779b9)+(hash<<6U)+(hash>>2U);
    }
    return hash;
}

static bool source_site_same(MiniAsSourceSiteKey a,
                             MiniAsSourceSiteKey b) {
    return a.parent==b.parent && a.definition==b.definition &&
           a.origin_line==b.origin_line &&
           a.origin_segment==b.origin_segment &&
           a.iteration==b.iteration && a.kind==b.kind;
}

static bool source_graph_rehash(MiniAsSourceGraph *graph,size_t capacity) {
    size_t *slots=calloc(capacity,sizeof(*slots));
    size_t i;
    if(slots==NULL)return false;
    for(i=0U;i<graph->site_count;++i) {
        size_t at=source_site_hash(graph->sites[i].key)&(capacity-1U);
        while(slots[at]!=0U)at=(at+1U)&(capacity-1U);
        slots[at]=i+1U;
    }
    free(graph->site_slots);
    graph->site_slots=slots;
    graph->site_slot_capacity=capacity;
    return true;
}

bool minias_source_graph_intern(MiniAs *as,MiniAsSourceSiteKey key,
                                MiniAsStableSiteId *site_id) {
    MiniAsSourceGraph *graph=minias_source_session(as)->graph;
    size_t slot,probes;
    size_t hashed=source_site_hash(key);
    if(graph==NULL || site_id==NULL ||
       key.parent>graph->site_count ||
       key.definition>graph->site_count ||
       graph->site_count==SIZE_MAX) {
        minias_set_error(as,"invalid-source-site-key");return false;
    }
    /* Most source-site visits are repeats across complete semantic replays.
     * Probe first so a pure lookup cannot accidentally trigger a costly
     * growth/rehash exactly at the half-full threshold. */
    if(graph->site_slot_capacity!=0U) {
        slot=hashed&(graph->site_slot_capacity-1U);
        for(probes=0U;probes<graph->site_slot_capacity;++probes) {
            size_t id=graph->site_slots[slot];
            if(id==0U)break;
            if(source_site_same(graph->sites[id-1U].key,key)) {
                *site_id=id;
                return true;
            }
            slot=(slot+1U)&(graph->site_slot_capacity-1U);
        }
    }
    if(graph->site_slot_capacity==0U ||
       graph->site_count+1U>=graph->site_slot_capacity/2U) {
        size_t next=graph->site_slot_capacity==0U?64U:
                    graph->site_slot_capacity*2U;
        if(next<graph->site_slot_capacity ||
           !source_graph_rehash(graph,next)) {
            minias_set_error(as,"out-of-memory:source-site-index");
            return false;
        }
    }
    slot=hashed&(graph->site_slot_capacity-1U);
    for(probes=0U;probes<graph->site_slot_capacity;++probes) {
        if(graph->site_slots[slot]==0U)break;
        slot=(slot+1U)&(graph->site_slot_capacity-1U);
    }
    if(probes==graph->site_slot_capacity ||
       !minias_grow_array((void **)&graph->sites,&graph->site_capacity,
                          sizeof(*graph->sites),graph->site_count+1U)) {
        minias_set_error(as,"out-of-memory:source-site");return false;
    }
    memset(&graph->sites[graph->site_count],0,sizeof(*graph->sites));
    graph->sites[graph->site_count].key=key;
    *site_id=++graph->site_count;
    graph->site_slots[slot]=*site_id;
    if(key.kind==MINIAS_SOURCE_SITE_MACRO ||
       key.kind==MINIAS_SOURCE_SITE_REPT ||
       key.kind==MINIAS_SOURCE_SITE_IRP ||
       key.kind==MINIAS_SOURCE_SITE_INLINE_REPEAT)
        ++graph->expansion_count;
    return true;
}

bool minias_source_graph_line_site(MiniAs *as,
                                   const MiniAsSourceLine *line,
                                   MiniAsStableSiteId *site_id) {
    const MiniAsSourceSiteKey key={
        .parent=minias_source_session(as)->current_expansion_site,
        .origin_line=line->line,
        .origin_segment=line->source_segment,
        .kind=MINIAS_SOURCE_SITE_LINE
    };
    return minias_source_graph_intern(as,key,site_id);
}

/* EOF is a synthetic LINE site with origin_line=file_line_count+1.
 * Keep that identity inside SourceGraph: it must match previous replays
 * exactly even though Common materializes pools and local .comm fragments. */
bool minias_source_graph_begin_eof(MiniAs *as) {
    MiniAsSourceGraph *graph=minias_source_session(as)->graph;
    MiniAsSourceLine eof={0};
    if(graph==NULL || graph->physical.count==SIZE_MAX) {
        minias_set_error(as,"invalid-source-graph-eof");
        return false;
    }
    eof.line=graph->physical.count+1U;
    return minias_source_graph_line_site(as,&eof,&minias_source_session(as)->current_site);
}

/* A fragment without a stable source owner makes forced-branch rebinding
 * unsafe. Keep the full identity proof with the persistent SourceGraph,
 * rather than inspecting its private site cardinality from the lowerer.
 * Source reads one pass-local projection per ordinal; it never borrows a
 * Common Fragment array, mutable MCInst or Target-owned operand payload. */
static bool source_replay_reader_valid(const MiniAsSourceReplayReader *reader) {
    return reader!=NULL && reader->owner!=NULL &&
           reader->fragment!=NULL && reader->operand!=NULL &&
           reader->growth_plan!=NULL;
}

static bool source_replay_fragment(const MiniAsSourceReplayReader *reader,
                                   size_t ordinal,
                                   MiniAsSourceReplayFragment *out) {
    return source_replay_reader_valid(reader) && out!=NULL &&
           ordinal<reader->fragment_count &&
           reader->fragment(reader->owner,ordinal,out);
}

bool minias_source_graph_validate_fragment_sites(
    MiniAs *as,const MiniAsSourceReplayReader *reader) {
    const MiniAsSourceGraph *graph=minias_source_session(as)->graph;
    size_t i;
    if(graph==NULL || !source_replay_reader_valid(reader)) {
        minias_set_error(as,"invalid-source-graph-replay-reader");
        return false;
    }
    for(i=0U;i<reader->fragment_count;++i) {
        MiniAsSourceReplayFragment fragment;
        if(!source_replay_fragment(reader,i,&fragment)) {
            minias_set_error(as,"invalid-source-replay-fragment:%zu",i);
            return false;
        }
        if(fragment.source_site==MINIAS_SITE_NONE ||
           fragment.source_site>graph->site_count) {
            minias_set_error(as,"fragment-without-source-site:%zu",i);
            return false;
        }
    }
    return true;
}

void minias_source_graph_record_condition(MiniAs *as,
                                          MiniAsStableSiteId owner,
                                          bool active) {
    MiniAsSourceGraph *graph=minias_source_session(as)->graph;
    MiniAsSourceSiteEntry *entry;
    if(graph==NULL || minias_source_session(as)->current_site==MINIAS_SITE_NONE ||
       minias_source_session(as)->current_site>graph->site_count)return;
    entry=&graph->sites[minias_source_session(as)->current_site-1U];
    if(entry->last_condition_pass!=0U &&
       entry->last_condition_pass+1U==graph->parse_passes &&
       (entry->last_condition_active!=active ||
        entry->last_condition_owner!=owner))
        ++graph->conditional_changes;
    entry->last_condition_pass=graph->parse_passes;
    entry->last_condition_owner=owner;
    entry->last_condition_active=active;
}

void minias_source_graph_destroy(MiniAsSourceGraph *graph) {
    size_t i;
    if(graph==NULL)return;
    minias_source_lines_destroy(graph->physical.lines,graph->physical.count);
    free(graph->sites);
    free(graph->site_slots);
    for(i=0U;i<graph->forced_branch_count;++i) {
        free(graph->forced_branches[i].op);
        free(graph->forced_branches[i].operands);
    }
    free(graph->forced_branches);
    free(graph);
}

static bool source_graph_forced_shape_matches(
                           const MiniAsForcedBranchSite *saved,
                           const MiniAsSourceReplayReader *reader,
                           size_t ordinal,
                           const MiniAsSourceReplayFragment *fragment) {
    unsigned i;
    if(!fragment->instruction || fragment->op==NULL || saved->op==NULL ||
       fragment->source_site!=saved->source_site ||
       strcmp(fragment->op,saved->op)!=0 ||
       fragment->opcode!=saved->opcode ||
       fragment->target_form!=saved->target_form ||
       fragment->mode_bits!=saved->mode_bits ||
       fragment->provenance_bits!=saved->provenance_bits ||
       fragment->operand_count!=saved->operand_count)return false;
    /* Operand spelling is NOT a stable source identity. GNU 1f/1b refs
     * become private generation names, while macro \@ counters can shift
     * when an earlier conditional inserts a new macro expansion. The same
     * lexical SourceSite and typed opcode/form/register classes remain
     * stable; all symbol values are rebound by the fresh MC parser. */
    for(i=0U;i<fragment->operand_count;++i) {
        MiniAsForcedOperandShape operand;
        const MiniAsForcedOperandShape *shape=&saved->operands[i];
        if(!reader->operand(reader->owner,ordinal,i,&operand) ||
           operand.kind!=shape->kind ||
           operand.reg_class!=shape->reg_class ||
           operand.target_reg_bank!=shape->target_reg_bank ||
           operand.reg!=shape->reg ||
           operand.register_64!=shape->register_64)return false;
    }
    return true;
}

/* Parse-time branch growth has to consult the lexical identity, NOT the
 * statement's position in a newly generated vector. A different instruction
 * at the same site or a reused expansion identity is an error, even when its
 * current branch size happens to match the previous pass. */
bool minias_source_graph_lookup_forced_branch(MiniAs *as,
                                              const MiniAsSourceReplayReader *reader,
                                              size_t fragment_ordinal,
                                              size_t *minimum_size) {
    MiniAsSourceGraph *graph=minias_source_session(as)->graph;
    MiniAsSourceReplayFragment fragment;
    size_t encoded;
    if(minimum_size==NULL)return false;
    *minimum_size=0U;
    if(graph==NULL)return true;
    if(!source_replay_fragment(reader,fragment_ordinal,&fragment) ||
       !fragment.instruction) {
        minias_set_error(as,"invalid-branch-replay-fragment");
        return false;
    }
    if(fragment.source_site==MINIAS_SITE_NONE ||
       fragment.source_site>graph->site_count) {
        minias_set_error(as,"invalid-branch-source-site");
        return false;
    }
    encoded=graph->sites[fragment.source_site-1U].forced_branch_index;
    if(encoded==0U)return true;
    if(encoded>graph->forced_branch_count ||
       !source_graph_forced_shape_matches(&graph->forced_branches[encoded-1U],
                                         reader,fragment_ordinal,&fragment)) {
        ++graph->ordinal_drifts;
        minias_set_error(as,"changed-forced-branch-shape:line=%zu",fragment.line);
        return false;
    }
    *minimum_size=graph->forced_branches[encoded-1U].minimum_size;
    return true;
}

/* Rebind every retained source identity to a fresh, pass-local Fragment ID.
 * One SourceSite may not ambiguously denote multiple machine instructions.
 * Missing branches (e.g. a changed conditional arm) fail CLOSED rather than
 * forcing an unrelated instruction that inherited its old ordinal. */
bool minias_source_graph_rebind_forced_branches(
    MiniAs *as,const MiniAsSourceReplayReader *reader,
    MiniAsFragmentGrowthSet *growth) {
    MiniAsSourceGraph *graph=minias_source_session(as)->graph;
    size_t *site_to_stmt;
    size_t i;
    if(!source_replay_reader_valid(reader) || growth==NULL) {
        minias_set_error(as,"invalid-source-rebind-reader");
        return false;
    }
    if(graph==NULL || graph->forced_branch_count==0U) {
        growth->count=0U;
        return true;
    }
    if(graph->site_count==SIZE_MAX ||
       graph->site_count+1U>SIZE_MAX/sizeof(*site_to_stmt)) {
        minias_set_error(as,"forced-source-index-overflow");
        return false;
    }
    site_to_stmt=calloc(graph->site_count+1U,sizeof(*site_to_stmt));
    if(site_to_stmt==NULL) {
        minias_set_error(as,"out-of-memory:forced-source-index");
        return false;
    }
    for(i=0U;i<reader->fragment_count;++i) {
        MiniAsSourceReplayFragment fragment;
        size_t *slot;
        if(!source_replay_fragment(reader,i,&fragment)) {
            minias_set_error(as,"invalid-source-replay-fragment:%zu",i);
            free(site_to_stmt);
            return false;
        }
        if(!fragment.instruction)continue;
        if(fragment.source_site==MINIAS_SITE_NONE ||
           fragment.source_site>graph->site_count) {
            minias_set_error(as,"invalid-source-instruction-site");
            free(site_to_stmt);
            return false;
        }
        slot=&site_to_stmt[fragment.source_site];
        *slot=*slot==0U?i+1U:SIZE_MAX;
    }
    if(!minias_grow_array((void **)&growth->ids,&growth->capacity,
                          sizeof(*growth->ids),
                          graph->forced_branch_count)) {
        minias_set_error(as,"out-of-memory:rebound-branch-ids");
        free(site_to_stmt);
        return false;
    }
    for(i=0U;i<graph->forced_branch_count;++i) {
        const MiniAsForcedBranchSite *saved=&graph->forced_branches[i];
        size_t entry=saved->source_site<=graph->site_count ?
            site_to_stmt[saved->source_site] : 0U;
        MiniAsSourceReplayFragment fragment;
        if(entry==0U || entry==SIZE_MAX ||
           !source_replay_fragment(reader,entry-1U,&fragment) ||
           !source_graph_forced_shape_matches(saved,reader,entry-1U,&fragment)) {
            ++graph->ordinal_drifts;
            minias_set_error(as,"lost-or-ambiguous-forced-source-site:%zu",
                             (size_t)saved->source_site);
            free(site_to_stmt);
            return false;
        }
        growth->ids[i]=entry-1U;
    }
    free(site_to_stmt);
    growth->count=graph->forced_branch_count;
    return true;
}

bool minias_source_graph_capture_forced_branches(
    MiniAs *as,const MiniAsSourceReplayReader *reader,
    const MiniAsFragmentGrowthSet *growth) {
    MiniAsSourceGraph *graph=minias_source_session(as)->graph;
    const size_t *ids;
    size_t count;
    size_t i;
    if(!source_replay_reader_valid(reader) || growth==NULL ||
       (growth->count!=0U && growth->ids==NULL)) {
        minias_set_error(as,"invalid-source-capture-reader");
        return false;
    }
    ids=growth->ids;
    count=growth->count;
    if(graph==NULL)return true;
    if(count<graph->forced_branch_count) {
        minias_set_error(as,"lost-forced-branch-after-layout");
        return false;
    }
    if(!minias_grow_array((void **)&graph->forced_branches,
                          &graph->forced_branch_capacity,
                          sizeof(*graph->forced_branches),count)) {
        minias_set_error(as,"out-of-memory:forced-source-sites");
        return false;
    }
    for(i=0U;i<count;++i) {
        MiniAsSourceReplayFragment fragment;
        MiniAsForcedBranchSite *saved;
        uint32_t required_size,terminal_size;
        unsigned j;
        if(!source_replay_fragment(reader,ids[i],&fragment) ||
           !fragment.instruction || fragment.op==NULL) {
            minias_set_error(as,"invalid-forced-branch-fragment:%zu",ids[i]);
            return false;
        }
        if(fragment.source_site==MINIAS_SITE_NONE ||
           fragment.source_site>graph->site_count) {
            minias_set_error(as,"invalid-forced-branch-source-site");
            return false;
        }
        if(!reader->growth_plan(reader->owner,ids[i],&required_size,
                                &terminal_size)) {
            /* Common's first-error-wins sink preserves a Target diagnostic
             * when one exists, while giving a failed opaque reader a
             * deterministic error even if its callback was silent. */
            minias_set_error(as,"invalid-forced-branch-growth-plan:line=%zu",
                             fragment.line);
            return false;
        }
        if(i<graph->forced_branch_count) {
            if(!source_graph_forced_shape_matches(&graph->forced_branches[i],
                                                  reader,ids[i],&fragment)) {
                ++graph->ordinal_drifts;
                minias_set_error(as,"changed-forced-branch-order:line=%zu",
                                 fragment.line);
                return false;
            }
            if(required_size>fragment.size &&
               graph->forced_branches[i].minimum_size<terminal_size)
                graph->forced_branches[i].minimum_size=terminal_size;
            continue;
        }
        if(required_size<=fragment.size) {
            minias_set_error(as,"invalid-forced-source-growth:line=%zu",
                             fragment.line);
            return false;
        }
        if(graph->sites[fragment.source_site-1U].forced_branch_index!=0U) {
            ++graph->ordinal_drifts;
            minias_set_error(as,"ambiguous-forced-branch-source-site:line=%zu",
                             fragment.line);
            return false;
        }
        saved=&graph->forced_branches[graph->forced_branch_count];
        memset(saved,0,sizeof(*saved));
        saved->source_site=fragment.source_site;
        saved->op=minias_strdup(fragment.op);
        saved->opcode=fragment.opcode;
        saved->target_form=fragment.target_form;
        saved->mode_bits=fragment.mode_bits;
        saved->provenance_bits=fragment.provenance_bits;
        /* Required is a lower bound; terminal is the actual Target-owned
         * alternative size committed by the MCLayout growth operation. */
        saved->minimum_size=terminal_size;
        saved->operand_count=fragment.operand_count;
        if(saved->operand_count!=0U) {
            saved->operands=calloc(saved->operand_count,
                                   sizeof(*saved->operands));
        }
        if(saved->op==NULL ||
           (saved->operand_count!=0U && saved->operands==NULL)) {
            free(saved->op);free(saved->operands);
            memset(saved,0,sizeof(*saved));
            minias_set_error(as,"out-of-memory:forced-branch-shape");
            return false;
        }
        for(j=0U;j<saved->operand_count;++j) {
            MiniAsForcedOperandShape *shape=&saved->operands[j];
            if(!reader->operand(reader->owner,ids[i],j,shape)) {
                free(saved->op);free(saved->operands);
                memset(saved,0,sizeof(*saved));
                minias_set_error(as,"invalid-forced-branch-operand:%zu:%u",
                                 ids[i],j);
                return false;
            }
        }
        graph->sites[fragment.source_site-1U].forced_branch_index=
            ++graph->forced_branch_count;
    }
    return true;
}

