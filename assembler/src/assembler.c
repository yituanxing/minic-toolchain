#include "minias_internal.h"
#include "minias_common_lowering.h"
#include "minias_common_context.h"
#include "minias_common_emission.h"
#include "minias_common_layout.h"
#include "minias_elf_object_view.h"
#include "source_graph_common.h"
#include "minias_source_semantic_services.h"
#include "minias.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/* These stack-scoped phase views borrow the live Common owner. Targets see
 * only the opaque handle and scalar/const projections, never a raw Session,
 * Fragment, Section or Symbol array. No shadow object graph is constructed. */
struct MiniAsPreLayoutView { const MiniAs *as; };
struct MiniAsPostEmitView { const MiniAs *as; };

static bool minias_project_fragment(const MiniAs *as,size_t index,
                                    MiniAsFragmentView *out) {
    const MiniAsStmt *stmt;
    if(as==NULL || out==NULL || index>=as->stmt_count)return false;
    stmt=&as->stmts[index];
    *out=(MiniAsFragmentView){
        .kind=stmt->fragment_kind,
        .op=stmt->op,
        .line=stmt->line,
        .section=stmt->section,
        .offset=stmt->offset,
        .size=stmt->size,
        .site=stmt->site
    };
    if(stmt->fragment_kind==MINIAS_FRAGMENT_INSTRUCTION &&
       stmt->payload.instruction.inst!=NULL) {
        out->has_instruction=true;
        out->instruction_opcode=stmt->payload.instruction.inst->opcode;
    }else if(stmt->fragment_kind==MINIAS_FRAGMENT_ALIGNMENT) {
        out->alignment_has_fill=stmt->payload.alignment.has_fill;
    }
    return true;
}

size_t minias_pre_layout_fragment_count(const MiniAsPreLayoutView *view) {
    return view!=NULL && view->as!=NULL?view->as->stmt_count:0U;
}
size_t minias_pre_layout_section_count(const MiniAsPreLayoutView *view) {
    return view!=NULL && view->as!=NULL?view->as->section_count:0U;
}
void *minias_pre_layout_target_state(const MiniAsPreLayoutView *view) {
    return view!=NULL && view->as!=NULL?view->as->target_state:NULL;
}
bool minias_pre_layout_fragment(const MiniAsPreLayoutView *view,size_t index,
                                MiniAsFragmentView *out) {
    return view!=NULL && minias_project_fragment(view->as,index,out);
}

size_t minias_post_emit_fragment_count(const MiniAsPostEmitView *view) {
    return view!=NULL && view->as!=NULL?view->as->stmt_count:0U;
}
size_t minias_post_emit_section_count(const MiniAsPostEmitView *view) {
    return view!=NULL && view->as!=NULL?view->as->section_count:0U;
}
const void *minias_post_emit_target_state(const MiniAsPostEmitView *view) {
    return view!=NULL && view->as!=NULL?view->as->target_state:NULL;
}
bool minias_post_emit_fragment(const MiniAsPostEmitView *view,size_t index,
                               MiniAsFragmentView *out) {
    return view!=NULL && minias_project_fragment(view->as,index,out);
}
bool minias_post_emit_section(const MiniAsPostEmitView *view,size_t index,
                              MiniAsSectionView *out) {
    const MiniAsSection *section;
    MiniAsEmittedSectionView bytes;
    if(view==NULL || view->as==NULL || out==NULL ||
       index>=view->as->section_count ||
       !minias_emission_section_view(view->as,(int)index,&bytes))return false;
    section=&view->as->sections[index];
    *out=(MiniAsSectionView){
        .name=section->name,
        .type=section->type,
        .flags=section->flags,
        .align=section->align,
        .data=bytes.data,
        .size=bytes.size
    };
    return true;
}

bool minias_grow_array(void **ptr, size_t *capacity, size_t elem_size, size_t need) {
    size_t next;
    void *p;

    if (*capacity >= need) {
        return true;
    }
    next = *capacity == 0U ? 8U : *capacity;
    while (next < need) {
        if (next > SIZE_MAX / 2U) {
            return false;
        }
        next *= 2U;
    }
    if (elem_size == 0U || next > SIZE_MAX / elem_size) {
        return false;
    }
    p = realloc(*ptr, next * elem_size);
    if (p == NULL) {
        return false;
    }
    *ptr = p;
    *capacity = next;
    return true;
}

char *minias_strdup(const char *text) {
    size_t n = strlen(text) + 1U;
    char *copy = malloc(n);

    if (copy != NULL) {
        memcpy(copy, text, n);
    }
    return copy;
}

bool minias_is_private_symbol_name(const char *name) {
    return name!=NULL && (unsigned char)name[0]==1U &&
           strncmp(name+1,"minias_",7U)==0;
}

bool minias_make_private_symbol_name(char *out,size_t capacity,
                                     const char *kind,size_t ordinal) {
    size_t i;
    int n;
    if(out==NULL || capacity==0U || kind==NULL || *kind=='\0')return false;
    for(i=0U;kind[i]!='\0';++i) {
        if(!((kind[i]>='a' && kind[i]<='z') ||
             (kind[i]>='A' && kind[i]<='Z') ||
             (kind[i]>='0' && kind[i]<='9') || kind[i]=='_'))
            return false;
    }
    n=snprintf(out,capacity,"\001minias_%s_%zu",kind,ordinal);
    return n>0 && (size_t)n<capacity;
}

char *minias_trim(char *text) {
    char *end;

    while (*text != '\0' && isspace((unsigned char)*text)) {
        ++text;
    }
    end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) {
        --end;
    }
    *end = '\0';
    return text;
}

/* The post-emit sinks intentionally expose no MiniAs* to a Target. Common
 * alone retains the writable object graph behind these opaque handles. */
struct MiniAsPostEmitErrorSink { MiniAs *as; };
struct MiniAsCfiMappingSink { MiniAs *as; };

void minias_set_error_va(MiniAs *as, const char *format, va_list ap) {
    if (as->error[0] != '\0') {
        return;
    }
    (void)vsnprintf(as->error, sizeof(as->error), format, ap);
}

void minias_set_error(MiniAs *as, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    minias_set_error_va(as, format, ap);
    va_end(ap);
}

void *minias_directive_target_state(MiniAsDirectiveContext *ctx) {
    return ctx->as->target_state;
}

bool minias_directive_section_executable(const MiniAsDirectiveContext *ctx) {
    const MiniAs *as=ctx->as;
    return as->current_section>=0 &&
           (size_t)as->current_section<as->section_count &&
           (as->sections[(size_t)as->current_section].flags &
            MINIAS_SHF_EXECINSTR)!=0U;
}

bool minias_directive_has_prior_instruction(const MiniAsDirectiveContext *ctx) {
    const MiniAs *as=ctx->as;
    size_t i;
    for(i=0U;i<as->stmt_count;++i)
        if(as->stmts[i].fragment_kind==MINIAS_FRAGMENT_INSTRUCTION)
            return true;
    return false;
}

bool minias_directive_parse_absolute_data_bits(MiniAsDirectiveContext *ctx,
                                               const char *text,uint64_t *out) {
    return minias_parse_absolute_data_bits(ctx->as,text,out);
}

void minias_directive_set_error(MiniAsDirectiveContext *ctx,
                                const char *format, ...) {
    va_list ap;
    va_start(ap,format);
    minias_set_error_va(ctx->as,format,ap);
    va_end(ap);
}

bool minias_cfi_mapping_define(MiniAsCfiMappingSink *sink,const char *stem,
                               bool always_suffix,int section,uint64_t offset) {
    MiniAs *as=sink->as;
    return minias_define_target_mapping_symbol(as,stem,as->symbol_count,
                                               always_suffix,section,offset);
}

bool minias_cfi_record_target_mapping(MiniAs *as,int section,uint64_t offset) {
    MiniAsCfiMappingSink sink={as};
    return as->target->record_cfi_data_mapping==NULL ||
           as->target->record_cfi_data_mapping(&sink,section,offset);
}

void minias_post_emit_error(MiniAsPostEmitErrorSink *sink,
                            const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    minias_set_error_va(sink->as, format, ap);
    va_end(ap);
}

void minias_measure_error(MiniAsMeasureErrorSink *sink,
                          const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    minias_set_error_va(sink->as, format, ap);
    va_end(ap);
}

const void *minias_parse_target_state(const MiniAsParseContext *ctx) {
    return ctx->as->target_state;
}

bool minias_parse_is_elf32(const MiniAsParseContext *ctx) {
    return ctx->as->target_spec->object.elf32;
}

bool minias_parse_has_error(const MiniAsParseContext *ctx) {
    return ctx->as->error[0] != '\0';
}

void minias_parse_error(MiniAsParseContext *ctx, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    minias_set_error_va(ctx->as, format, ap);
    va_end(ap);
}

bool minias_parse_bind_symbol_ref(MiniAsParseContext *ctx,
                                  const MiniAsSymbolExpr *expr,
                                  MiniAsSymbolRef *ref) {
    return minias_bind_symbol_ref(ctx->as, expr, ref);
}

bool minias_parse_bind_symbol_difference_ref(
    MiniAsParseContext *ctx, const MiniAsSymbolDifference *expr,
    MiniAsSymbolDifferenceRef *ref) {
    return minias_bind_symbol_difference_ref(ctx->as, expr, ref);
}

bool minias_parse_symbol_pair_same_defined_section(
    MiniAsParseContext *ctx, MiniAsSymbolRef positive,
    MiniAsSymbolRef negative) {
    MiniAsSymbolView lhs,rhs;
    return minias_common_context_symbol_ref_view(ctx->as,positive,&lhs) &&
           minias_common_context_symbol_ref_view(ctx->as,negative,&rhs) &&
           lhs.defined && rhs.defined && lhs.section>=0 &&
           lhs.section==rhs.section;
}

bool minias_parse_eval_absolute_data_bits(MiniAsParseContext *ctx,
                                          const char *text, uint64_t *bits) {
    return minias_parse_absolute_data_bits(ctx->as, text, bits);
}

bool minias_parse_evaluate_absolute_operand(MiniAsParseContext *ctx,
                                            const char *text, int64_t *value) {
    return minias_evaluate_absolute_expression(ctx->as, text, value);
}

bool minias_parse_eval_symbol_difference_absolute_tail(
    MiniAsParseContext *ctx, const char *text,
    MiniAsSymbolDifference *difference) {
    return minias_parse_symbol_difference_absolute_tail(
        ctx->as, text, difference);
}

bool minias_parse_queue_literal(MiniAsParseContext *ctx,
                                const MiniAsSymbolExpr *expr,
                                unsigned width,
                                MiniAsSymbolExpr *pool_label) {
    return minias_queue_literal(ctx->as, expr, width, pool_label);
}

/* Context construction is one fail-closed transaction. Earlier code
 * ignored section/subsection allocations, then invoked Target initialization
 * against a partial Common object. A failed init is destroyed by the caller
 * without parsing or emitting an output ELF. */
static bool minias_init_for_target(MiniAs *as,
                                   const MiniAsTargetSpec *spec) {
    const MiniAsTargetOps *target=spec->target;
    int text;
    memset(as,0,sizeof(*as));
    as->target=target;
    as->target_spec=spec;
    if(target==NULL) {
        minias_set_error(as,"missing-assembler-target");
        return false;
    }
    if(!minias_common_emission_init(as))return false;
    if(!minias_common_context_init(as) ||
       !minias_common_lowering_init(as) ||
       !minias_layout_init(as))return false;
    as->source=minias_source_context_create();
    if(as->source==NULL) {
        minias_set_error(as,"out-of-memory:source-context");
        return false;
    }
    text=minias_ensure_section(as,".text",MINIAS_SHT_PROGBITS,
            MINIAS_SHF_ALLOC|MINIAS_SHF_EXECINSTR,
            target->default_exec_alignment!=0U ?
            target->default_exec_alignment:4U);
    if(text<0 || minias_ensure_section(as,".data",MINIAS_SHT_PROGBITS,
                     MINIAS_SHF_ALLOC|MINIAS_SHF_WRITE,1U)<0 ||
       minias_ensure_section(as,".bss",MINIAS_SHT_NOBITS,
                     MINIAS_SHF_ALLOC|MINIAS_SHF_WRITE,1U)<0)
        return false;
    as->current_section=text;
    as->current_subsection=0U;
    as->previous_section=text;
    as->previous_subsection=0U;
    as->debug_is_stmt=true;
    return minias_layout_touch_subsection(as,text,0U);
}

uint64_t minias_target_exec_alignment(const MiniAs *as) {
    unsigned align=as->target->default_exec_alignment_for_site!=NULL ?
        as->target->default_exec_alignment_for_site(as->target_state) : 0U;
    if(align==0U)align=as->target->default_exec_alignment;
    return align!=0U ? align : 4U;
}

static void minias_destroy(MiniAs *as) {
    size_t i;
    minias_layout_destroy(as);

    minias_common_context_destroy_sections(as);
    if(as->target && as->target->destroy_target_state)
        as->target->destroy_target_state(as->target_state);
    as->target_state=NULL;
    minias_common_context_destroy_symbols(as);
    minias_common_lowering_destroy(as);
    minias_common_emission_destroy(as);
    minias_source_context_destroy(as->source);
    free(as->cfi_procs);
    for(i=0U;i<as->cfi_op_count;++i)
        free(as->cfi_ops[i].bytes);
    free(as->cfi_ops);
    for (i = 0U; i < as->debug_file_capacity; ++i)
        free(as->debug_files[i]);
    free(as->debug_files);
    for (i = 0U; i < as->cu_file_count; ++i)
        free(as->cu_files[i]);
    free(as->cu_files);
    free(as->debug_locs);
}

/* Read-only .ifdef/.ifndef query: a declaration or relocation reference
 * alone cannot create a defined symbol in the Source conditional grammar. */
/* Source sees only its own pass state and immutable lexical policy.
 * Common retains the sole mutable MiniAs session definition. */
MiniAsSourceContext *minias_source_session(MiniAs *as) {
    return as->source;
}
const MiniAsSourceContext *minias_source_const_session(const MiniAs *as) {
    return as->source;
}
char minias_source_target_comment_prefix(const MiniAs *as) {
    return as->target->comment_prefix;
}
bool minias_source_target_preserve_macro_parentheses(const MiniAs *as) {
    return as->target->macro_preserve_parenthesized_operand;
}
bool minias_source_target_evaluate_if(const MiniAs *as,
                                      const char *expression,bool *result) {
    return as->target->evaluate_target_if!=NULL &&
           as->target->evaluate_target_if(expression,result);
}

bool minias_common_source_symbol_defined(MiniAs *as,const char *name) {
    MiniAsSymbolView symbol;
    return minias_common_context_find_symbol_view(as,name,&symbol) &&
           (symbol.defined || symbol.alias_pending);
}

/* Finalization is the only phase allowed to raise Target-driven ELF Section
 * alignment. Reset the capability on failure as well as success. */
static bool minias_finalize_target_output(
    MiniAs *as, const MiniAsPostEmitView *layout,
    MiniAsPostEmitErrorSink *errors, MiniAsPostEmitObjectSink *object) {
    bool ok;
    if(as->target->finalize_output==NULL)return true;
    as->finalizing_target_output=true;
    ok=as->target->finalize_output(layout,errors,object);
    as->finalizing_target_output=false;
    return ok;
}

/* The Driver publishes one terminal ObjectModel borrow after Target
 * finalization. ELF cannot inspect or mutate the live Common session:
 * symbolic identity remains Context-owned and bytes remain Emission-owned. */
static const MiniAsSymbol *minias_elf_find_symbol(const void *reader,
                                                 const char *name) {
    return minias_common_context_find_symbol(reader,name);
}

static const char *minias_elf_cu_file(const void *reader,size_t index) {
    const MiniAs *as=reader;
    return index<as->cu_file_count?as->cu_files[index]:NULL;
}

static int minias_elf_find_section(const void *reader,const char *name) {
    return minias_find_section(reader,name);
}

static bool minias_elf_fragment_location(
    const void *reader,size_t index,MiniAsElfFragmentLocation *out) {
    const MiniAs *as=reader;
    const MiniAsStmt *stmt;
    if(as==NULL || out==NULL || index>=as->stmt_count)return false;
    stmt=&as->stmts[index];
    *out=(MiniAsElfFragmentLocation){
        .section=stmt->section,.offset=stmt->offset,
        .size=stmt->size,.line=stmt->line
    };
    return true;
}

static bool minias_elf_section_bytes(const void *reader,int section,
                                     const unsigned char **data,size_t *size) {
    MiniAsEmittedSectionView bytes;
    if(data==NULL || size==NULL ||
       !minias_emission_section_view(reader,section,&bytes))return false;
    *data=bytes.data;
    *size=bytes.size;
    return true;
}

static bool minias_write_final_object(MiniAs *as,const char *path,bool elf32) {
    const MiniAsFixupSpan fixups=minias_emission_fixups(as);
    MiniAsElfObjectView object={0};
    bool ok;
    object.sections=as->sections;
    object.section_count=as->section_count;
    object.symbols=as->symbols;
    object.symbol_count=as->symbol_count;
    object.fixups=fixups.data;
    object.fixup_count=fixups.count;
    object.cu_file_count=as->cu_file_count;
    object.cu_file=minias_elf_cu_file;
    object.policy=(MiniAsElfTargetPolicy){
        .elf_machine=as->target->elf_machine,
        .emit_all_section_symbols=as->target->emit_all_section_symbols,
        .fold_defined_local_relocation_to_section=
            as->target->fold_defined_local_relocation_to_section,
        .fold_local_pcrel_relocation_to_section=
            as->target->fold_local_pcrel_relocation_to_section,
        .fold_synthetic_local_relocation_to_section=
            as->target->fold_synthetic_local_relocation_to_section,
        .fold_local_abs_relocation_to_section=
            as->target->fold_local_abs_relocation_to_section,
        .allow_local_section_symbol_fixup=
            as->target->allow_local_section_symbol_fixup,
        .supports_fixup=as->target->supports_fixup,
        .fixup_relocation_type=as->target->fixup_relocation_type,
        .object_attributes=as->target->object_attributes
    };
    object.target_abi=minias_target_abi_view(as);
    object.object_machine=as->target_spec->object.machine;
    object.output_rel_relocations=as->target_spec->object.rel_relocations;
    object.elf_flags=as->elf_flags;
    object.reader=as;
    object.find_symbol=minias_elf_find_symbol;
    object.find_section=minias_elf_find_section;
    object.fragment_location=minias_elf_fragment_location;
    object.section_bytes=minias_elf_section_bytes;
    ok=elf32?minias_write_elf32(&object,path):
             minias_write_elf64(&object,path);
    if(!ok)minias_set_error(as,"%s",object.error[0]!='\0'?
                            object.error:"elf-object-write-failed");
    return ok;
}

int minias_assemble_config(const char *input_path,
                           const char *output_path,
                           const MiniAsTargetSpec *config,
                           FILE *diagnostic) {
    MiniAsFragmentGrowthSet branch_growth={0};
    size_t source_reparse_pass = 0U;
    MiniAsSourceGraph *graph;
    int rc = 1;

    if(config==NULL) {
        if(diagnostic!=NULL)
            fprintf(diagnostic,"minic-as: missing-target-spec\n");
        return 2;
    }
    graph=minias_source_graph_create();
    if(graph==NULL) {
        if(diagnostic!=NULL)
            fprintf(diagnostic,"minic-as: out-of-memory:source-graph\n");
        return 1;
    }

    for (;;) {
        MiniAs as;
        bool parsed;
        bool added_long_branches = false;

        const bool initialized=minias_init_for_target(&as,config);
        as.elf_flags = config->object.initial_elf_flags;
        if (config->profile_size > sizeof(config->profile)) {
            minias_set_error(&as, "invalid-target-profile-size");
        } else if (initialized && as.target->init_target_state != NULL) {
            void *new_target_state=NULL;
            if(!as.target->init_target_state(config,&new_target_state,
                                            as.error,sizeof(as.error))) {
                /* The Target may return a partial allocation but cannot
                 * publish it. Common always owns the failure unwind. */
                if(new_target_state!=NULL &&
                   as.target->destroy_target_state!=NULL)
                    as.target->destroy_target_state(new_target_state);
                if(as.error[0]=='\0')
                    minias_set_error(&as,"target-initialization-failed");
            }else as.target_state=new_target_state;
        }
        parsed=initialized && as.error[0]=='\0';
        if(parsed && as.target->default_exec_alignment_for_site!=NULL) {
            /* Initial .text is created before target ISA state exists.
             * Reconcile its default after profile initialization, before
             * any source fragment can rely on the Section alignment. */
            int text_section=minias_find_section(&as,".text");
            uint64_t align=minias_target_exec_alignment(&as);
            if(text_section<0 || align==0U || (align&(align-1U))!=0U) {
                minias_set_error(&as,"invalid-target-exec-alignment");
                parsed=false;
            } else {
                parsed=minias_common_context_set_section_alignment(
                    &as,text_section,align);
            }
        }
        if(parsed && !minias_source_graph_is_loaded(graph)) {
            parsed=minias_source_graph_load_physical(&as,input_path,graph);
        }
        if(parsed)parsed=minias_common_parse_source_graph(&as,graph);
        if(parsed && as.target->prepare_layout!=NULL) {
            const MiniAsPreLayoutView parsed_fragments = { .as=&as };
            parsed=as.target->prepare_layout(&parsed_fragments,
                                              as.error,sizeof(as.error));
            if(!parsed && as.error[0]=='\0')
                minias_set_error(&as,"target-layout-preparation-failed");
            if(as.error[0]!='\0')parsed=false;
        }
        const MiniAsSourceReplayReader replay=
            minias_common_source_replay_reader(&as);
        if(parsed)parsed=minias_source_graph_rebind_forced_branches(
                &as,&replay,&branch_growth);
        if(parsed && !minias_solve_fragment_layout(
              &as,&branch_growth,&added_long_branches,diagnostic))
            parsed=false;
        if(parsed && added_long_branches) {
            /* Layout may have materialized more Fragments. Re-snapshot the
             * borrowed count just as the old live-vector API did, while
             * keeping the Source boundary scalar-only. */
            const MiniAsSourceReplayReader after_layout=
                minias_common_source_replay_reader(&as);
            parsed=minias_source_graph_capture_forced_branches(
                &as,&after_layout,&branch_growth);
        }
        if(parsed && added_long_branches) {
            ++source_reparse_pass;
            /* Even adversarial conditional/macro feedback cannot keep
             * rebuilding whole semantic states without a finite bound. */
            if(source_reparse_pass>1024U) {
                minias_set_error(&as,"source-replay-nonconvergent");
                parsed=false;
            }
        }
        if(parsed && added_long_branches) {
            /* The successful pass prints reparse=0 because it is an in-place
             * layout trace.  Report the actual exceptional source replay
             * here, without changing the protected .if/.set semantics. */
            if (diagnostic != NULL && getenv("MINIAS_TRACE_RELAYOUT") != NULL)
                fprintf(diagnostic,"minias-source-reparse:pass=%zu\n",
                        source_reparse_pass);
            minias_destroy(&as);
            continue;
        }
        if (parsed && as.error[0] == '\0' && minias_emit_sections(&as)) {
            const MiniAsPostEmitView final_layout = { .as=&as };
            MiniAsPostEmitErrorSink errors = { &as };
            MiniAsPostEmitObjectSink object = { &as };
            if ((as.target->validate_output == NULL ||
                 as.target->validate_output(&final_layout, &errors)) &&
                minias_finalize_target_output(&as,&final_layout,&errors,&object) &&
                minias_write_final_object(&as,output_path,config->object.elf32)) {
                rc = 0;
            }
        }
        if (rc != 0 && diagnostic != NULL) {
            fprintf(diagnostic,
                    "minic-as: %s\n",
                    as.error[0] == '\0' ? "unknown-error" : as.error);
        }
        minias_destroy(&as);
        break;
    }
    minias_source_graph_trace(graph,source_reparse_pass,diagnostic);
    minias_source_graph_destroy(graph);
    free(branch_growth.ids);
    return rc;
}

int minias_assemble_file_target_isa(const char *input_path,
                                    const char *output_path,
                                    bool elf32,
                                    uint32_t elf_flags,
                                    unsigned strict_i_xlen,
                                    FILE *diagnostic) {
    MiniAsTargetSpec config={0};
    config.target=minias_target_riscv();
    config.object.elf32=elf32;
    config.object.initial_elf_flags=elf_flags;
    if(strict_i_xlen!=0U &&
       (config.target->configure_legacy_target==NULL ||
        !config.target->configure_legacy_target(&config,strict_i_xlen))) {
        if(diagnostic!=NULL)
            fprintf(diagnostic,"minic-as: unsupported-legacy-target-configuration\n");
        return 1;
    }
    return minias_assemble_config(input_path,output_path,&config,diagnostic);
}

/* Historical API stays permissive. ISA-feature checks belong to invocations
 * with an explicit -march; the stronger Base-I byte audit is an opt-in here. */
int minias_assemble_file_target(const char *input_path,
                                const char *output_path,
                                bool elf32,
                                uint32_t elf_flags,
                                FILE *diagnostic) {
    return minias_assemble_file_target_isa(
        input_path, output_path, elf32, elf_flags, false, diagnostic);
}

int minias_assemble_file_class(const char *input_path,
                               const char *output_path,
                               bool elf32,
                               FILE *diagnostic) {
    return minias_assemble_file_target(input_path,
                                       output_path,
                                       elf32,
                                       0U,
                                       diagnostic);
}

int minias_assemble_file(const char *input_path,
                         const char *output_path,
                         FILE *diagnostic) {
    return minias_assemble_file_class(input_path, output_path, false, diagnostic);
}
