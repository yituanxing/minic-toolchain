#include "minias_internal.h"
#include "minias_common_lowering.h"
#include "source_graph_common.h"
#include "minias_source_semantic_services.h"
#include "minias.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

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

bool minias_current_location(MiniAs *as, uint64_t *value) {
    MiniAsSubsection *sub;

    if (as->current_section < 0 ||
        (size_t)as->current_section >= as->section_count) {
        minias_set_error(as, "internal:bad-current-section");
        return false;
    }
    sub = minias_ensure_subsection(as, as->current_section, as->current_subsection);
    if (sub == NULL) {
        return false;
    }
    *value = sub->size;
    return true;
}

bool advance_current_location(MiniAs *as, uint64_t amount) {
    MiniAsSubsection *sub =
        minias_ensure_subsection(as, as->current_section, as->current_subsection);

    if (sub == NULL) {
        return false;
    }
    if (amount > UINT64_MAX - sub->size) {
        minias_set_error(as, "subsection-size-overflow");
        return false;
    }
    sub->size += amount;
    return true;
}

/* SymbolID edges remain retained across every MCFragment relayout. Resolve
 * one edge only after its target has been normalized in the current pass.
 * This has no ISA knowledge: section-relative aliases and section-cancelled
 * absolute aliases share the same dependency graph and overflow contract. */
static bool resolve_symbol_alias_edge(MiniAs *as, size_t alias_id) {
    MiniAsSymbol *alias = &as->symbols[alias_id];
    const MiniAsSymbol *target = &as->symbols[alias->alias_target_index];
    uint64_t value = target->value;

    if (alias->alias_section_base != NULL) {
        int target_section = minias_find_section(as, alias->alias_section_base);
        int64_t absolute;
        if (target_section < 0 || target->section != target_section ||
            value > INT64_MAX ||
            !minias_checked_add_i64((int64_t)value,
                                     alias->alias_addend, &absolute)) {
            minias_set_error(as, "invalid-section-relative-set:%s:line=%zu",
                             alias->name, alias->alias_line);
            return false;
        }
        alias->defined = true;
        alias->section = MINIAS_SECTION_ABS;
        alias->subsection = 0U;
        alias->value = (uint64_t)absolute;
        alias->type = MINIAS_STT_NOTYPE;
        alias->alias_pending = false;
        return true;
    }
    /* SHN_ABS aliases are integer expressions, not section offsets.
     * GNU GAS keeps their full 64-bit two's-complement bit pattern: a
     * forward `zero-2` is -2 and even UINT64_MAX+1 wraps to zero.  The
     * nonnegative address/overflow rule below applies only to symbols
     * owned by real sections, where an underflow cannot be an address. */
    if (target->section == MINIAS_SECTION_ABS) {
        value += (uint64_t)alias->alias_addend;
    } else if (alias->alias_addend >= 0) {
        uint64_t addend = (uint64_t)alias->alias_addend;
        if (addend > UINT64_MAX - value) {
            minias_set_error(as, "set-value-overflow:%s:line=%zu",
                             alias->name, alias->alias_line);
            return false;
        }
        value += addend;
    } else {
        uint64_t magnitude = (uint64_t)(-(alias->alias_addend + 1)) + 1U;
        if (magnitude > value) {
            minias_set_error(as, "set-value-underflow:%s:line=%zu",
                             alias->name, alias->alias_line);
            return false;
        }
        value -= magnitude;
    }
    alias->defined = true;
    alias->section = target->section;
    alias->subsection = target->subsection;
    alias->value = value;
    alias->type = target->type;
    if (alias->size == 0U) alias->size = target->size;
    alias->alias_pending = false;
    return true;
}

/* Iterative topological evaluation, O(symbols + alias edges). The former
 * repeated full-symbol scan was O(symbols * maximum alias-chain depth) and
 * ran AGAIN for every branch/deferred-data relayout pass. A 4096-entry GNU
 * macro alias chain could therefore dominate assembling a whole C file.
 *
 * States belong to this evaluation pass, not the parser: each later MC
 * relayout invalidates every dependent value and evaluates the same graph
 * against new symbol anchors. The explicit DFS path bounds stack usage and
 * rejects cycles/undefined targets without recursive C call depth. */
bool minias_refresh_symbol_aliases(MiniAs *as) {
    const size_t count = as->symbol_count;
    unsigned char *state;
    size_t *path;
    size_t i;
    bool result = false;

    if (count == 0U) return true;
    if (count > SIZE_MAX / sizeof(*path)) {
        minias_set_error(as, "symbol-alias-graph-capacity-overflow");
        return false;
    }
    state = calloc(count, sizeof(*state));
    path = malloc(count * sizeof(*path));
    if (state == NULL || path == NULL) {
        minias_set_error(as, "out-of-memory:symbol-alias-graph");
        goto done;
    }
    for (i = 0U; i < count; ++i) {
        if (as->symbols[i].has_alias_dependency)
            as->symbols[i].alias_pending = true;
    }
    for (i = 0U; i < count; ++i) {
        size_t cursor = i;
        size_t depth = 0U;
        if (!as->symbols[i].has_alias_dependency || state[i] == 2U)
            continue;
        while (as->symbols[cursor].has_alias_dependency &&
               state[cursor] != 2U) {
            const MiniAsSymbol *alias = &as->symbols[cursor];
            if (alias->alias_target_index >= count) {
                minias_set_error(as, "internal:set-target-index:%s", alias->name);
                goto done;
            }
            if (state[cursor] == 1U) {
                minias_set_error(as, "unresolved-set-target:%s:%s:line=%zu",
                                 alias->name,
                                 as->symbols[alias->alias_target_index].name,
                                 alias->alias_line);
                goto done;
            }
            state[cursor] = 1U;
            path[depth++] = cursor;
            cursor = alias->alias_target_index;
        }
        if (!as->symbols[cursor].defined ||
            (as->symbols[cursor].has_alias_dependency &&
             state[cursor] != 2U) || as->symbols[cursor].alias_pending) {
            const MiniAsSymbol *alias = &as->symbols[path[depth-1U]];
            minias_set_error(as, "unresolved-set-target:%s:%s:line=%zu",
                             alias->name, as->symbols[cursor].name,
                             alias->alias_line);
            goto done;
        }
        while (depth != 0U) {
            size_t id = path[--depth];
            if (!resolve_symbol_alias_edge(as, id)) goto done;
            state[id] = 2U;
        }
    }
    result = true;
done:
    free(path);
    free(state);
    return result;
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
struct MiniAsPostEmitObjectSink { MiniAs *as; };
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
    return ctx->as->output_elf32;
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
    const MiniAsSymbol *lhs=minias_symbol_ref_lookup(ctx->as, positive);
    const MiniAsSymbol *rhs=minias_symbol_ref_lookup(ctx->as, negative);
    return lhs!=NULL && rhs!=NULL && lhs->defined && rhs->defined &&
           lhs->section>=0 && lhs->section==rhs->section;
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

/* SectionID is the identity of one output Section throughout parse, layout
 * and ELF serialization. GNU can create several Sections with the same
 * spelling; group signature, LINK_ORDER source anchor and explicit unique ID
 * all distinguish them. The index is immutable across Section array moves.
 * Absence of unique is different from the valid numeric ID zero. */
static MiniAsSectionIdentity section_identity(const MiniAsSection *section) {
    MiniAsSectionIdentity result={section->name,section->group_signature,
                                  section->link_order_reference,
                                  section->has_unique_id,section->unique_id};
    return result;
}

static uint64_t section_key_hash_component(uint64_t hash,const char *text) {
    const unsigned char *p=(const unsigned char *)text;
    hash^=text==NULL ? UINT64_C(0) : UINT64_C(1);
    hash*=UINT64_C(1099511628211);
    if(text!=NULL) {
        while(*p!='\0') {
            hash^=(uint64_t)*p++;
            hash*=UINT64_C(1099511628211);
        }
    }
    hash^=UINT64_C(255); /* tuple boundary */
    return hash*UINT64_C(1099511628211);
}

static size_t section_key_hash(const MiniAsSectionIdentity *key) {
    uint64_t hash=UINT64_C(1469598103934665603);
    uint32_t id=key->unique_id;
    size_t i;
    hash=section_key_hash_component(hash,key->name);
    hash=section_key_hash_component(hash,key->group_signature);
    hash=section_key_hash_component(hash,key->link_order_reference);
    hash^=key->has_unique_id ? UINT64_C(1) : UINT64_C(0);
    hash*=UINT64_C(1099511628211);
    for(i=0U;i<sizeof(id);++i) {
        hash^=(id>>(i*8U))&UINT32_C(0xff);
        hash*=UINT64_C(1099511628211);
    }
    return (size_t)hash;
}

static bool section_key_component_equal(const char *a,const char *b) {
    return a==b || (a!=NULL && b!=NULL && strcmp(a,b)==0);
}

static bool section_key_equal(const MiniAsSection *sec,
                              const MiniAsSectionIdentity *key) {
    return section_key_component_equal(sec->name,key->name) &&
           section_key_component_equal(sec->group_signature,key->group_signature) &&
           section_key_component_equal(sec->link_order_reference,key->link_order_reference) &&
           sec->has_unique_id==key->has_unique_id &&
           (!key->has_unique_id || sec->unique_id==key->unique_id);
}

/* Rehash only on growth, never on lookup. On OOM the original index remains
 * intact, and the pending new Section is not committed. */
static bool minias_rebuild_section_index(MiniAs *as,size_t capacity) {
    size_t *slots=calloc(capacity,sizeof(*slots));
    size_t i;
    if(slots==NULL) {
        minias_set_error(as,"out-of-memory:section-index");
        return false;
    }
    for(i=0U;i<as->section_count;++i) {
        const MiniAsSection *sec=&as->sections[i];
        const MiniAsSectionIdentity key=section_identity(sec);
        size_t slot=section_key_hash(&key)&(capacity-1U);
        while(slots[slot]!=0U)slot=(slot+1U)&(capacity-1U);
        slots[slot]=i+1U;
    }
    free(as->section_slots);
    as->section_slots=slots;
    as->section_slot_capacity=capacity;
    return true;
}

static bool minias_reserve_section_index(MiniAs *as) {
    const size_t cap=as->section_slot_capacity;
    size_t next;
    if(cap!=0U && as->section_count+1U<cap/2U)return true;
    if(cap>SIZE_MAX/2U) {
        minias_set_error(as,"section-index-too-large");
        return false;
    }
    next=cap==0U ? 16U : cap*2U;
    return minias_rebuild_section_index(as,next);
}

static size_t minias_section_index_slot(const MiniAs *as,
                                        const MiniAsSectionIdentity *key) {
    size_t slot=section_key_hash(key)&
                (as->section_slot_capacity-1U);
    /* At most half of the table is occupied. An empty slot always exists. */
    while(as->section_slots[slot]!=0U) {
        const size_t section_id=as->section_slots[slot]-1U;
        if(section_key_equal(&as->sections[section_id],key))
            break;
        slot=(slot+1U)&(as->section_slot_capacity-1U);
    }
    return slot;
}

int minias_find_section_key(const MiniAs *as,
                                   const MiniAsSectionIdentity *key) {
    size_t slot;
    if(as->section_slot_capacity==0U)return -1;
    slot=minias_section_index_slot(as,key);
    return as->section_slots[slot]==0U ? -1 :
           (int)(as->section_slots[slot]-1U);
}

int minias_find_section(const MiniAs *as,const char *name) {
    const MiniAsSectionIdentity key={name,NULL,NULL,false,0U};
    return minias_find_section_key(as,&key);
}

int minias_ensure_section_key(MiniAs *as,
                                     const MiniAsSectionIdentity *key,
                                     uint32_t group_flags,
                                     uint32_t type,uint64_t flags,
                                     uint64_t align) {
    const int found=minias_find_section_key(as,key);
    MiniAsSection *sec;
    size_t slot;
    if(found>=0) {
        sec=&as->sections[(size_t)found];
        if(sec->group_flags!=group_flags) {
            minias_set_error(as,"conflicting-section-group-linkage:%s",key->name);
            return -1;
        }
        /* Preserve GAS first declaration; later conflicting metadata must
         * never silently change ELF section type or discard stored bytes. */
        (void)type;
        (void)flags;
        if(align>sec->align)sec->align=align;
        return found;
    }
    if(as->section_count>=(size_t)INT_MAX) {
        minias_set_error(as,"section-count-too-large");
        return -1;
    }
    if(!minias_reserve_section_index(as))return -1;
    if(!minias_grow_array((void **)&as->sections,&as->section_capacity,
                          sizeof(*as->sections),as->section_count+1U)) {
        minias_set_error(as,"out-of-memory:section");
        return -1;
    }
    sec=&as->sections[as->section_count];
    memset(sec,0,sizeof(*sec));
    sec->name=minias_strdup(key->name);
    if(sec->name==NULL) {
        minias_set_error(as,"out-of-memory:section-name");
        return -1;
    }
    if(key->group_signature!=NULL) {
        sec->group_signature=minias_strdup(key->group_signature);
        if(sec->group_signature==NULL) {
            free(sec->name);sec->name=NULL;
            minias_set_error(as,"out-of-memory:group-signature");
            return -1;
        }
    }
    if(key->link_order_reference!=NULL) {
        sec->link_order_reference=minias_strdup(key->link_order_reference);
        if(sec->link_order_reference==NULL) {
            free(sec->group_signature);sec->group_signature=NULL;
            free(sec->name);sec->name=NULL;
            minias_set_error(as,"out-of-memory:link-order-reference");
            return -1;
        }
    }
    sec->type=type;
    sec->flags=flags;
    sec->group_flags=group_flags;
    sec->has_unique_id=key->has_unique_id;
    sec->unique_id=key->unique_id;
    sec->align=align==0U ? 1U : align;
    /* No failure point remains: publish SectionID and hash entry together. */
    slot=minias_section_index_slot(as,key);
    as->section_slots[slot]=as->section_count+1U;
    ++as->section_count;
    return (int)(as->section_count-1U);
}

int minias_ensure_section(MiniAs *as,
                          const char *name,
                          uint32_t type,
                          uint64_t flags,
                          uint64_t align) {
    const MiniAsSectionIdentity key={name,NULL,NULL,false,0U};
    return minias_ensure_section_key(as,&key,0U,type,flags,align);
}

static size_t symbol_name_hash(const char *name) {
    uint64_t hash = UINT64_C(1469598103934665603);
    const unsigned char *p = (const unsigned char *)name;

    while (*p != '\0') {
        hash ^= (uint64_t)*p++;
        hash *= UINT64_C(1099511628211);
    }
    return (size_t)hash;
}

static bool rebuild_symbol_index(MiniAs *as, size_t minimum_capacity) {
    size_t capacity = 16U;
    size_t *slots;
    size_t i;

    while (capacity < minimum_capacity) {
        if (capacity > SIZE_MAX / 2U) {
            minias_set_error(as, "symbol-index-too-large");
            return false;
        }
        capacity *= 2U;
    }
    slots = calloc(capacity, sizeof(*slots));
    if (slots == NULL) {
        minias_set_error(as, "out-of-memory:symbol-index");
        return false;
    }
    for (i = 0U; i < as->symbol_count; ++i) {
        size_t slot = symbol_name_hash(as->symbols[i].name) & (capacity - 1U);
        while (slots[slot] != 0U) {
            slot = (slot + 1U) & (capacity - 1U);
        }
        slots[slot] = i + 1U;
    }
    free(as->symbol_slots);
    as->symbol_slots = slots;
    as->symbol_slot_capacity = capacity;
    return true;
}

static bool find_symbol_slot(const MiniAs *as,
                             const char *name,
                             size_t *slot_out,
                             size_t *index_out) {
    size_t slot;
    size_t scanned;

    if (as->symbol_slot_capacity == 0U) {
        *slot_out = 0U;
        return false;
    }
    slot = symbol_name_hash(name) & (as->symbol_slot_capacity - 1U);
    for (scanned = 0U; scanned < as->symbol_slot_capacity; ++scanned) {
        size_t encoded = as->symbol_slots[slot];
        if (encoded == 0U) {
            *slot_out = slot;
            return false;
        }
        if (strcmp(as->symbols[encoded - 1U].name, name) == 0) {
            *slot_out = slot;
            *index_out = encoded - 1U;
            return true;
        }
        slot = (slot + 1U) & (as->symbol_slot_capacity - 1U);
    }
    *slot_out = 0U;
    return false;
}

MiniAsSymbol *minias_get_symbol(MiniAs *as, const char *name, bool create) {
    size_t slot = 0U;
    size_t index = 0U;
    MiniAsSymbol *sym;

    if (find_symbol_slot(as, name, &slot, &index)) {
        return &as->symbols[index];
    }
    if (!create) {
        return NULL;
    }
    if (as->symbol_slot_capacity == 0U ||
        as->symbol_count + 1U >= as->symbol_slot_capacity / 2U) {
        size_t next_capacity =
            as->symbol_slot_capacity == 0U ? 16U
                                          : as->symbol_slot_capacity * 2U;
        if (next_capacity < as->symbol_slot_capacity ||
            !rebuild_symbol_index(as, next_capacity)) {
            return NULL;
        }
        if (find_symbol_slot(as, name, &slot, &index)) {
            return &as->symbols[index];
        }
    }
    if (!minias_grow_array((void **)&as->symbols,
                    &as->symbol_capacity,
                    sizeof(*as->symbols),
                    as->symbol_count + 1U)) {
        minias_set_error(as, "out-of-memory:symbol");
        return NULL;
    }
    sym = &as->symbols[as->symbol_count];
    memset(sym, 0, sizeof(*sym));
    sym->name = minias_strdup(name);
    if (sym->name == NULL) {
        minias_set_error(as, "out-of-memory:symbol-name");
        return NULL;
    }
    sym->section = MINIAS_SECTION_UNDEF;
    sym->bind = MINIAS_STB_LOCAL;
    sym->type = MINIAS_STT_NOTYPE;
    sym->visibility = MINIAS_STV_DEFAULT;
    ++as->symbol_count;
    as->symbol_slots[slot] = as->symbol_count;
    return sym;
}

bool minias_define_target_mapping_symbol(MiniAs *as, const char *stem,
                                         size_t first_ordinal,
                                         bool always_suffix,
                                         int section, uint64_t offset) {
    char name[640];
    size_t ordinal = first_ordinal;
    MiniAsSymbol *symbol;
    if (stem == NULL || *stem == '\0' || section < 0 ||
        (size_t)section >= as->section_count) {
        minias_set_error(as, "invalid-target-mapping-symbol");
        return false;
    }
    for (;;) {
        int written = (!always_suffix && ordinal == 0U)
            ? snprintf(name, sizeof(name), "%s", stem)
            : snprintf(name, sizeof(name), "%s.%zu", stem, ordinal);
        if (written < 0 || (size_t)written >= sizeof(name)) {
            minias_set_error(as, "target-mapping-symbol-name-overflow");
            return false;
        }
        if (minias_get_symbol(as, name, false) == NULL) break;
        if (ordinal == SIZE_MAX) {
            minias_set_error(as, "target-mapping-symbol-ordinal-overflow");
            return false;
        }
        ++ordinal;
    }
    symbol = minias_get_symbol(as, name, true);
    if (symbol == NULL) return false;
    symbol->defined = true;
    symbol->section = section;
    symbol->value = offset;
    symbol->bind = MINIAS_STB_LOCAL;
    symbol->type = MINIAS_STT_NOTYPE;
    return true;
}

/* A Target may request this ABI alignment property during object
 * finalization, but Section storage and mutation remain Common-owned. */
static bool minias_require_target_section_alignment(MiniAs *as, int section,
                                                     uint64_t alignment) {
    if(!as->finalizing_target_output || section<0 ||
       (size_t)section>=as->section_count || alignment==0U ||
       (alignment&(alignment-1U))!=0U) {
        minias_set_error(as,"invalid-target-finalization-alignment");
        return false;
    }
    if(as->sections[(size_t)section].align<alignment)
        as->sections[(size_t)section].align=alignment;
    return true;
}

size_t minias_post_emit_symbol_count(const MiniAsPostEmitObjectSink *sink) {
    return sink->as->symbol_count;
}

bool minias_post_emit_define_mapping_symbol(MiniAsPostEmitObjectSink *sink,
                                            const char *stem,
                                            size_t first_ordinal,
                                            bool always_suffix,
                                            int section, uint64_t offset) {
    if (!sink->as->finalizing_target_output) {
        minias_set_error(sink->as, "invalid-target-mapping-phase");
        return false;
    }
    return minias_define_target_mapping_symbol(sink->as, stem, first_ordinal,
                                               always_suffix, section, offset);
}

bool minias_post_emit_raise_section_alignment(MiniAsPostEmitObjectSink *sink,
                                              int section, uint64_t align) {
    /* Reuse the guarded Common alignment policy introduced in the
     * preceding Fixup/finalization batch; do not make a second owner. */
    return minias_require_target_section_alignment(sink->as, section, align);
}

/* Stable parse-time NameIds are intentionally NOT Object SymbolIDs.  A
 * machine operand may name a forward or external symbol during parsing,
 * while the historical ELF symbol order is determined later by definitions
 * and committed fixups.  Interning here may allocate only this name pool;
 * it must never call minias_get_symbol(...,true). */
static bool rebuild_symbol_name_index(MiniAs *as,size_t minimum) {
    size_t capacity=16U,i;
    size_t *slots;
    while(capacity<minimum) {
        if(capacity>SIZE_MAX/2U) {
            minias_set_error(as,"symbol-name-index-too-large");
            return false;
        }
        capacity*=2U;
    }
    slots=calloc(capacity,sizeof(*slots));
    if(!slots) {
        minias_set_error(as,"out-of-memory:symbol-name-index");
        return false;
    }
    for(i=0U;i<as->symbol_name_count;++i) {
        size_t slot=symbol_name_hash(as->symbol_names[i].name)&(capacity-1U);
        while(slots[slot]!=0U)slot=(slot+1U)&(capacity-1U);
        slots[slot]=i+1U;
    }
    free(as->symbol_name_slots);
    as->symbol_name_slots=slots;
    as->symbol_name_slot_capacity=capacity;
    return true;
}

bool minias_symbol_name_intern(MiniAs *as,const char *name,
                               MiniAsSymbolNameId *name_id) {
    size_t slot,scanned;
    if(as==NULL || name==NULL || *name=='\0' || name_id==NULL)return false;
    /* A repeated operand must not trigger a table expansion merely because
     * the pool is near its load threshold.  Name lookup has no side effect. */
    if(as->symbol_name_slot_capacity!=0U) {
        slot=symbol_name_hash(name)&(as->symbol_name_slot_capacity-1U);
        for(scanned=0U;scanned<as->symbol_name_slot_capacity;++scanned) {
            size_t encoded=as->symbol_name_slots[slot];
            if(encoded==0U)break;
            if(strcmp(as->symbol_names[encoded-1U].name,name)==0) {
                *name_id=encoded-1U;
                return true;
            }
            slot=(slot+1U)&(as->symbol_name_slot_capacity-1U);
        }
    }
    if(as->symbol_name_count==SIZE_MAX) {
        minias_set_error(as,"symbol-name-count-overflow");
        return false;
    }
    if(as->symbol_name_slot_capacity==0U ||
       as->symbol_name_count+1U>=as->symbol_name_slot_capacity/2U) {
        size_t next=as->symbol_name_slot_capacity==0U?16U:
                    as->symbol_name_slot_capacity*2U;
        if(next<as->symbol_name_slot_capacity ||
           !rebuild_symbol_name_index(as,next))return false;
    }
    slot=symbol_name_hash(name)&(as->symbol_name_slot_capacity-1U);
    for(scanned=0U;scanned<as->symbol_name_slot_capacity;++scanned) {
        size_t encoded=as->symbol_name_slots[slot];
        if(encoded==0U)break;
        slot=(slot+1U)&(as->symbol_name_slot_capacity-1U);
    }
    if(scanned==as->symbol_name_slot_capacity ||
       as->symbol_name_count==SIZE_MAX ||
       !minias_grow_array((void **)&as->symbol_names,
                          &as->symbol_name_capacity,
                          sizeof(*as->symbol_names),
                          as->symbol_name_count+1U)) {
        minias_set_error(as,"out-of-memory:symbol-name-pool");
        return false;
    }
    as->symbol_names[as->symbol_name_count].name=minias_strdup(name);
    if(as->symbol_names[as->symbol_name_count].name==NULL) {
        minias_set_error(as,"out-of-memory:symbol-name");
        return false;
    }
    *name_id=as->symbol_name_count;
    as->symbol_name_slots[slot]=++as->symbol_name_count;
    return true;
}

bool minias_bind_symbol_ref(MiniAs *as,const MiniAsSymbolExpr *expr,
                            MiniAsSymbolRef *ref) {
    MiniAsSymbolNameId name_id;
    if(expr==NULL || ref==NULL ||
       !minias_symbol_name_intern(as,expr->name,&name_id))return false;
    ref->name_id=name_id;
    ref->addend=expr->addend;
    return true;
}

bool minias_bind_symbol_difference_ref(MiniAs *as,
                                        const MiniAsSymbolDifference *expr,
                                        MiniAsSymbolDifferenceRef *ref) {
    if(expr==NULL || ref==NULL)return false;
    return minias_bind_symbol_ref(as,&expr->positive,&ref->positive) &&
           minias_bind_symbol_ref(as,&expr->negative,&ref->negative);
}

const MiniAsSymbol *minias_symbol_ref_lookup(const MiniAs *as,
                                             MiniAsSymbolRef ref) {
    size_t slot=0U,index=0U;
    const char *name;
    if(as==NULL || ref.name_id>=as->symbol_name_count)return NULL;
    name=as->symbol_names[ref.name_id].name;
    if(name==NULL || !find_symbol_slot(as,name,&slot,&index))return NULL;
    return &as->symbols[index];
}

/* Keep staged MCData Fixup widths identical to the committed Section
 * contract. LoongArch uses 8-bit and 24-bit ADD/SUB pairs as well as
 * 16/32/64-bit relocations; staging must not reject a legal typed Fixup. */
bool minias_record_fixup(MiniAs *as,
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
    if (origin == MINIAS_FIXUP_ORIGIN_FRAGMENT) {
        const MiniAsStmt *owner;
        uint64_t delta;
        if ((as->emitting_fragment && retained_fragment_id!=SIZE_MAX) ||
            (!as->emitting_fragment && retained_fragment_id==SIZE_MAX)) {
            minias_set_error(as, "fixup-without-fragment-owner");
            return false;
        }
        fragment_id = retained_fragment_id!=SIZE_MAX ? retained_fragment_id :
                     as->emitting_fragment_id;
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
               as->emitting_fragment) {
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

    if (!minias_grow_array((void **)&as->fixups,
                    &as->fixup_capacity,
                    sizeof(*as->fixups),
                    as->fixup_count + 1U)) {
        minias_set_error(as, "out-of-memory:fixup");
        return false;
    }
    fixup = &as->fixups[as->fixup_count++];
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

/* The final linker-relaxation predicate needs all Target MCInst fixups,
 * including instructions parsed *after* the ULEB source line. A second
 * retained-fragment commit window allows its paired relocation records to
 * be added after full emission without pretending they are .reloc or DWARF
 * section-owned fixups. The encoded ULEB bytes remain the fragment's own. */
bool minias_add_late_fragment_fixup(MiniAs *as,size_t fragment_id,
                                   MiniAsFixupKind kind,size_t symbol_index,
                                   int64_t addend) {
    const MiniAsStmt *stmt;
    if(as==NULL || as->emitting_fragment || as->committing_fragment ||
       fragment_id>=as->stmt_count || symbol_index>=as->symbol_count) {
        if(as!=NULL)minias_set_error(as,"invalid-late-fragment-fixup");
        return false;
    }
    stmt=&as->stmts[fragment_id];
    if(stmt->fragment_kind!=MINIAS_FRAGMENT_BYTES || stmt->leb_mode!=1U ||
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
    MiniAsEncodeResult *result = as->active_encode_result;
    size_t i,logical_size=0U;
    if (result == NULL || !as->emitting_fragment ||
        owner != &as->stmts[as->emitting_fragment_id]) {
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
    if (result->anchor_count > SIZE_MAX - as->synthetic_anchor_counter ||
        result->fixup_count > SIZE_MAX - as->fixup_count) {
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
    if (!minias_grow_array((void **)&as->fixups, &as->fixup_capacity,
                           sizeof(*as->fixups),
                           as->fixup_count + result->fixup_count)) {
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
    as->committing_fragment=true;
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
            as->committing_fragment=false;
            return false;
        }
    }
    as->committing_fragment=false;
    as->synthetic_anchor_counter += result->anchor_count;
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
           (!stmt->align_has_fill ||
            (as->target->explicit_alignment_fill_is_code_nop &&
             as->target->explicit_alignment_fill_is_code_nop(
                 stmt->align_fill)));
}

static bool minias_stage_instruction_bytes(MiniAs *as,
                                           const void *data, size_t size) {
    MiniAsSection *sec;
    int section_index;
    MiniAsEncodeResult *result = as->active_encode_result;
    if (result == NULL || !as->emitting_fragment) {
        minias_set_error(as, "target-emission-outside-instruction");
        return false;
    }
    const MiniAsStmt *owner;
    if (as->emitting_fragment_id >= as->stmt_count) {
        minias_set_error(as, "instruction-bytes-without-fragment");
        return false;
    }
    owner = &as->stmts[as->emitting_fragment_id];
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

const MiniAsSymbol *minias_encode_ctx_symbol_lookup(
    MiniAsEncodeContext *ctx,MiniAsSymbolRef ref) {
    return minias_symbol_ref_lookup(ctx->as,ref);
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
    return as!=NULL && as->emitting_fragment &&
           as->emitting_fragment_id<as->stmt_count;
}

/* Source-order and final position are Common-owned properties, not private
 * target parser data.  The x86 padding policy queries this site predicate
 * without reaching through MiniAs into neighboring retained statements. */
bool minias_code_align_preceded_by_adjacent_data(const MiniAsCodeAlignSink *sink) {
    const MiniAs *as=sink!=NULL?sink->as:NULL;
    const MiniAsStmt *owner, *previous;
    if(as==NULL || !as->emitting_fragment ||
       as->emitting_fragment_id==0U ||
       as->emitting_fragment_id>=as->stmt_count)return false;
    owner=&as->stmts[as->emitting_fragment_id];
    previous=&as->stmts[as->emitting_fragment_id-1U];
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
    MiniAsEncodeResult *result=as->active_encode_result;
    const MiniAsStmt *owner;
    MiniAsSection *sec=&as->sections[(size_t)section];
    const unsigned char *bytes=(const unsigned char *)data;
    MiniAsFragmentWrite *write;
    size_t i,needed;
    bool borrow;
    if(result==NULL || as->emitting_fragment_id>=as->stmt_count ||
       (size!=0U && data==NULL)) {
        minias_set_error(as,"invalid-fragment-bytes");
        return false;
    }
    owner=&as->stmts[as->emitting_fragment_id];
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
           data==owner->static_data && size==owner->static_data_size;
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
    MiniAsEncodeResult *result=as->active_encode_result;
    const MiniAsStmt *owner;
    MiniAsSection *sec=&as->sections[(size_t)section];
    MiniAsFragmentWrite *write;
    size_t size,i;
    if(result==NULL || as->emitting_fragment_id>=as->stmt_count ||
       width==0U || width>8U || count>SIZE_MAX/width) {
        minias_set_error(as,"invalid-fragment-pattern");
        return false;
    }
    owner=&as->stmts[as->emitting_fragment_id];
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
    if(as->emitting_fragment && !as->committing_fragment)
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
bool minias_section_append_pattern(MiniAs *as,int section_index,
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
    if(as->emitting_fragment && !as->committing_fragment)
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

bool minias_section_append_fill(MiniAs *as,int section_index,
                                        size_t size,uint8_t fill,
                                        bool ignore_nobits_fill) {
    return minias_section_append_pattern(as,section_index,&fill,1U,size,
                                         ignore_nobits_fill);
}

bool minias_section_append_zero(MiniAs *as,int section_index,size_t size) {
    return minias_section_append_fill(as,section_index,size,0U,false);
}

/* Context construction is one fail-closed transaction. Earlier code
 * ignored section/subsection allocations, then invoked Target initialization
 * against a partial Common object. A failed init is destroyed by the caller
 * without parsing or emitting an output ELF. */
static bool minias_init_for_target(MiniAs *as,
                                   const MiniAsTargetOps *target) {
    int text;
    memset(as,0,sizeof(*as));
    as->target=target;
    if(target==NULL) {
        minias_set_error(as,"missing-assembler-target");
        return false;
    }
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
    return minias_ensure_subsection(as,text,0U)!=NULL;
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

    for (i = 0U; i < as->section_count; ++i) {
        free(as->sections[i].name);
        free(as->sections[i].group_signature);
        free(as->sections[i].link_order_reference);
        free(as->sections[i].data);
    }
    free(as->sections);
    free(as->section_slots);
    if(as->target && as->target->destroy_target_state)
        as->target->destroy_target_state(as->target_state);
    as->target_state=NULL;
    for (i = 0U; i < as->symbol_count; ++i) {
        free(as->symbols[i].name);
        free(as->symbols[i].alias_section_base);
    }
    free(as->symbols);
    free(as->symbol_slots);
    for (i = 0U; i < as->symbol_name_count; ++i)
        free(as->symbol_names[i].name);
    free(as->symbol_names);
    free(as->symbol_name_slots);
    for (i = 0U; i < as->stmt_count; ++i) {
        free(as->stmts[i].op);
        free(as->stmts[i].args);
        minias_deferred_expr_destroy(as->stmts[i].org_expr);
        free(as->stmts[i].static_data);
        minias_mcdata_destroy(as->stmts[i].mcdata);
        for (size_t k=0U;k<3U;++k)
            minias_deferred_expr_destroy(as->stmts[i].variable_exprs[k]);
        minias_mcinst_destroy(as->stmts[i].mcinst);
    }
    free(as->stmts);
    free(as->pending_local_commons);
    free(as->fixups);
    if (as->active_encode_result != NULL) {
        minias_encode_result_destroy(as->active_encode_result);
        free(as->active_encode_result);
    }
    free(as->literals);
    free(as->subsections);
    free(as->subsection_slots);
    free(as->section_stack);
    free(as->numeric_label_counts);
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
    const MiniAsSymbol *symbol=minias_get_symbol(as,name,false);
    return symbol!=NULL && (symbol->defined || symbol->alias_pending);
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

static bool minias_emit_sections(MiniAs *as) {
    size_t i;
    MiniAsEmitRef *refs = NULL;

    if(!minias_validate_fragment_semantics(as))return false;
    if (as->active_encode_result != NULL) {
        minias_set_error(as, "encode-result-reentered");
        return false;
    }
    as->active_encode_result = calloc(1U, sizeof(*as->active_encode_result));
    if (as->active_encode_result == NULL) {
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
        as->emitting_fragment = true;
        as->emitting_fragment_id = refs[i].order;
        as->committing_fragment=false;
        if (stmt->fragment_kind == MINIAS_FRAGMENT_ALIGNMENT ||
            stmt->fragment_kind == MINIAS_FRAGMENT_ORIGIN) {
            bool code_nop_align=minias_alignment_emits_target_nops(
                as,stmt,section);
            MiniAsCodeAlignSink alignment_sink={as};
            if (!(code_nop_align
                      ? as->target->append_code_alignment(&alignment_sink,stmt,stmt->size)
                      : stmt->align_has_fill
                      ? minias_section_append_fill(as,stmt->section,stmt->size,
                                                   stmt->align_fill,true)
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
            if (!minias_common_emit_data_stmt(as, stmt)) {
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
        } else if (!as->target->encode(&(MiniAsEncodeContext){as}, stmt)) {
            if (as->error[0] == '\0') {
                minias_set_error(as,
                                 "encode-failed:%s:%s:line=%zu",
                                 stmt->op,
                                 stmt->args,
                                 stmt->line);
            }
            free(refs);
            return false;
        }
        /* Every source Fragment uses the same measured-size validation
         * and commit. No fixed DATA, ALIGN, FILL or SPACE bypass remains. */
        if(as->active_encode_result->byte_count!=stmt->size ||
           stmt->offset>SIZE_MAX) {
            minias_set_error(as,
                "fragment-emitted-size-mismatch:line=%zu:expected=%u:actual=%zu",
                stmt->line,stmt->size,
                as->active_encode_result->byte_count);
            free(refs);
            return false;
        }
        if(!minias_commit_encode_result(as,stmt)) {
            free(refs);
            return false;
        }
        as->emitting_fragment = false;
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

int minias_assemble_config(const char *input_path,
                           const char *output_path,
                           const MiniAsAssemblyConfig *config,
                           FILE *diagnostic) {
    MiniAsFragmentGrowthSet branch_growth={0};
    size_t source_reparse_pass = 0U;
    MiniAsSourceGraph *graph=minias_source_graph_create();
    int rc = 1;

    if(graph==NULL) {
        if(diagnostic!=NULL)
            fprintf(diagnostic,"minic-as: out-of-memory:source-graph\n");
        return 1;
    }

    for (;;) {
        MiniAs as;
        bool parsed;
        bool added_long_branches = false;

        const bool initialized=minias_init_for_target(&as,config->target);
        as.elf_flags = config->elf_flags;
        as.output_elf32 = config->elf32;
        as.object_machine = config->object_machine;
        as.output_rel_relocations = config->output_rel_relocations;
        if (config->target_profile_size > sizeof(config->target_profile)) {
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
                as.sections[(size_t)text_section].align=align;
            }
        }
        if(parsed && !minias_source_graph_is_loaded(graph)) {
            parsed=minias_source_graph_load_physical(&as,input_path,graph);
        }
        if(parsed)parsed=minias_common_parse_source_graph(&as,graph);
        if(parsed && as.target->prepare_layout!=NULL) {
            const MiniAsPreLayoutView parsed_fragments = {
                .stmts = as.stmts,
                .stmt_count = as.stmt_count,
                .section_count = as.section_count,
                .target_state = as.target_state
            };
            parsed=as.target->prepare_layout(&parsed_fragments,
                                              as.error,sizeof(as.error));
            if(!parsed && as.error[0]=='\0')
                minias_set_error(&as,"target-layout-preparation-failed");
            if(as.error[0]!='\0')parsed=false;
        }
        if(parsed)parsed=minias_source_graph_rebind_forced_branches(
                &as,as.stmts,as.stmt_count,&branch_growth);
        if(parsed && !minias_solve_fragment_layout(
              &as,&branch_growth,&added_long_branches,diagnostic))
            parsed=false;
        if(parsed && added_long_branches) {
            parsed=minias_source_graph_capture_forced_branches(
                &as,as.stmts,as.stmt_count,&branch_growth);
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
            const MiniAsPostEmitView final_layout = {
                .stmts = as.stmts,
                .stmt_count = as.stmt_count,
                .sections = as.sections,
                .section_count = as.section_count,
                .target_state = as.target_state
            };
            MiniAsPostEmitErrorSink errors = { &as };
            MiniAsPostEmitObjectSink object = { &as };
            if ((as.target->validate_output == NULL ||
                 as.target->validate_output(&final_layout, &errors)) &&
                minias_finalize_target_output(&as,&final_layout,&errors,&object) &&
                (config->elf32 ? minias_write_elf32(&as, output_path)
                       : minias_write_elf64(&as, output_path))) {
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
    MiniAsAssemblyConfig config={0};
    config.target=minias_target_riscv();
    config.elf32=elf32;
    config.elf_flags=elf_flags;
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
