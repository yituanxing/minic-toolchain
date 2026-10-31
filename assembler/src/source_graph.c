#include "source_graph_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    char quote='\0';
    bool escaped=false;
    const unsigned char *p=(const unsigned char *)line;
    for(;*p!='\0';++p) {
        if(escaped) {escaped=false;continue;}
        if(quote!='\0') {
            if(*p=='\\')escaped=true;
            else if(*p==(unsigned char)quote)quote='\0';
        }else if(*p=='"' || *p=='\'') {
            quote=(char)*p;
        }else if(*p==1U) {
            minias_set_error(as,"private-symbol-marker-in-source:line=%zu",
                             source_line);
            return false;
        }
    }
    return true;
}

/* GAS block comments are a physical source lexical state, independent of
 * ISA operand parsing and stable across physical input lines. */
static void strip_gnu_c_comments(MiniAs *as, char *line) {
    char quote='\0';
    bool escaped=false;
    size_t i;
    for(i=0U;line[i]!='\0';++i) {
        if(as->source->c_comment_active) {
            if(line[i]=='*' && line[i+1U]=='/') {
                line[i++]=' ';line[i]=' ';
                as->source->c_comment_active=false;
            }else if(line[i]!='\n' && line[i]!='\r')line[i]=' ';
            continue;
        }
        if(escaped){escaped=false;continue;}
        if(quote) {
            if(line[i]=='\\')escaped=true;
            else if(line[i]==quote)quote='\0';
            continue;
        }
        if(line[i]=='"' || line[i]=='\'') {
            quote=line[i];continue;
        }
        if((as->target->comment_prefix=='/' &&
            line[i]=='/' && line[i+1U]=='/') ||
           (as->target->comment_prefix!='/' &&
            line[i]==as->target->comment_prefix))
            return;
        if(line[i]=='/' && line[i+1U]=='*') {
            as->source->c_comment_active=true;
            line[i++]=' ';line[i]=' ';
        }
    }
}

/* SourceGraph is the only owner of the normalized physical SourceFrame.
 * A layout-sensitive reparse reuses it; no second FILE reader is involved. */
bool minias_source_graph_load_physical(MiniAs *as, const char *path,
                                      MiniAsSourceGraph *graph) {
    FILE *file;
    char linebuf[65536];
    size_t line_no=0U;
    bool ok=true;
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
    as->source->c_comment_active=false;
    frame->kind=MINIAS_SOURCE_FRAME_FILE;
    while(fgets(linebuf,sizeof(linebuf),file)!=NULL) {
        size_t len;
        MiniAsSourceLine *line;
        ++line_no;
        if(!validate_user_source_private_marker(as,linebuf,line_no)) {
            ok=false;break;
        }
        strip_gnu_c_comments(as,linebuf);
        len=strlen(linebuf);
        if(len==sizeof(linebuf)-1U && linebuf[len-1U]!='\n') {
            minias_set_error(as,"line-too-long:line=%zu",line_no);
            ok=false;break;
        }
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
    if(ok && ferror(file)) {
        minias_set_error(as,"input-read:%s",path);
        ok=false;
    }
    fclose(file);
    if(ok && as->source->c_comment_active) {
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
    MiniAsSourceGraph *graph=as->source->graph;
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
                                   size_t segment,
                                   MiniAsStableSiteId *site_id) {
    const MiniAsSourceSiteKey key={
        .parent=as->source->current_expansion_site,
        .origin_line=line->line,
        .origin_segment=line->source_segment,
        .iteration=segment,
        .kind=segment==0U ? MINIAS_SOURCE_SITE_LINE :
                             MINIAS_SOURCE_SITE_SEGMENT
    };
    return minias_source_graph_intern(as,key,site_id);
}

void minias_source_graph_record_condition(MiniAs *as,
                                          MiniAsStableSiteId owner,
                                          bool active) {
    MiniAsSourceGraph *graph=as->source->graph;
    MiniAsSourceSiteEntry *entry;
    if(graph==NULL || as->source->current_site==MINIAS_SITE_NONE ||
       as->source->current_site>graph->site_count)return;
    entry=&graph->sites[as->source->current_site-1U];
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
    memset(graph,0,sizeof(*graph));
}

static bool source_graph_forced_shape_matches(
                           const MiniAsForcedBranchSite *saved,
                           const MiniAsStmt *stmt) {
    const MiniAsMCInst *inst=stmt->mcinst;
    unsigned i;
    if(stmt->fragment_kind!=MINIAS_FRAGMENT_INSTRUCTION || inst==NULL ||
       stmt->source_site!=saved->source_site ||
       strcmp(stmt->op,saved->op)!=0 ||
       inst->opcode!=saved->opcode ||
       inst->target_form!=saved->target_form ||
       stmt->site.mode_bits!=saved->mode_bits ||
       inst->count>inst->capacity ||
       inst->count!=saved->operand_count)return false;
    /* Operand spelling is NOT a stable source identity. GNU 1f/1b refs
     * become private generation names, while macro \@ counters can shift
     * when an earlier conditional inserts a new macro expansion. The same
     * lexical SourceSite and typed opcode/form/register classes remain
     * stable; all symbol values are rebound by the fresh MC parser. */
    for(i=0U;i<inst->count;++i) {
        const MiniAsMCOperand *operand=&inst->operands[i];
        const MiniAsForcedOperandShape *shape=&saved->operands[i];
        if(operand->kind!=shape->kind ||
           operand->reg_class!=shape->reg_class ||
           operand->target_reg_bank!=shape->target_reg_bank ||
           operand->reg!=shape->reg ||
           operand->register_64!=shape->register_64)return false;
    }
    return true;
}

/* Parse-time branch growth has to consult the lexical identity, NOT the
 * statement's position in a newly generated vector. A different instruction
 * at the same site or a reused expansion identity is an error, even when its
 * current branch size happens to match the previous pass. */
bool minias_source_graph_lookup_forced_branch(MiniAs *as,
                                              const MiniAsStmt *stmt,
                                              size_t *minimum_size) {
    MiniAsSourceGraph *graph=as->source->graph;
    size_t encoded;
    *minimum_size=0U;
    if(graph==NULL)return true;
    if(stmt->source_site==MINIAS_SITE_NONE ||
       stmt->source_site>graph->site_count) {
        minias_set_error(as,"invalid-branch-source-site");
        return false;
    }
    encoded=graph->sites[stmt->source_site-1U].forced_branch_index;
    if(encoded==0U)return true;
    if(encoded>graph->forced_branch_count ||
       !source_graph_forced_shape_matches(&graph->forced_branches[encoded-1U],
                                         stmt)) {
        ++graph->ordinal_drifts;
        minias_set_error(as,"changed-forced-branch-shape:line=%zu",stmt->line);
        return false;
    }
    *minimum_size=graph->forced_branches[encoded-1U].minimum_size;
    return true;
}

/* Rebind every retained source identity to a fresh, pass-local Fragment ID.
 * One SourceSite may not ambiguously denote multiple machine instructions.
 * Missing branches (e.g. a changed conditional arm) fail CLOSED rather than
 * forcing an unrelated instruction that inherited its old ordinal. */
bool minias_source_graph_rebind_forced_branches(MiniAs *as,
                                                size_t **ids,
                                                size_t *count,
                                                size_t *capacity) {
    MiniAsSourceGraph *graph=as->source->graph;
    size_t *site_to_stmt;
    size_t i;
    if(graph==NULL || graph->forced_branch_count==0U) {
        *count=0U;
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
    for(i=0U;i<as->stmt_count;++i) {
        const MiniAsStmt *stmt=&as->stmts[i];
        size_t *slot;
        if(stmt->fragment_kind!=MINIAS_FRAGMENT_INSTRUCTION)continue;
        if(stmt->source_site==MINIAS_SITE_NONE ||
           stmt->source_site>graph->site_count) {
            minias_set_error(as,"invalid-source-instruction-site");
            free(site_to_stmt);
            return false;
        }
        slot=&site_to_stmt[stmt->source_site];
        *slot=*slot==0U?i+1U:SIZE_MAX;
    }
    if(!minias_grow_array((void **)ids,capacity,sizeof(**ids),
                          graph->forced_branch_count)) {
        minias_set_error(as,"out-of-memory:rebound-branch-ids");
        free(site_to_stmt);
        return false;
    }
    for(i=0U;i<graph->forced_branch_count;++i) {
        const MiniAsForcedBranchSite *saved=&graph->forced_branches[i];
        size_t entry=saved->source_site<=graph->site_count ?
            site_to_stmt[saved->source_site] : 0U;
        if(entry==0U || entry==SIZE_MAX ||
           !source_graph_forced_shape_matches(saved,&as->stmts[entry-1U])) {
            ++graph->ordinal_drifts;
            minias_set_error(as,"lost-or-ambiguous-forced-source-site:%zu",
                             (size_t)saved->source_site);
            free(site_to_stmt);
            return false;
        }
        (*ids)[i]=entry-1U;
    }
    free(site_to_stmt);
    *count=graph->forced_branch_count;
    return true;
}

bool minias_source_graph_capture_forced_branches(MiniAs *as,
                                                 const size_t *ids,
                                                 size_t count) {
    MiniAsSourceGraph *graph=as->source->graph;
    size_t i;
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
        const MiniAsStmt *stmt;
        MiniAsForcedBranchSite *saved;
        MiniAsTargetRelaxPlan plan;
        unsigned j;
        if(ids[i]>=as->stmt_count ||
           as->stmts[ids[i]].fragment_kind!=MINIAS_FRAGMENT_INSTRUCTION ||
           as->stmts[ids[i]].mcinst==NULL ||
           as->stmts[ids[i]].mcinst->count>
               as->stmts[ids[i]].mcinst->capacity) {
            minias_set_error(as,"invalid-forced-branch-fragment:%zu",ids[i]);
            return false;
        }
        stmt=&as->stmts[ids[i]];
        if(stmt->source_site==MINIAS_SITE_NONE ||
           stmt->source_site>graph->site_count) {
            minias_set_error(as,"invalid-forced-branch-source-site");
            return false;
        }
        if(!minias_query_target_relax_plan(as,stmt,&plan))return false;
        if(i<graph->forced_branch_count) {
            if(!source_graph_forced_shape_matches(&graph->forced_branches[i],
                                                  stmt)) {
                ++graph->ordinal_drifts;
                minias_set_error(as,"changed-forced-branch-order:line=%zu",
                                 stmt->line);
                return false;
            }
            if(plan.required_size>stmt->size &&
               graph->forced_branches[i].minimum_size<plan.terminal_size)
                graph->forced_branches[i].minimum_size=plan.terminal_size;
            continue;
        }
        if(plan.required_size<=stmt->size) {
            minias_set_error(as,"invalid-forced-source-growth:line=%zu",
                             stmt->line);
            return false;
        }
        if(graph->sites[stmt->source_site-1U].forced_branch_index!=0U) {
            ++graph->ordinal_drifts;
            minias_set_error(as,"ambiguous-forced-branch-source-site:line=%zu",
                             stmt->line);
            return false;
        }
        saved=&graph->forced_branches[graph->forced_branch_count];
        memset(saved,0,sizeof(*saved));
        saved->source_site=stmt->source_site;
        saved->op=minias_strdup(stmt->op);
        saved->opcode=stmt->mcinst->opcode;
        saved->target_form=stmt->mcinst->target_form;
        saved->mode_bits=stmt->site.mode_bits;
        /* Required is a lower bound; terminal is the actual Target-owned
         * alternative size committed by the MCLayout growth operation. */
        saved->minimum_size=plan.terminal_size;
        saved->operand_count=stmt->mcinst->count;
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
            const MiniAsMCOperand *operand=&stmt->mcinst->operands[j];
            MiniAsForcedOperandShape *shape=&saved->operands[j];
            shape->kind=operand->kind;
            shape->reg_class=operand->reg_class;
            shape->target_reg_bank=operand->target_reg_bank;
            shape->reg=operand->reg;
            shape->register_64=operand->register_64;
        }
        graph->sites[stmt->source_site-1U].forced_branch_index=
            ++graph->forced_branch_count;
    }
    return true;
}

