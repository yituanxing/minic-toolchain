#include "minias_common_lowering.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/* One Common semantic lowerer: GNU directives, typed data, labels,
 * local numeric symbols, literal-pool fragments and source EOF commit.
 * No duplicate parser or fallback pipeline is introduced by this split. */

static bool add_stmt(MiniAs *as,
                     MiniAsFragmentKind fragment_kind,
                     const char *op,
                     const char *args,
                     size_t line,
                     uint32_t size,
                     uint64_t align) {
    MiniAsStmt *st;

    if (!minias_grow_array((void **)&as->stmts,
                    &as->stmt_capacity,
                    sizeof(*as->stmts),
                    as->stmt_count + 1U)) {
        minias_set_error(as, "out-of-memory:stmt");
        return false;
    }
    st = &as->stmts[as->stmt_count];
    memset(st, 0, sizeof(*st));
    st->fragment_kind = fragment_kind;
    st->source_site = minias_source_current_site(as);
    st->op = minias_strdup(op);
    st->args = minias_strdup(args == NULL ? "" : args);
    st->line = line;
    st->section = as->current_section;
    st->subsection = as->current_subsection;
    if (!minias_current_location(as, &st->offset)) {
        free(st->op);
        free(st->args);
        st->op = NULL;
        st->args = NULL;
        return false;
    }
    st->size = size;
    st->align = align;
    if (as->target->capture_site_state != NULL)
        as->target->capture_site_state(as->target_state, st, &st->site);
    if (st->op == NULL || st->args == NULL) {
        minias_set_error(as, "out-of-memory:stmt-text");
        return false;
    }
    ++as->stmt_count;
    if (fragment_kind == MINIAS_FRAGMENT_INSTRUCTION) {
        MiniAsParseContext parsing = { .as=as };
        if (!as->target->parse_mcinst(&parsing, st))return false;
    }
    /* Code-after-data needs a GNU target-defined implicit alignment. Record
     * the transition per subsection so enormous instruction-only Linux
     * regions do not grow a redundant alignment Fragment per instruction.
     * The actual padding is deferred to MCLayout, where symbolic LEBs and
     * other variable-width preceding data have their final sizes. */
    if(fragment_kind==MINIAS_FRAGMENT_INSTRUCTION ||
       fragment_kind==MINIAS_FRAGMENT_FIXED_DATA ||
       fragment_kind==MINIAS_FRAGMENT_BYTES ||
       fragment_kind==MINIAS_FRAGMENT_FILL ||
       fragment_kind==MINIAS_FRAGMENT_SPACE ||
       fragment_kind==MINIAS_FRAGMENT_ORIGIN) {
        MiniAsSubsection *sub=minias_ensure_subsection(
            as,st->section,st->subsection);
        if(sub==NULL)return false;
        sub->pending_instruction_alignment=
            fragment_kind!=MINIAS_FRAGMENT_INSTRUCTION;
    }
    return true;
}

/* GNU AArch64 emits zero data padding before an instruction after an odd-
 * sized .byte/.uleb128, while regular .data instructions are not padded.
 * This is a target-advertised alignment capability, independent of ELF
 * section alignment and shared for real instructions and .inst words.
 * Retain an ordinary ALIGN Fragment so symbolic LEB resizing remains in
 * MCLayout's one fixed point (including anchors before the next opcode). */
static bool minias_maybe_align_instruction(MiniAs *as,size_t line) {
    const unsigned alignment=as->target->implicit_instruction_alignment;
    MiniAsSubsection *sub;
    MiniAsStmt *stmt;
    uint64_t pad;
    if(alignment==0U ||
       (as->sections[(size_t)as->current_section].flags&MINIAS_SHF_EXECINSTR)==0U)
        return true;
    if((alignment&(alignment-1U))!=0U || alignment>UINT32_MAX) {
        minias_set_error(as,"invalid-target-implicit-code-alignment");
        return false;
    }
    sub=minias_ensure_subsection(as,as->current_section,as->current_subsection);
    if(sub==NULL)return false;
    pad=(alignment-(sub->size&(alignment-1U)))&(alignment-1U);
    if(!sub->pending_instruction_alignment && pad==0U)return true;
    if(!add_stmt(as,MINIAS_FRAGMENT_ALIGNMENT,".implicit_code_align","",
                 line,(uint32_t)pad,alignment))return false;
    stmt=&as->stmts[as->stmt_count-1U];
    /* GNU inserts zero bytes here, not 4-byte NOP instructions. Mark this
     * as explicit DATA fill to prevent the target NOP alignment callback. */
    stmt->align_has_fill=true;
    stmt->align_fill=0U;
    if(!advance_current_location(as,pad))return false;
    sub->pending_instruction_alignment=false;
    if(as->sections[(size_t)as->current_section].align<alignment)
        as->sections[(size_t)as->current_section].align=alignment;
    return true;
}

bool minias_parse_u64(const char *text, uint64_t *out) {
    char *end = NULL;
    unsigned long long value;

    errno = 0;
    value = strtoull(text, &end, 0);
    if (errno != 0 || end == text) {
        return false;
    }
    while (*end == ' ' || *end == '\t') {
        ++end;
    }
    if (*end != '\0') {
        return false;
    }
    *out = (uint64_t)value;
    return true;
}

static bool switch_section(MiniAs *as,
                           const char *name,
                           const char *group_signature,
                           const char *link_order_reference,
                           bool has_unique_id,
                           uint32_t unique_id,
                           uint32_t group_flags,
                           uint32_t type,
                           uint64_t flags,
                           uint64_t align) {
    const MiniAsSectionIdentity key={name,group_signature,
                                      link_order_reference,
                                      has_unique_id,unique_id};
    int index = minias_ensure_section_key(as,&key,group_flags,
                                          type,flags,align);

    if (index < 0) {
        return false;
    }
    if (index != as->current_section || as->current_subsection != 0U) {
        as->previous_section = as->current_section;
        as->previous_subsection = as->current_subsection;
        as->current_section = index;
        as->current_subsection = 0U;
    }
    return minias_ensure_subsection(as, as->current_section, as->current_subsection) != NULL;
}

/* GAS's implicit section names are family members only at the exact
 * name or a dot boundary: .bss.hot is NOBITS, .bssification is not.
 * .note.GNU-stack is intentionally PROGBITS without SHF_ALLOC. */
static bool gnu_section_family(const char *name,const char *root) {
    size_t n=strlen(root);
    return strncmp(name,root,n)==0 && (name[n]=='\0' || name[n]=='.');
}

static bool parse_section_directive(MiniAs *as, char *args, size_t line) {
    char *comma = NULL,*scan=args;
    char *name;
    bool quoted=false,escaped=false;
    uint64_t flags = 0U;
    uint32_t type = MINIAS_SHT_PROGBITS;
    uint32_t group_flags = 0U;
    uint64_t entry_size=0U;
    bool has_unique_id=false,inherit_group=false;
    uint32_t unique_id=0U;
    const char *group_signature=NULL;
    const char *link_order_reference=NULL;

    /* A quoted GNU section name may contain literal commas. Only a
     * top-level comma separates its name from flags/type metadata. */
    for(;*scan;++scan) {
        if(quoted && *scan=='\\' && !escaped) {escaped=true;continue;}
        if(*scan=='"' && !escaped)quoted=!quoted;
        if(*scan==',' && !quoted) {comma=scan;break;}
        escaped=false;
    }
    if(quoted) {
        minias_set_error(as,"unterminated-section-name:line=%zu",line);
        return false;
    }
    if (comma != NULL) {
        *comma = '\0';
    }
    name = minias_trim(args);
    if (*name == '"') {
        size_t n = strlen(name);
        if(n<2U || name[n-1U]!='"') {
            minias_set_error(as,"bad-quoted-section-name:line=%zu",line);
            return false;
        }
        name[n-1U]='\0';++name;
    }
    if (*name == '\0') {
        minias_set_error(as, "bad-directive:.section:line=%zu", line);
        return false;
    }

    if (gnu_section_family(name,".bss") ||
        gnu_section_family(name,".tbss")) {
        type=MINIAS_SHT_NOBITS;
    } else if(gnu_section_family(name,".note") &&
              strcmp(name,".note.GNU-stack")!=0) {
        type=MINIAS_SHT_NOTE;
    } else if(gnu_section_family(name,".init_array")) {
        type=MINIAS_SHT_INIT_ARRAY;
    } else if(gnu_section_family(name,".fini_array")) {
        type=MINIAS_SHT_FINI_ARRAY;
    } else if(gnu_section_family(name,".preinit_array")) {
        type=MINIAS_SHT_PREINIT_ARRAY;
    }

    if (comma != NULL) {
        char *rest = comma + 1;
        char *q = strchr(rest, '"');
        char *type_separator = NULL;

        if (q != NULL) {
            char *flag_end;

            ++q;
            while (*q != '\0' && *q != '"') {
                if (*q == 'a') {
                    flags |= MINIAS_SHF_ALLOC;
                } else if (*q == 'w') {
                    flags |= MINIAS_SHF_WRITE;
                } else if (*q == 'x') {
                    flags |= MINIAS_SHF_EXECINSTR;
                } else if (*q == 'M') {
                    flags |= MINIAS_SHF_MERGE;
                } else if (*q == 'S') {
                    flags |= MINIAS_SHF_STRINGS;
                } else if (*q == 'T') {
                    flags |= MINIAS_SHF_TLS;
                } else if (*q == 'G') {
                    flags |= MINIAS_SHF_GROUP;
                } else if (*q == '?') {
                    inherit_group=true;
                } else if (*q == 'R') {
                    flags |= MINIAS_SHF_GNU_RETAIN;
                } else if (*q == 'o') {
                    flags |= MINIAS_SHF_LINK_ORDER;
                } else {
                    /* A silently dropped flag can change ELF selection,
                     * merging, relocation or linker garbage collection.
                     * GNU's unknown letters are fatal, not advisory. */
                    minias_set_error(as,"unsupported-section-flag:%c:%s:line=%zu",
                                     *q,name,line);
                    return false;
                }
                ++q;
            }
            if (*q != '"') {
                minias_set_error(as,"unterminated-section-flags:%s:line=%zu",
                                 name,line);
                return false;
            }
            flag_end = q;
            type_separator = strchr(flag_end, ',');
        } else {
            type_separator = strchr(rest, ',');
        }

        if (type_separator != NULL) {
            char *kind = minias_trim(type_separator + 1);
            char *fields[8];
            size_t field_count=0U,next=0U;
            char *cursor=kind,*tail;
            while((tail=strchr(cursor,','))!=NULL) {
                if(field_count>=sizeof(fields)/sizeof(fields[0])) {
                    minias_set_error(as,"too-many-section-parameters:%s:line=%zu",name,line);
                    return false;
                }
                *tail='\0';
                fields[field_count++]=minias_trim(tail+1);
                cursor=tail+1;
            }
            /* Extra section arguments are consumed in ELF syntax order:
             * M entsize, o linked-to symbol, G signature and linkage. */
            if((flags & MINIAS_SHF_MERGE)!=0U) {
                if(next>=field_count ||
                   !minias_parse_u64(fields[next++],&entry_size) ||
                   entry_size==0U) {
                    minias_set_error(as,"bad-merge-entry-size:%s:line=%zu",name,line);
                    return false;
                }
            }
            if((flags & MINIAS_SHF_LINK_ORDER)!=0U) {
                /* A missing anchor is accepted by GAS as sh_link=0.  An
                 * explicit empty argument is not a valid symbol.  Store a
                 * unique empty identity so this never aliases a plain
                 * same-named Section without SHF_LINK_ORDER. */
                link_order_reference="";
                if(next<field_count &&
                   !(strcmp(fields[next],"unique")==0 &&
                     next+1U<field_count)) {
                    const char *linked=fields[next++];
                    if(*linked=='\0' || strlen(linked)>=256U ||
                       strpbrk(linked," \t,\r\n\"")!=NULL) {
                        minias_set_error(as,"bad-link-order-reference:%s:line=%zu",name,line);
                        return false;
                    }
                    link_order_reference=linked;
                }
            }
            if((flags & MINIAS_SHF_GROUP)!=0U) {
                const char *sig;
                if(next>=field_count) {
                    minias_set_error(as,"missing-group-signature:%s:line=%zu",name,line);
                    return false;
                }
                sig=fields[next++];
                if(*sig=='\0' ||
                   strlen(sig)>=256U ||
                   strpbrk(sig," \t,\r\n\"")!=NULL) {
                    minias_set_error(as,"bad-section-group:%s:line=%zu",name,line);
                    return false;
                }
                group_signature=sig;
                if(next<field_count &&
                   (strcmp(fields[next],"comdat")==0 ||
                    strcmp(fields[next],".gnu.linkonce")==0)) {
                    group_flags=MINIAS_GRP_COMDAT;
                    ++next;
                }
            }
            /* GAS treats this trailing pair as ELF Section identity, not
             * another section attribute. ID zero must stay distinguishable
             * from an ordinary section with no unique argument. */
            if(next<field_count && strcmp(fields[next],"unique")==0) {
                uint64_t parsed;
                ++next;
                if(next>=field_count ||
                   !minias_parse_u64(fields[next++],&parsed) ||
                   parsed>UINT32_MAX) {
                    minias_set_error(as,"bad-section-unique-id:%s:line=%zu",name,line);
                    return false;
                }
                has_unique_id=true;
                unique_id=(uint32_t)parsed;
            }
            if(next!=field_count) {
                minias_set_error(as,"unexpected-section-parameter:%s:line=%zu",name,line);
                return false;
            }
            if (*kind == '@' || *kind == '%') {
                ++kind;
            }
            if (strcmp(kind, "note") == 0) {
                type = MINIAS_SHT_NOTE;
            } else if (strcmp(kind, "nobits") == 0) {
                type = MINIAS_SHT_NOBITS;
            } else if (strcmp(kind, "progbits") == 0) {
                type = MINIAS_SHT_PROGBITS;
            } else if (strcmp(kind, "init_array") == 0) {
                type = MINIAS_SHT_INIT_ARRAY;
            } else if (strcmp(kind, "fini_array") == 0) {
                type = MINIAS_SHT_FINI_ARRAY;
            } else if (strcmp(kind, "preinit_array") == 0) {
                type = MINIAS_SHT_PREINIT_ARRAY;
            } else {
                minias_set_error(as,"unsupported-section-type:%s:line=%zu",name,line);
                return false;
            }
        } else if((flags & MINIAS_SHF_LINK_ORDER)!=0U ||
                  (flags & MINIAS_SHF_GROUP)!=0U ||
                  (flags & MINIAS_SHF_MERGE)!=0U) {
            minias_set_error(as,"missing-section-type:%s:line=%zu",name,line);
            return false;
        }
    }

    if(inherit_group && (flags & MINIAS_SHF_GROUP)!=0U) {
        minias_set_error(as,"conflicting-section-group-inheritance:%s:line=%zu",name,line);
        return false;
    }
    if ((flags & MINIAS_SHF_GROUP)!=0U && group_signature==NULL) {
        minias_set_error(as,"missing-section-group:%s:line=%zu",name,line);
        return false;
    }
    if (flags == 0U) {
        if(gnu_section_family(name,".text")) {
            flags=MINIAS_SHF_ALLOC | MINIAS_SHF_EXECINSTR;
        } else if(gnu_section_family(name,".rodata")) {
            flags=MINIAS_SHF_ALLOC;
        } else if(gnu_section_family(name,".data") ||
                  gnu_section_family(name,".bss")) {
            flags=MINIAS_SHF_ALLOC | MINIAS_SHF_WRITE;
        } else if(gnu_section_family(name,".tdata") ||
                  gnu_section_family(name,".tbss")) {
            flags=MINIAS_SHF_ALLOC | MINIAS_SHF_WRITE | MINIAS_SHF_TLS;
        } else if(type==MINIAS_SHT_INIT_ARRAY ||
                  type==MINIAS_SHT_FINI_ARRAY ||
                  type==MINIAS_SHT_PREINIT_ARRAY) {
            flags=MINIAS_SHF_ALLOC | MINIAS_SHF_WRITE;
        }
        /* GNU note sections are NOT implicitly SHF_ALLOC; only an
         * explicit 'a' allocates .note.* payloads. */
    }
    if(inherit_group && as->current_section>=0 &&
       (size_t)as->current_section<as->section_count) {
        const MiniAsSection *prior=&as->sections[(size_t)as->current_section];
        if(prior->group_signature!=NULL) {
            flags|=MINIAS_SHF_GROUP;
            group_signature=prior->group_signature;
            group_flags=prior->group_flags;
        }
    }
    if(entry_size==0U &&
       (type==MINIAS_SHT_INIT_ARRAY || type==MINIAS_SHT_FINI_ARRAY ||
        type==MINIAS_SHT_PREINIT_ARRAY))
        entry_size=as->output_elf32?4U:8U;
    if ((flags & MINIAS_SHF_MERGE)!=0U && entry_size==0U) {
        minias_set_error(as,"missing-merge-entry-size:%s:line=%zu",name,line);
        return false;
    }
    /*
     * RISC-V instructions in executable input subsections must not be
     * placed at byte alignment.  In particular .text.main may follow
     * odd-length .rodata when MiniLD links section-per-function inputs.
     * The .text directive already uses four-byte instruction alignment.
     */
    {
        const MiniAsSectionIdentity key={name,group_signature,
                                          link_order_reference,
                                          has_unique_id,unique_id};
        int prior=minias_find_section_key(as,&key);
        bool ok=switch_section(as,name,group_signature,link_order_reference,
                has_unique_id,unique_id,group_flags,type,flags,
                (flags & MINIAS_SHF_EXECINSTR)!=0U ?
                   minias_target_exec_alignment(as):1U);
        /* sh_entsize is a property of the *first* ELF section creation.
         * Later incompatible .section definitions are ignored by GAS. */
        if(ok && prior<0 && entry_size!=0U)
            as->sections[(size_t)as->current_section].entry_size=entry_size;
        if (ok) {
            if (group_signature!=NULL && prior<0) {
                MiniAsSymbol *signature;
                signature=minias_get_symbol(as,group_signature,true);
                if(signature==NULL)return false;
                /* GAS synthesizes an undefined default-local group
                 * signature as a LOCAL symbol in the new SHT_GROUP section.
                 * An explicitly .globl signature remains GLOBAL UND. */
            }
        }
        return ok;
    }
}

static bool handle_symbol_list(MiniAs *as,
                               char *args,
                               uint8_t bind,
                               bool binding,
                               uint8_t visibility,
                               bool set_visibility) {
    char *cursor = args;

    while (cursor != NULL) {
        char *comma = strchr(cursor, ',');
        MiniAsSymbol *symbol;

        if (comma != NULL) {
            *comma = '\0';
        }
        cursor = minias_trim(cursor);
        if (*cursor != '\0') {
            symbol = minias_get_symbol(as, cursor, true);
            if (symbol == NULL) {
                return false;
            }
            if (binding) {
                /* GNU GAS: a late .local never turns an already-declared
                 * SHN_COMMON global into a local COMMON ELF symbol. */
                if (!(bind == MINIAS_STB_LOCAL && symbol->defined &&
                      symbol->section == MINIAS_SECTION_COMMON)) {
                    symbol->bind = bind;
                    symbol->explicit_local_binding = bind == MINIAS_STB_LOCAL;
                }
            }
            if (set_visibility) {
                symbol->visibility = visibility;
            }
        }
        cursor = comma == NULL ? NULL : comma + 1;
    }
    return true;
}

static bool parse_type(MiniAs *as, char *args, size_t line) {
    char *separator = strchr(args, ',');
    MiniAsSymbol *symbol;
    char *kind;

    if (separator == NULL) {
        separator = args;
        while (*separator != '\0' &&
               !isspace((unsigned char)*separator)) {
            ++separator;
        }
        if (*separator == '\0') {
            minias_set_error(as, "bad-directive:.type:line=%zu", line);
            return false;
        }
    }
    *separator = '\0';
    symbol = minias_get_symbol(as, minias_trim(args), true);
    if (symbol == NULL) {
        return false;
    }
    kind = minias_trim(separator + 1);
    if (strcmp(kind, "@function") == 0 || strcmp(kind, "%function") == 0 ||
        strcmp(kind, "STT_FUNC") == 0) {
        symbol->type = MINIAS_STT_FUNC;
    } else if (strcmp(kind, "@object") == 0 || strcmp(kind, "%object") == 0 ||
               strcmp(kind, "STT_OBJECT") == 0) {
        symbol->type = MINIAS_STT_OBJECT;
    } else if(strcmp(kind,"@notype")==0 || strcmp(kind,"%notype")==0 ||
              strcmp(kind,"STT_NOTYPE")==0) {
        symbol->type=MINIAS_STT_NOTYPE;
    } else if(strcmp(kind,"@tls_object")==0 || strcmp(kind,"%tls_object")==0 ||
              strcmp(kind,"STT_TLS")==0) {
        symbol->type=MINIAS_STT_TLS;
    } else if(strcmp(kind,"@common")==0 || strcmp(kind,"%common")==0 ||
              strcmp(kind,"STT_COMMON")==0) {
        /* GAS treats @common as STT_OBJECT + GLOBAL binding, not as
         * an ELF STT_COMMON (5) symbol. This is observable in symtab. */
        symbol->type=MINIAS_STT_OBJECT;
        symbol->bind=MINIAS_STB_GLOBAL;
    } else if(strcmp(kind,"@gnu_unique_object")==0 ||
              strcmp(kind,"%gnu_unique_object")==0) {
        symbol->type=MINIAS_STT_OBJECT;
        symbol->bind=MINIAS_STB_GNU_UNIQUE;
    } else if(strcmp(kind,"@gnu_indirect_function")==0 ||
              strcmp(kind,"%gnu_indirect_function")==0 ||
              strcmp(kind,"STT_GNU_IFUNC")==0) {
        symbol->type=MINIAS_STT_GNU_IFUNC;
    } else {
        minias_set_error(as,"unsupported-elf-symbol-type:%s:line=%zu",
                         kind,line);
        return false;
    }
    return true;
}

static bool parse_size(MiniAs *as, char *args, size_t line) {
    char *comma = strchr(args, ',');
    MiniAsSymbol *symbol;
    MiniAsSymbol *start_symbol;
    char *expr;
    int64_t size_addend;
    size_t start_symbol_index;

    if (comma == NULL) {
        minias_set_error(as, "bad-directive:.size:line=%zu", line);
        return false;
    }
    *comma = '\0';
    symbol = minias_get_symbol(as, minias_trim(args), true);
    if (symbol == NULL) {
        return false;
    }
    expr = minias_trim(comma + 1);
    {
        uint64_t explicit_size;
        MiniAsSymbol *size_symbol;
        if (minias_parse_u64(expr, &explicit_size)) {
            /*
             * GNU GCC commonly emits .size before the object's label:
             *
             *   .type object, @object
             *   .size object, 8
             * object:
             *
             * The size declaration is independent of definition order, so
             * retain it on the symbol and let the later label define value
             * and section ownership.
             */
            symbol->size = explicit_size;
            return true;
        }
        size_symbol = minias_get_symbol(as, expr, false);
        if (size_symbol != NULL && size_symbol->defined &&
            size_symbol->section == MINIAS_SECTION_ABS) {
            symbol->size = size_symbol->value;
            return true;
        }
    }
    if (!symbol->defined) {
        minias_set_error(as, "bad-size-symbol:%s:line=%zu", symbol->name, line);
        return false;
    }
    start_symbol=symbol;
    start_symbol_index=(size_t)(symbol-as->symbols);
    if (!minias_parse_size_end_addend(expr,symbol->name,&size_addend)) {
        MiniAsSymbolDifference difference;
        if(!minias_parse_symbol_difference_absolute_tail(as,expr,&difference) ||
           strcmp(difference.positive.name,".")!=0 ||
           (start_symbol=minias_get_symbol(as,difference.negative.name,false))==NULL ||
           !start_symbol->defined || !start_symbol->has_fragment_anchor ||
           !minias_parse_size_end_addend(expr,start_symbol->name,&size_addend)) {
            minias_set_error(as,"unsupported-expression:.size:%s:line=%zu",expr,line);
            return false;
        }
        start_symbol_index=(size_t)(start_symbol-as->symbols);
    }
    if (symbol->section != as->current_section ||
        start_symbol->section != as->current_section) {
        minias_set_error(as, "size-section-mismatch:%s:line=%zu", symbol->name, line);
        return false;
    }
    symbol->has_size_start_symbol=start_symbol!=symbol;
    symbol->size_start_symbol_index=start_symbol_index;
    if(symbol->subsection!=as->current_subsection ||
       start_symbol->subsection!=as->current_subsection) {
        /* GAS joins numbered subsections before evaluating symbol sizes.
         * A span from subsection 0 to subsection 1 cannot be computed from
         * parse-site LOCAL offsets: capture its end owner and let MCLayout
         * subtract the two final Section-relative Fragment anchors. */
        symbol->has_size_end_anchor=true;
        symbol->size_end_anchor=as->stmt_count;
        symbol->size_end_subsection=as->current_subsection;
        symbol->size_end_addend=size_addend;
        symbol->size=0U;
        as->has_cross_subsection_size_end=true;
        return true;
    }
    {
        uint64_t here;
        int64_t end;
        if (!minias_current_location(as, &here) ||
            here>(uint64_t)INT64_MAX ||
            !minias_checked_add_i64((int64_t)here,size_addend,&end) ||
            end<0 || (uint64_t)end < start_symbol->value) {
            minias_set_error(as, "size-location-mismatch:%s:line=%zu", symbol->name, line);
            return false;
        }
        symbol->size = (uint64_t)end - start_symbol->value;
        symbol->has_size_end_anchor = true;
        symbol->size_end_anchor = as->stmt_count;
        symbol->size_end_subsection = as->current_subsection;
        symbol->size_end_addend = size_addend;
    }
    return true;
}

/* A symbol alias carries a stable SymbolID edge.  The first parse resolves
 * forward references, and subsequent MCLayout passes refresh values after
 * branch/data growth.  Do not intern a second symbol under the same name. */
static bool bind_pending_alias(MiniAs *as,const char *name,size_t target_index,
                               int64_t addend,size_t line) {
    MiniAsSymbol *alias=minias_get_symbol(as,name,true);
    if(alias==NULL)return false;
    if(alias->defined || alias->alias_pending || alias->has_alias_dependency) {
        minias_set_error(as,"duplicate-symbol:%s:line=%zu",name,line);
        return false;
    }
    alias->has_alias_dependency=true;
    alias->alias_pending=true;
    alias->alias_target_index=target_index;
    alias->alias_addend=addend;
    alias->alias_line=line;
    return true;
}

static bool parse_equ(MiniAs *as, char *args, size_t line) {
    char *comma = strchr(args, ',');
    char *name;
    char *expression;
    int64_t value;
    MiniAsSymbol *symbol;
    MiniAsSymbolExpr expr={0};

    if (comma == NULL) {
        minias_set_error(as, "bad-directive:.equ:line=%zu", line);
        return false;
    }
    *comma = '\0';
    name = minias_trim(args);
    expression = minias_trim(comma + 1);
    /* Signed absolute ELF .equ is legal: -1 is stored in uint64_t
     * symbol storage as two's-complement bits and interpreted as i64 by
     * Common's signed expression evaluator. Only relocatable values
     * require section-specific nonnegative address constraints. */
    if (*name == '\0' || *expression == '\0') {
        minias_set_error(as,
                         "unsupported-expression:.equ:%s:line=%zu",
                         expression,
                         line);
        return false;
    }
    if(minias_parse_symbol_addend(expression,&expr)) {
        if(strcmp(expr.name,".")==0) {
            uint64_t here,position;
            if(!minias_current_location(as,&here) ||
               (expr.addend>=0 && (uint64_t)expr.addend>UINT64_MAX-here) ||
               (expr.addend<0 &&
                (uint64_t)(-(expr.addend+1))+1U>here)) {
                minias_set_error(as,"equ-location-overflow:line=%zu",line);
                return false;
            }
            position=expr.addend>=0 ? here+(uint64_t)expr.addend :
                here-((uint64_t)(-(expr.addend+1))+1U);
            symbol=minias_get_symbol(as,name,true);
            if(!symbol)return false;
            if(symbol->defined || symbol->alias_pending) {
                minias_set_error(as,"duplicate-symbol:%s:line=%zu",name,line);
                return false;
            }
            symbol->defined=true;
            symbol->section=as->current_section;
            symbol->subsection=as->current_subsection;
            symbol->value=position;
            symbol->has_fragment_anchor=true;
            symbol->fragment_anchor=as->stmt_count;
            symbol->fragment_anchor_addend=expr.addend;
            return true;
        }
        {
            MiniAsSymbol *target=minias_get_symbol(as,expr.name,true);
            size_t target_index;
            if(!target)return false;
            target_index=(size_t)(target-as->symbols);
            if(!target->defined || target->section!=MINIAS_SECTION_ABS)
                return bind_pending_alias(as,name,target_index,expr.addend,line);
        }
    }
    if(!minias_evaluate_absolute_expression(as, expression, &value)) {
        minias_set_error(as,"unsupported-expression:.equ:%s:line=%zu",
                         expression,line);
        return false;
    }
    symbol = minias_get_symbol(as, name, true);
    if (symbol == NULL) {
        return false;
    }
    if (symbol->defined || symbol->alias_pending) {
        if (symbol->defined && symbol->section == MINIAS_SECTION_ABS &&
            symbol->value == (uint64_t)value) {
            return true;
        }
        minias_set_error(as, "duplicate-symbol:%s:line=%zu", name, line);
        return false;
    }
    symbol->defined = true;
    symbol->section = MINIAS_SECTION_ABS;
    symbol->subsection = 0U;
    symbol->value = (uint64_t)value;
    return true;
}

static bool parse_set(MiniAs *as, char *args, size_t line) {
    char *comma = strchr(args, ',');
    char *name;
    char *expression;
    MiniAsSymbolExpr expr;
    MiniAsSymbol *target;
    MiniAsSymbol *alias;
    size_t target_index;

    if (comma == NULL) {
        minias_set_error(as, "bad-directive:.set:line=%zu", line);
        return false;
    }
    *comma = '\0';
    name = minias_trim(args);
    expression = minias_trim(comma + 1);
    if (*name == '\0' || *expression == '\0') {
        minias_set_error(as,
                         "unsupported-expression:.set:%s:line=%zu",
                         expression,
                         line);
        return false;
    }

    {
        const char *tail = expression;
        bool location_constant = false;

        if (*tail == '.') {
            ++tail;
            while (*tail == ' ' || *tail == '\t') {
                ++tail;
            }
            if (*tail == '\0') {
                location_constant = true;
            } else if (*tail == '+' || *tail == '-') {
                ++tail;
                while (*tail == ' ' || *tail == '\t') {
                    ++tail;
                }
                location_constant = isdigit((unsigned char)*tail) != 0;
            }
        }

        if (location_constant) {
            int64_t value;

            /*
             * GCC emits section anchors as:
             *
             *   .set .LANCHOR0, . + 0
             *
             * "." is the location counter, not a symbol named ".". Keep the
             * result relocatable.  Do not catch ". - symbol" here: that is an
             * absolute symbol difference and belongs to the generic evaluator.
             */
            if (!minias_evaluate_absolute_expression(as, expression, &value) || value < 0) {
                minias_set_error(as,
                                 "unsupported-expression:.set:%s:line=%zu",
                                 expression,
                                 line);
                return false;
            }
            alias = minias_get_symbol(as, name, true);
            if (alias == NULL) {
                return false;
            }
            if (alias->defined || alias->alias_pending) {
                minias_set_error(as, "duplicate-symbol:%s:line=%zu", name, line);
                return false;
            }
            alias->defined = true;
            alias->section = as->current_section;
            alias->subsection = as->current_subsection;
            alias->value = (uint64_t)value;
            {
                MiniAsSymbolExpr location = {0};
                if (minias_parse_symbol_addend(expression, &location) &&
                    strcmp(location.name, ".") == 0) {
                    /* Capture the source-site symbol as an immutable
                     * Fragment anchor. Relaxation moves the anchor, not
                     * the already-parsed GNU assignment statement. */
                    alias->has_fragment_anchor = true;
                    alias->fragment_anchor = as->stmt_count;
                    alias->fragment_anchor_addend = location.addend;
                }
            }
            return true;
        }
    }

    /* Section-relative or forward source symbols are relocatable even if
     * the absolute evaluator can currently return a numeric offset.  Bind
     * their identity first; only known ABS .set counters are snapshotted. */
    if (minias_parse_symbol_addend(expression,&expr) &&
        strcmp(expr.name,".")!=0) {
        target=minias_get_symbol(as,expr.name,true);
        if(!target)return false;
        target_index=(size_t)(target-as->symbols);
        if(!target->defined || target->section!=MINIAS_SECTION_ABS)
            return bind_pending_alias(as,name,target_index,expr.addend,line);
    }

    /* GNU .set is mutable, unlike .equ. Evaluate an already-defined
     * absolute symbol RHS at the *current* statement position, including
     * self-referential .set counter, counter+1 in .irp/macro expansion.
     * Preserve relocatable symbol aliases via the existing fallthrough. */
    {
        int64_t value;
        if(minias_evaluate_absolute_expression(as,expression,&value)) {
            alias=minias_get_symbol(as,name,true);
            if(!alias)return false;
            if(alias->alias_pending ||
               (alias->defined &&
                (alias->section!=MINIAS_SECTION_ABS ||
                 !alias->mutable_absolute_set))) {
                minias_set_error(as,"duplicate-symbol:%s:line=%zu",name,line);
                return false;
            }
            alias->defined=true;
            alias->section=MINIAS_SECTION_ABS;
            alias->subsection=0U;
            alias->value=(uint64_t)value;
            alias->mutable_absolute_set=true;
            as->has_mutable_absolute_sets=true;
            return true;
        }
    }
    if (minias_parse_symbol_addend(expression, &expr)) {
        target = minias_get_symbol(as, expr.name, true);
        if (target == NULL) {
            return false;
        }
        target_index = (size_t)(target - as->symbols);

        alias = minias_get_symbol(as, name, true);
        if (alias == NULL) {
            return false;
        }
        if (alias->defined || alias->alias_pending) {
            minias_set_error(as, "duplicate-symbol:%s:line=%zu", name, line);
            return false;
        }
        return bind_pending_alias(as,name,target_index,expr.addend,line);
    }

    /* GNU ARM64 tramp_alias:
     * .set NAME, TRAMP_VALIAS + tramp_exit - .entry.tramp.text
     * Section-base subtraction turns a forward section symbol into a
     * later absolute constant; the macro often runs before tramp_exit is
     * defined. Preserve a typed pending symbol alias, never fabricate a
     * section-relative numeric address or ignore the .set directive. */
    {
        char *copy=minias_strdup(expression);
        if(!copy){minias_set_error(as,"out-of-memory:section-set");return false;}
        char *sum=strchr(copy,'+');
        char *minus=sum?strrchr(sum+1U,'-'):NULL;
        MiniAsSymbolExpr base;
        int64_t number;
        bool valid=false;
        if(sum && minus && minus>sum+1U) {
            *sum++='\0';*minus++='\0';
            char *section_name=minias_trim(minus);
            char *target_name=minias_trim(sum);
            if(*section_name=='.' &&
               minias_eval_i64_expression(minias_trim(copy),&number) &&
               minias_parse_symbol_addend(target_name,&base) &&
               base.addend==0) {
                MiniAsSymbol *ref=minias_get_symbol(as,base.name,true);
                if(ref!=NULL) {
                    size_t ref_index=(size_t)(ref-as->symbols);
                    MiniAsSymbol *result=minias_get_symbol(as,name,true);
                    if(result && !result->defined && !result->alias_pending) {
                        result->alias_section_base=minias_strdup(section_name);
                        if(result->alias_section_base!=NULL) {
                            result->alias_pending=true;
                            result->has_alias_dependency=true;
                            result->alias_target_index=ref_index;
                            result->alias_addend=number;
                            result->alias_line=line;
                            valid=true;
                        }
                    }
                }
            }
        }
        free(copy);
        if(valid)return true;
    }

    {
        int64_t value;
        if (!minias_evaluate_absolute_expression(as, expression, &value) || value < 0) {
            minias_set_error(as,
                             "unsupported-expression:.set:%s:line=%zu",
                             expression,
                             line);
            return false;
        }
        alias = minias_get_symbol(as, name, true);
        if (alias == NULL) {
            return false;
        }
        if (alias->defined || alias->alias_pending) {
            minias_set_error(as, "duplicate-symbol:%s:line=%zu", name, line);
            return false;
        }
        alias->defined = true;
        alias->section = MINIAS_SECTION_ABS;
        alias->subsection = 0U;
        alias->value = (uint64_t)value;
        return true;
    }
}

/* A single SymbolID +/- a constant is represented as a retained alias
 * dependency, including forward .equ/.set targets. MCLayout refreshes the
 * entire dependency chain after every Fragment growth. Reopening the GNU
 * source for this grammar would discard that stable MC symbol graph and
 * needlessly re-expand macros and conditional source around a branch.
 *
 * More general arithmetic (. - label, A - B, ...) can depend on the
 * original lexical location and still uses the conservative reparse guard.
 */
static bool is_persistent_symbol_assignment(const char *expression) {
    MiniAsSymbolExpr parsed = {0};
    int64_t literal;
    return minias_eval_i64_expression(expression, &literal) ||
           minias_parse_symbol_addend(expression, &parsed);
}

/* Parse and retain the position constraint separately from its GNU optional
 * fill operand. The fill byte belongs to the Common Fragment emitter, not
 * to the instruction target or the symbol-layout solver. */
static bool handle_org_position(MiniAs *as, const char *args,
                                const char *source_args, size_t line,
                                bool has_fill, uint8_t fill) {
    int64_t evaluated;
    int64_t lexical_target;
    MiniAsSymbolExpr symbolic = {0};
    MiniAsSymbol *symbol = NULL;
    MiniAsDeferredExpr *deferred = NULL;
    bool ready;
    uint64_t desired;
    uint64_t current;
    uint64_t pad;

    ready=minias_evaluate_absolute_expression(as,args,&evaluated);
    if(!ready) {
        /* A multi-level GNU .set/.equ alias may not have its final value
         * during lexical parsing. Retain the expression as a SymbolID AST
         * and let the two-phase MCLayout fixed point resolve it after all
         * definitions and moving subsection bases are available. Do NOT
         * treat an unknown symbol as zero or fabricate parser-time bytes. */
        deferred=minias_compile_deferred_expr(as,args);
        if(deferred==NULL)return false;
    } else if(evaluated<0) {
        minias_set_error(as, "unsupported-expression:.org:%s:line=%zu", args, line);
        return false;
    }
    desired = ready ? (uint64_t)evaluated : 0U;
    if (!minias_current_location(as, &current)) {
        minias_deferred_expr_destroy(deferred);
        return false;
    }
    if (ready && desired < current) {
        minias_set_error(as,
                         "org-backwards:%s:current=%llu:target=%llu:line=%zu",
                         args,
                         (unsigned long long)current,
                         (unsigned long long)desired,
                         line);
        minias_deferred_expr_destroy(deferred);
        return false;
    }
    pad = ready ? desired - current : 0U;
    if (ready && pad > UINT32_MAX) {
        minias_set_error(as, "org-gap-too-large:%s:line=%zu", args, line);
        minias_deferred_expr_destroy(deferred);
        return false;
    }
    if (!add_stmt(as,
                  MINIAS_FRAGMENT_ORIGIN,
                  ".org",
                  source_args,
                  line,
                  (uint32_t)pad,
                  1U)) {
        minias_deferred_expr_destroy(deferred);
        return false;
    }
    as->stmts[as->stmt_count - 1U].align_has_fill = has_fill;
    as->stmts[as->stmt_count - 1U].align_fill = fill;
    if(deferred!=NULL) {
        MiniAsStmt *fragment=&as->stmts[as->stmt_count-1U];
        fragment->org_expr=deferred;
        fragment->org_mode=MINIAS_ORG_TYPED_EXPR;
        as->has_org_constraints=true;
        return true; /* Zero-size placeholder; MCLayout owns its padding. */
    }
    /* Preserve the GNU expression as a typed position constraint. A source
     * string is kept only for diagnostics or unsupported control grammar. */
    if (minias_eval_i64_expression(args, &lexical_target) &&
        lexical_target == evaluated) {
        MiniAsStmt *fragment = &as->stmts[as->stmt_count - 1U];
        fragment->org_mode = MINIAS_ORG_ABSOLUTE;
        fragment->org_target = desired;
        as->has_org_constraints = true;
    } else if (minias_parse_symbol_addend(args, &symbolic) &&
               strcmp(symbolic.name, ".") == 0 &&
               symbolic.addend >= 0) {
        MiniAsStmt *fragment = &as->stmts[as->stmt_count - 1U];
        fragment->org_mode = MINIAS_ORG_DOT_ADDEND;
        fragment->org_target = (uint64_t)symbolic.addend;
        as->has_org_constraints = true;
    } else if (minias_parse_symbol_addend(args, &symbolic) &&
               (symbol = minias_get_symbol(as, symbolic.name, false)) != NULL &&
               symbol->defined && symbol->section == MINIAS_SECTION_ABS) {
        /* Lexically resolved .set/.equ values are frozen at this site. */
        MiniAsStmt *fragment = &as->stmts[as->stmt_count - 1U];
        fragment->org_mode = MINIAS_ORG_ABSOLUTE;
        fragment->org_target = desired;
        as->has_org_constraints = true;
    } else if (symbol != NULL && symbol->defined &&
               symbol->has_fragment_anchor &&
               symbol->section == as->current_section &&
               /* Lower-numbered subsections precede this output location;
                * a later subsection would create a circular .org dependency. */
               symbol->subsection <= as->current_subsection &&
               symbol->fragment_anchor < as->stmt_count) {
        MiniAsStmt *fragment = &as->stmts[as->stmt_count - 1U];
        fragment->org_mode = MINIAS_ORG_SYMBOL_ADDEND;
        fragment->org_symbol_index = (size_t)(symbol - as->symbols);
        fragment->org_addend = symbolic.addend;
        as->has_org_constraints = true;
    } else {
        /* Retain the complete GNU expression as stable SymbolID operations.
         * Its dot and symbol values are evaluated only from MCLayout, not
         * by re-entering a source parser with mutable current-section. */
        MiniAsStmt *fragment=&as->stmts[as->stmt_count - 1U];
        fragment->org_expr=minias_compile_deferred_expr(as,args);
        if(fragment->org_expr==NULL)return false;
        fragment->org_mode=MINIAS_ORG_TYPED_EXPR;
        as->has_org_constraints=true;
    }
    return advance_current_location(as, pad);
}

static bool handle_org(MiniAs *as, const char *args, size_t line) {
    char *copy = minias_strdup(args);
    char *position, *comma;
    uint8_t fill = 0U;
    bool has_fill = false;
    bool ok;
    if(copy == NULL) {
        minias_set_error(as,"out-of-memory:org-expression");
        return false;
    }
    comma = strchr(copy, ',');
    if(comma != NULL) {
        int64_t value;
        char *fill_expr;
        *comma++ = '\0';
        fill_expr = minias_trim(comma);
        if(*fill_expr == '\0' || strchr(fill_expr, ',') != NULL ||
           !minias_evaluate_absolute_expression(as,fill_expr,&value)) {
            minias_set_error(as,"bad-directive:.org-fill:line=%zu",line);
            free(copy);
            return false;
        }
        has_fill = true;
        fill = (uint8_t)value;
    }
    position = minias_trim(copy);
    ok = handle_org_position(as,position,args,line,has_fill,fill);
    free(copy);
    return ok;
}

/* A new ISA may request a pool address without teaching Core the ISA
 * encoding or introducing a parallel ELF writer.  Records are grouped by
 * SECTION+SUBSECTION and deduped only until the next .ltorg flush. */
bool minias_queue_literal(MiniAs *as,const MiniAsSymbolExpr *expression,
                                 unsigned width,MiniAsSymbolExpr *pool_label) {
    MiniAsLiteral *entry;
    MiniAsSymbolRef bound;
    size_t i;
    int written;
    if(!as->target->supports_literal_pools || as->current_section<0 ||
       !expression || !pool_label || (width!=4U && width!=8U)) {
        minias_set_error(as,"unsupported-target-literal-pool");return false;
    }
    if(!minias_bind_symbol_ref(as,expression,&bound))return false;
    for(i=0U;i<as->literal_count;++i) {
        entry=&as->literals[i];
        if(!entry->emitted && entry->section==as->current_section &&
           entry->subsection==as->current_subsection &&
           entry->width==width &&
           entry->expression.addend==bound.addend &&
           entry->expression.name_id==bound.name_id) {
            written=snprintf(pool_label->name,sizeof(pool_label->name),
                             "%s",entry->label);
            if(written<0 || (size_t)written>=sizeof(pool_label->name))
                return false;
            pool_label->addend=0;return true;
        }
    }
    if(as->literal_count==SIZE_MAX ||
       !minias_grow_array((void **)&as->literals,&as->literal_capacity,
                   sizeof(*as->literals),as->literal_count+1U)) {
        minias_set_error(as,"out-of-memory:literal-pool");return false;
    }
    entry=&as->literals[as->literal_count];
    memset(entry,0,sizeof(*entry));
    entry->section=as->current_section;
    entry->subsection=as->current_subsection;
    entry->width=width;entry->expression=bound;
    if(!minias_make_private_symbol_name(entry->label,sizeof(entry->label),
                                        "literal",as->literal_count)) {
        minias_set_error(as,"literal-label-overflow");return false;
    }
    ++as->literal_count;
    written=snprintf(pool_label->name,sizeof(pool_label->name),
                     "%s",entry->label);
    if(written<0 || (size_t)written>=sizeof(pool_label->name))return false;
    pool_label->addend=0;
    return true;
}

/* GNU .p2align/.balign/.align alignment[,fill[,max_skip]].
 * Target owns NOP encoding, Common owns fragment length and optional fill. */
static bool parse_align_args(MiniAs *as,const char *op,const char *args,
                             uint64_t *align,bool *has_fill,uint8_t *fill,
                             bool *has_max,uint64_t *max_skip){
    char *copy=minias_strdup(args),*cursor,*comma,*parts[3];
    unsigned n=0U;
    int64_t value;
    if(!copy)return false;
    cursor=copy;
    for(;;) {
        if(n>=3U)goto failed;
        parts[n++]=minias_trim(cursor);
        comma=strchr(cursor,',');
        if(!comma)break;
        *comma='\0';cursor=comma+1U;
    }
    if(!*parts[0] ||
       !minias_evaluate_absolute_expression(as,parts[0],&value) ||
       value<0)goto failed;
    if(!strcmp(op,".p2align") ||
       (!strcmp(op,".align") && !as->target->align_directive_bytes)){
        if(value>=63)goto failed;
        *align=UINT64_C(1)<<(unsigned)value;
    }else *align=(uint64_t)value;
    if(!*align || (*align&(*align-1U)) || *align>UINT32_MAX)goto failed;
    *has_fill=false;*fill=0U;*has_max=false;*max_skip=0U;
    if(n>=2U && *parts[1]){
        if(!minias_evaluate_absolute_expression(as,parts[1],&value) ||
           value<-128 || value>255)goto failed;
        *has_fill=true;*fill=(uint8_t)value;
    }
    if(n>=3U && *parts[2]){
        if(!minias_evaluate_absolute_expression(as,parts[2],&value) ||
           value<0)goto failed;
        *has_max=true;*max_skip=(uint64_t)value;
    }
    free(copy);return true;
failed:
    free(copy);return false;
}

static bool handle_align(MiniAs *as,const char *op,char *args,size_t line){
    uint64_t alignment,current,pad,max_skip;
    bool has_fill,has_max;
    uint8_t fill;
    MiniAsStmt *stmt;
    if(!parse_align_args(as,op,args,&alignment,&has_fill,&fill,
                         &has_max,&max_skip)){
        minias_set_error(as,"unsupported-alignment:%s:line=%zu",op,line);
        return false;
    }
    if(!minias_current_location(as,&current))return false;
    pad=(alignment-(current&(alignment-1U)))&(alignment-1U);
    if(has_max && pad>max_skip)pad=0U;
    if(pad>UINT32_MAX)return false;
    if(!add_stmt(as,MINIAS_FRAGMENT_ALIGNMENT,op,"",line,(uint32_t)pad,alignment))
        return false;
    stmt=&as->stmts[as->stmt_count-1U];
    stmt->align_has_fill=has_fill;stmt->align_fill=fill;
    stmt->align_has_max_skip=has_max;stmt->align_max_skip=max_skip;
    if(!advance_current_location(as,pad))return false;
    if(alignment>as->sections[(size_t)as->current_section].align)
        as->sections[(size_t)as->current_section].align=alignment;
    return true;
}

static unsigned int data_width(const MiniAs *as, const char *op) {
    if (strcmp(op, ".byte") == 0) {
        return 1U;
    }
    if (strcmp(op, ".half") == 0 || strcmp(op, ".short") == 0 ||
        strcmp(op, ".hword") == 0 || strcmp(op, ".value") == 0 ||
        strcmp(op, ".2byte") == 0) {
        return 2U;
    }
    if (!strcmp(op,".inst")) return as->target->inst_directive_width;
    if (strcmp(op, ".word") == 0)
        return as->target->word_directive_width == 0U ?
            4U : as->target->word_directive_width;
    if (strcmp(op, ".long") == 0 || strcmp(op, ".4byte") == 0) {
        return 4U;
    }
    if (strcmp(op, ".dword") == 0 || strcmp(op, ".quad") == 0 ||
        strcmp(op, ".xword") == 0 || strcmp(op, ".8byte") == 0) {
        return 8U;
    }
    return 0U;
}

/* Shared directive grammar is the sole source of aliases that can inject
 * raw bytes into an executable section. The identical category controls
 * Common dispatch and the Target ISA-escape policy callback. This prevents
 * future .hword/.skip/.uleb128-style aliases bypassing strict RV profiles. */
static MiniAsCommonDirectiveKind common_directive_kind(const MiniAs *as,
                                                        const char *op) {
    static const char *const raw_data[] = {
        ".fill", ".zero", ".space", ".skip", ".uleb128", ".sleb128",
        ".asciz", ".string", ".ascii", ".incbin"
    };
    size_t i;
    if(!strcmp(op,".org"))return MINIAS_COMMON_DIRECTIVE_ORG;
    if(data_width(as,op)!=0U)return MINIAS_COMMON_DIRECTIVE_RAW_DATA;
    for(i=0U;i<sizeof(raw_data)/sizeof(raw_data[0]);++i)
        if(!strcmp(op,raw_data[i]))return MINIAS_COMMON_DIRECTIVE_RAW_DATA;
    return MINIAS_COMMON_DIRECTIVE_OTHER;
}

static char *decode_incbin_path(MiniAs *as,
                                const char *args,
                                size_t line) {
    unsigned char *decoded = NULL;
    size_t decoded_size = 0U;
    char *path;

    if (!minias_decode_string_literals(args, false, &decoded, &decoded_size) ||
        decoded_size == 0U || memchr(decoded, '\0', decoded_size) != NULL) {
        free(decoded);
        minias_set_error(as, "bad-directive:.incbin:line=%zu", line);
        return NULL;
    }
    path = malloc(decoded_size + 1U);
    if (path == NULL) {
        free(decoded);
        minias_set_error(as, "out-of-memory:incbin-path");
        return NULL;
    }
    memcpy(path, decoded, decoded_size);
    path[decoded_size] = '\0';
    free(decoded);
    return path;
}

/* A source-data Fragment is immutable. Capture .incbin binary bytes
 * once, before changing the Fragment graph, instead of measuring by ftell
 * and then reopening the pathname after layout/branch relaxation.
 * Detect a concurrent source mutation during capture and fail closed. */
static bool capture_incbin(MiniAs *as,const char *args,size_t line,
                           unsigned char **data,size_t *size) {
    char *path=decode_incbin_path(as,args,line);
    FILE *file;
    long end;
    unsigned char *buffer=NULL;
    size_t count,used=0U;
    if(!path)return false;
    file=fopen(path,"rb");
    if(!file) {
        minias_set_error(as,"incbin-open:%s:line=%zu",path,line);
        free(path);
        return false;
    }
    if(fseek(file,0L,SEEK_END)!=0 || (end=ftell(file))<0L ||
       (uint64_t)end>UINT32_MAX || fseek(file,0L,SEEK_SET)!=0) {
        minias_set_error(as,"incbin-size:%s:line=%zu",path,line);
        goto failed;
    }
    count=(size_t)end;
    buffer=malloc(count?count:1U);
    if(!buffer) {
        minias_set_error(as,"out-of-memory:incbin-snapshot");
        goto failed;
    }
    while(used<count) {
        size_t got=fread(buffer+used,1U,count-used,file);
        if(got==0U) {
            minias_set_error(as,"incbin-read:%s:line=%zu",path,line);
            goto failed;
        }
        used+=got;
    }
    if(fgetc(file)!=EOF || ferror(file)) {
        minias_set_error(as,"incbin-mutated-during-read:%s:line=%zu",
                         path,line);
        goto failed;
    }
    fclose(file);
    free(path);
    *data=buffer;
    *size=count;
    return true;
failed:
    free(buffer);
    fclose(file);
    free(path);
    return false;
}

static bool emit_common_pattern(MiniAs *as,int section,
                                const unsigned char *element,
                                size_t width,size_t repeat) {
    return minias_section_append_pattern(as,section,element,width,repeat,false);
}

/* Shared full-width ULEB bytes: DWARF attributes may legitimately contain
 * values above INT64_MAX even though source .uleb128 expressions currently
 * use the checked signed expression evaluator. */
bool minias_encode_uleb_integer(uint64_t number,unsigned char output[10],
                                size_t *length) {
    size_t n=0U;
    do {
        uint8_t byte=(uint8_t)(number&0x7fU);
        number>>=7U;
        output[n++]=(unsigned char)(byte|(number?0x80U:0U));
    }while(number);
    *length=n;
    return true;
}

/* One encoding contract for eagerly resolved LEB literals, retained
 * deferred fragments and both DWARF writers. Never reparse source text. */
bool minias_encode_leb_integer(int64_t number,bool signed_leb,
                               unsigned char output[10],size_t *length) {
    size_t n=0U;
    if(!signed_leb)return number>=0 &&
        minias_encode_uleb_integer((uint64_t)number,output,length);
    for(;;) {
        uint8_t byte=(uint8_t)((uint64_t)number&0x7fU);
        bool done;
        number>>=7U;
        done=(number==0 && (byte&0x40U)==0U) ||
             (number==-1 && (byte&0x40U)!=0U);
        if(n>=10U)return false;
        output[n++]=(unsigned char)(byte | (done?0U:0x80U));
        if(done){*length=n;return true;}
    }
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

/* An immediate LEB remains a parse-time immutable byte snapshot. Symbolic
 * expressions enter the same Fragment fixed-point as .fill/.space instead. */
static bool minias_leb_bytes(MiniAs *as,const char *op,const char *text,
                             unsigned char output[10],size_t *length) {
    int64_t number;
    return minias_evaluate_absolute_expression(as,text,&number) &&
           minias_encode_leb_integer(number,strcmp(op,".sleb128")==0,
                                     output,length);
}

/* Repeat a literal little-endian element: GNU .fill repeat[,size[,value]].
 * All targets use Common DATA fragments; no target-specific section writer. */
/* Size expressions with only numeric tokens are stable first-pass
 * fragments and must not disable the in-place relaxation fast path.
 * Symbolic names, location counter '.' and unresolved numeric syntax
 * enter the shared deferred solver. Scan tokens, not raw alphabetic
 * characters: 0xffULL is a number, not a forward symbol. */
static bool common_data_has_symbolic_token(const char *p) {
    while(*p) {
        if(isdigit((unsigned char)*p)) {
            char *end;
            uint64_t bits;
            if(minias_parse_number_literal(p,&end,&bits)) {
                (void)bits;
                p=end;
                continue;
            }
        }
        if(isalpha((unsigned char)*p) || *p=='_' || *p=='.' ||
           *p=='$')return true;
        ++p;
    }
    return false;
}

/* Recognize only real absolute .set symbols; never rewrite ordinary
 * relocatable labels or PC-relative expressions into guessed constants. */
static bool data_references_mutable_set(MiniAs *as,const char *expr) {
    const char *p=expr;
    while(*p) {
        if(minias_expr_symbol_start(*p)) {
            const char *start=p;
            size_t len;
            char name[256];
            MiniAsSymbol *sym;
            do {++p;} while(minias_expr_symbol_continue(*p));
            len=(size_t)(p-start);
            if(len>=sizeof(name))continue;
            memcpy(name,start,len);name[len]='\0';
            sym=minias_get_symbol(as,name,false);
            if(sym && sym->mutable_absolute_set)return true;
        }else ++p;
    }
    return false;
}

/* GAS snapshots mutable absolute .set operands at each lexical DATA site,
 * including symbolic .fill/.space sizes. A later .set reassignment cannot
 * retroactively change the earlier fragment during iterative layout. */
static bool snapshot_mutable_data_tokens(MiniAs *as,const char *input,
                                         char **out) {
    size_t original=strlen(input),capacity,used=0U;
    const char *p=input;
    char *result;
    if(original>(SIZE_MAX-1U)/24U) {
        minias_set_error(as,"mutable-data-expression-overflow");return false;
    }
    capacity=original*24U+1U;
    result=malloc(capacity);
    if(!result) {
        minias_set_error(as,"out-of-memory:mutable-data-snapshot");return false;
    }
    while(*p) {
        const char *start=p;
        char representation[48];
        const char *chosen=NULL;
        size_t part;
        if(isdigit((unsigned char)*p)) {
            char *end=NULL;uint64_t number;
            if(minias_parse_number_literal(p,&end,&number) && end>p) {
                p=end;
            }else ++p;
        }else if(minias_expr_symbol_start(*p)) {
            char name[256];MiniAsSymbol *sym;
            while(minias_expr_symbol_continue(*p))++p;
            part=(size_t)(p-start);
            if(part<sizeof(name)) {
                int n;
                memcpy(name,start,part);name[part]='\0';
                sym=minias_get_symbol(as,name,false);
                if(sym && sym->defined && sym->mutable_absolute_set &&
                   sym->section==MINIAS_SECTION_ABS) {
                    if(sym->value<=INT64_MAX)
                        n=snprintf(representation,sizeof(representation),
                                   "%llu",(unsigned long long)sym->value);
                    else
                        n=snprintf(representation,sizeof(representation),
                                   "-%llu",(unsigned long long)
                                   (UINT64_MAX-sym->value+UINT64_C(1)));
                    if(n<0 || (size_t)n>=sizeof(representation)) {
                        free(result);return false;
                    }
                    chosen=representation;
                }
            }
        }else ++p;
        part=chosen?strlen(chosen):(size_t)(p-start);
        if(part>=capacity-used) {
            minias_set_error(as,"mutable-data-snapshot-overflow");
            free(result);return false;
        }
        memcpy(result+used,chosen?chosen:start,part);
        used+=part;
    }
    result[used]='\0';*out=result;return true;
}

/* One Common semantic expression compiler for every target's
 * .byte/.short/.long/.quad/.inst data fields. The earlier syntax checker
 * and the final materializer no longer tokenize the same source again.
 * A deferred-absolute node is necessary for .set and local-dot arithmetic:
 * it is not a fabricated relocation or prematurely frozen section address.
 */
static bool add_data_stmt(MiniAs *as, const char *op, char *args, size_t line) {
    uint64_t count = 0U;
    uint64_t bytes;
    MiniAsFragmentKind fragment_kind=MINIAS_FRAGMENT_FIXED_DATA;
    unsigned int width = data_width(as, op);
    char *copy = NULL;
    char *cursor;
    char *snapshot=NULL;
    bool delayed=false;
    bool variable_parsed=false;
    uint64_t variable_repeat=0U,variable_value=0U;
    unsigned variable_width=0U;
    unsigned char leb_snapshot[10]={0};
    size_t leb_snapshot_size=0U;
    bool has_leb_snapshot=false;
    unsigned char *string_snapshot=NULL;
    size_t string_snapshot_size=0U;
    bool has_binary_snapshot=false;

    if (!strcmp(op,".fill")) {
        fragment_kind=MINIAS_FRAGMENT_FILL;
        delayed=common_data_has_symbolic_token(args);
        /* Compile the retained expression before computing its provisional
         * size below, after lexical .set snapshots have been captured. The
         * former immediate parser was a second grammar for these fields. */
        bytes=0U;
    } else if (strcmp(op,".uleb128")==0 || strcmp(op,".sleb128")==0) {
        fragment_kind=MINIAS_FRAGMENT_BYTES;
        unsigned char coded[10];
        size_t len=0U;
        /* Forward numeric labels and DWARF view symbols are not yet
         * located. Their widths belong to the typed layout fixed point,
         * not to a speculative first-pass evaluation or source replay. */
        delayed=common_data_has_symbolic_token(args);
        if(delayed) {
            bytes=0U;
        }else if(!minias_leb_bytes(as,op,args,coded,&len)) {
            minias_set_error(as,"unsupported-leb-expression:%s:line=%zu",args,line);
            return false;
        }else {
            bytes=len;
            memcpy(leb_snapshot,coded,len);
            leb_snapshot_size=len;
            has_leb_snapshot=true;
        }
    } else if (strcmp(op, ".incbin") == 0) {
        fragment_kind=MINIAS_FRAGMENT_BYTES;
        if(!capture_incbin(as,args,line,&string_snapshot,
                           &string_snapshot_size))return false;
        bytes=(uint64_t)string_snapshot_size;
        has_binary_snapshot=true;
    } else if (strcmp(op, ".asciz") == 0 || strcmp(op, ".string") == 0 ||
        strcmp(op, ".ascii") == 0) {
        fragment_kind=MINIAS_FRAGMENT_BYTES;
        unsigned char *decoded = NULL;
        size_t decoded_size = 0U;
        bool terminate = strcmp(op, ".ascii") != 0;

        if (!minias_decode_string_literals(args, terminate, &decoded, &decoded_size)) {
            minias_set_error(as, "unsupported-string:%s:line=%zu", op, line);
            return false;
        }
        /* Retain decoded literal bytes: output must never re-decode a
         * source string after Fragment layout has changed. */
        string_snapshot=decoded;
        string_snapshot_size=decoded_size;
        bytes = (uint64_t)decoded_size;
    } else if (strcmp(op, ".zero") == 0 || strcmp(op, ".space") == 0 ||
               strcmp(op, ".skip") == 0) {
        fragment_kind=MINIAS_FRAGMENT_SPACE;
        delayed=common_data_has_symbolic_token(args);
        bytes=0U;
    } else {
        /* One Common MCExpr parser validates every fixed-width field.
         * Measurement only counts fields; parsed semantic expressions are
         * retained on the stable DATA Fragment for emission. */
        if(width==0U) {
            minias_set_error(as,"invalid-fixed-data-width:%s",op);
            return false;
        }
        /* GNU GAS accepts an operand-free fixed-width directive as a
         * zero-byte statement (`663: .long` appears in AArch64 Linux
         * alternative-insn expansions). An empty argument list is not a
         * single invalid MCExpr or an implicit zero-valued element. */
        count=*minias_trim(args)=='\0'?0U:1U;
        for(cursor=args;*cursor;++cursor)
            if(*cursor==',')++count;
        if(count>UINT32_MAX/width) {
            minias_set_error(as,"mc-data-count-overflow:line=%zu",line);
            return false;
        }
        bytes=count*width;
    }

    if (bytes > UINT32_MAX) {
        free(string_snapshot);
        minias_set_error(as, "data-too-large:%s:line=%zu", op, line);
        return false;
    }
    /*
     * GNU GAS freezes an absolute .set expression at its data emission
     * site, not at the last value after macro/.irp expansion. Store only
     * those parsed constants in the retained DATA statement string.
     * Leave all relocatable/forward references intact for layout/fixups.
     */
    if(width && as->has_mutable_absolute_sets && count) {
        size_t original_len=strlen(args),capacity;
        char *walk,*out;
        bool changed=false;
        size_t used=0U;
        if(count>(SIZE_MAX-original_len-1U)/24U) {
            minias_set_error(as,"set-data-snapshot-overflow");return false;
        }
        capacity=original_len+1U+(size_t)count*24U;
        snapshot=malloc(capacity);
        copy=minias_strdup(args);
        if(!snapshot || !copy) {
            free(snapshot);free(copy);
            minias_set_error(as,"out-of-memory:set-data-snapshot");return false;
        }
        walk=copy;out=snapshot;
        for(;;) {
            char *comma=strchr(walk,',');
            char *token;
            const char *selected;
            char numeric[32];
            size_t part;
            uint64_t value;
            if(comma)*comma='\0';
            token=minias_trim(walk);
            selected=token;
            if(data_references_mutable_set(as,token)) {
                MiniAsSymbolExpr reloc;
                if(minias_parse_absolute_data_bits(as,token,&value)) {
                    (void)snprintf(numeric,sizeof(numeric),"%llu",
                                   (unsigned long long)value);
                    selected=numeric;
                    changed=true;
                }else if(minias_expr_parse_symbol_absolute_tail(as,token,&reloc)) {
                    char *reloc_snapshot=malloc(strlen(reloc.name)+32U);
                    size_t needed;
                    if(!reloc_snapshot) {
                        free(copy);free(snapshot);
                        minias_set_error(as,"out-of-memory:set-reloc-snapshot");
                        return false;
                    }
                    needed=(size_t)snprintf(reloc_snapshot,
                                           strlen(reloc.name)+32U,
                                           "%s%+lld",reloc.name,
                                           (long long)reloc.addend);
                    if(needed>capacity-used-1U) {
                        free(reloc_snapshot);free(copy);free(snapshot);
                        minias_set_error(as,"set-reloc-snapshot-overflow");
                        return false;
                    }
                    memcpy(out+used,reloc_snapshot,needed);
                    used+=needed;
                    free(reloc_snapshot);
                    changed=true;
                    if(!comma)break;
                    out[used++]=',';
                    walk=comma+1U;
                    continue;
                }
            }
            part=strlen(selected);
            if(part>capacity-used-1U) {
                free(copy);free(snapshot);
                minias_set_error(as,"set-data-snapshot-overflow");return false;
            }
            memcpy(out+used,selected,part);used+=part;
            if(!comma)break;
            out[used++]=',';
            walk=comma+1U;
        }
        out[used]='\0';
        free(copy);copy=NULL;
        if(!changed){free(snapshot);snapshot=NULL;}
    }
    if(!snapshot && as->has_mutable_absolute_sets &&
       (fragment_kind==MINIAS_FRAGMENT_FILL ||
        fragment_kind==MINIAS_FRAGMENT_SPACE ||
        (fragment_kind==MINIAS_FRAGMENT_BYTES && delayed)) &&
       !snapshot_mutable_data_tokens(as,args,&snapshot))return false;
    if (!add_stmt(as, fragment_kind, op,
                  snapshot?snapshot:args, line, (uint32_t)bytes, 1U)) {
        free(snapshot);
        free(string_snapshot);
        return false;
    }
    free(snapshot);
    if(has_leb_snapshot || has_binary_snapshot || string_snapshot!=NULL) {
        MiniAsStmt *stmt=&as->stmts[as->stmt_count-1U];
        if(has_leb_snapshot) {
            stmt->static_data=malloc(leb_snapshot_size);
            if(!stmt->static_data) {
                minias_set_error(as,"out-of-memory:leb-snapshot:line=%zu",line);
                return false;
            }
            memcpy(stmt->static_data,leb_snapshot,leb_snapshot_size);
            stmt->static_data_size=leb_snapshot_size;
        }else {
            stmt->static_data=string_snapshot;
            stmt->static_data_size=string_snapshot_size;
            string_snapshot=NULL;
        }
    }
    if(fragment_kind==MINIAS_FRAGMENT_BYTES &&
       (strcmp(op,".uleb128")==0 || strcmp(op,".sleb128")==0))
        as->stmts[as->stmt_count-1U].leb_mode=
            strcmp(op,".sleb128")==0?2U:1U;
    if(fragment_kind==MINIAS_FRAGMENT_FILL ||
       fragment_kind==MINIAS_FRAGMENT_SPACE) {
        MiniAsStmt *stmt=&as->stmts[as->stmt_count-1U];
        uint64_t repeat=0U,value=0U;
        unsigned element_width=0U;
        /* The stored spelling is already frozen at this lexical site.
         * Compile exactly once; both the first-pass placement and the
         * later MCLayout fixed point evaluate these same owned SymbolIDs. */
        if(!minias_compile_variable_exprs(as,stmt))return false;
        if(minias_resolve_variable_exprs(as,stmt,&repeat,&element_width,&value) &&
           (element_width==0U || repeat<=UINT32_MAX/element_width)) {
            bytes=repeat*element_width;
            stmt->size=(uint32_t)bytes;
            variable_parsed=true;
            variable_repeat=repeat;
            variable_width=element_width;
            variable_value=value;
        }else {
            /* Forward symbols and section-relative values may settle only
             * after relaxation. MCLayout also rejects malformed resolved
             * size/value combinations without a speculative source reparse. */
            delayed=true;
        }
    }
    if(variable_parsed) {
        MiniAsStmt *fragment=&as->stmts[as->stmt_count-1U];
        fragment->variable_data_resolved=true;
        fragment->variable_repeat=variable_repeat;
        fragment->variable_value=variable_value;
        fragment->variable_element_width=variable_width;
    }
    if(width!=0U &&
       !minias_compile_data_mc(as,&as->stmts[as->stmt_count-1U],width))
        return false;
    if(fragment_kind==MINIAS_FRAGMENT_BYTES && delayed) {
        MiniAsStmt *stmt=&as->stmts[as->stmt_count-1U];
        stmt->variable_exprs[0]=minias_compile_deferred_expr(as,stmt->args);
        if(stmt->variable_exprs[0]==NULL)return false;
        stmt->variable_expr_count=1U;
        /* The retained payload changes size during the Fragment fixed
         * point; allocate its maximum encoded width only once. */
        stmt->static_data=malloc(10U);
        if(!stmt->static_data) {
            minias_set_error(as,"out-of-memory:deferred-leb:line=%zu",line);
            return false;
        }
    }
    if(delayed) {
        as->stmts[as->stmt_count-1U].deferred_data_size=true;
        as->has_deferred_data=true;
        /* A deferred size is a persistent Fragment property. It need not
         * trigger a second source parse: the shared layout solver can
         * recompute it after branch growth using final symbol anchors.
         * Only controls that change the Fragment graph, notably
         * location-dependent .if/.elseif, retain the reparse guard.
         * Typed .org/.fill/.space are resolved in-place by MCLayout. */
    }
    return advance_current_location(as, bytes);
}

bool minias_common_emit_data_stmt(MiniAs *as, const MiniAsStmt *stmt) {
    /* Source spelling is intentionally absent from this dispatcher.
     * Every data producer, including synthesized literal pools, installs
     * its semantic FragmentKind while parsing, once and for all. */
    switch(stmt->fragment_kind) {
    case MINIAS_FRAGMENT_FILL: {
        uint64_t repeat=stmt->variable_repeat;
        uint64_t value=stmt->variable_value;
        unsigned width_bytes=stmt->variable_element_width,j;
        unsigned char element[8];
        if(!stmt->variable_data_resolved || width_bytes>8U ||
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
        if(stmt->static_data_size!=stmt->size ||
           (stmt->size!=0U && !stmt->static_data)) {
            minias_set_error(as,"immutable-data-fragment-size:line=%zu",
                             stmt->line);
            return false;
        }
        /* DATA emission consumes the owned byte snapshot (lexical bytes or
         * fixed-point-resolved LEB), never reinterpreting source text. */
        return minias_section_append(as,stmt->section,
                                     stmt->static_data,stmt->static_data_size);
    case MINIAS_FRAGMENT_SPACE:
        if(!stmt->variable_data_resolved ||
           stmt->variable_element_width!=1U ||
           stmt->variable_repeat!=(uint64_t)stmt->size ||
           stmt->variable_value>255U) {
            minias_set_error(as,"invalid-typed-space-fragment:line=%zu",
                             stmt->line);
            return false;
        }
        return minias_section_append_fill(as,stmt->section,stmt->size,
                                          (uint8_t)stmt->variable_value,true);
    case MINIAS_FRAGMENT_FIXED_DATA:
        return minias_emit_mc_data(as,stmt);
    default:
        minias_set_error(as,"unexpected-data-fragment-kind:%u:line=%zu",
                         (unsigned)stmt->fragment_kind,stmt->line);
        return false;
    }
}

static bool parse_numeric_label_id(const char *text, size_t *id) {
    size_t value = 0U;
    const char *p = text;

    if (text == NULL || id == NULL || *text == '\0') {
        return false;
    }
    while (*p != '\0') {
        size_t digit;
        if (!isdigit((unsigned char)*p)) {
            return false;
        }
        digit = (size_t)(*p - '0');
        if (value > (SIZE_MAX - digit) / 10U) {
            return false;
        }
        value = value * 10U + digit;
        ++p;
    }
    *id = value;
    return true;
}

static bool ensure_numeric_label_slot(MiniAs *as, size_t id) {
    if (id < as->numeric_label_capacity) {
        return true;
    }
    if (id >= SIZE_MAX/sizeof(*as->numeric_label_counts)) {
        minias_set_error(as, "numeric-label-too-large:%zu", id);
        return false;
    }
    size_t old_capacity=as->numeric_label_capacity;
    if (!minias_grow_array((void **)&as->numeric_label_counts,
                          &as->numeric_label_capacity,
                          sizeof(*as->numeric_label_counts),id+1U)) {
        minias_set_error(as, "out-of-memory:numeric-labels");
        return false;
    }
    memset(as->numeric_label_counts + old_capacity, 0,
           (as->numeric_label_capacity - old_capacity) *
           sizeof(*as->numeric_label_counts));
    return true;
}

static bool numeric_ref_boundary(char ch) {
    return ch == '\0' ||
           !(isalnum((unsigned char)ch) || ch == '_' || ch == '.' || ch == '$');
}

static bool append_rewritten_text(char **buffer,
                                  size_t *size,
                                  size_t *capacity,
                                  const char *text,
                                  size_t count) {
    size_t need;
    if (*size == SIZE_MAX || count > SIZE_MAX-1U-*size) return false;
    need = *size + count + 1U;
    if (!minias_grow_array((void **)buffer,capacity,sizeof(**buffer),need))
        return false;
    memcpy(*buffer + *size, text, count);
    *size += count;
    (*buffer)[*size] = '\0';
    return true;
}

bool minias_numeric_refs_rewrite(MiniAs *as,
                                       const char *text,
                                       char **out,
                                       size_t line) {
    size_t size = 0U;
    size_t capacity = 0U;
    char *buffer = NULL;
    const char *p = text;
    char quote = '\0';
    bool escaped = false;

    while (*p != '\0') {
        if (escaped) {
            if (!append_rewritten_text(&buffer, &size, &capacity, p, 1U)) {
                goto oom;
            }
            escaped = false;
            ++p;
            continue;
        }
        if (quote != '\0') {
            if (*p == '\\') {
                escaped = true;
            } else if (*p == quote) {
                quote = '\0';
            }
            if (!append_rewritten_text(&buffer, &size, &capacity, p, 1U)) {
                goto oom;
            }
            ++p;
            continue;
        }
        if (*p == '\'' || *p == '"') {
            quote = *p;
            if (!append_rewritten_text(&buffer, &size, &capacity, p, 1U)) {
                goto oom;
            }
            ++p;
            continue;
        }
        if (isdigit((unsigned char)*p) &&
            (p == text || numeric_ref_boundary(p[-1]) ||
             /* Immediate introducer $ precedes GNU local labels $1f/$2b,
              * but must not split the suffix of a longer symbol foo$1f. */
             (p[-1] == '$' &&
              ((size_t)(p - text) == 1U || numeric_ref_boundary(p[-2]))))) {
            const char *digits = p;
            size_t label_id = 0U;
            size_t generation;
            char replacement[96];
            int written;

            while (isdigit((unsigned char)*p)) {
                size_t digit = (size_t)(*p - '0');
                if (label_id > (SIZE_MAX - digit) / 10U) {
                    p = digits;
                    break;
                }
                label_id = label_id * 10U + digit;
                ++p;
            }
            if (p != digits && (*p == 'f' || *p == 'b') &&
                numeric_ref_boundary(p[1])) {
                if (!ensure_numeric_label_slot(as, label_id)) {
                    free(buffer);
                    return false;
                }
                if (*p == 'b') {
                    if (as->numeric_label_counts[label_id] == 0U) {
                        minias_set_error(as,
                                         "undefined-numeric-label:%zub:line=%zu",
                                         label_id,
                                         line);
                        free(buffer);
                        return false;
                    }
                    generation = as->numeric_label_counts[label_id];
                } else {
                    generation = as->numeric_label_counts[label_id] + 1U;
                }
                {
                    char kind[40];
                    written=snprintf(kind,sizeof(kind),"num_%zu",label_id);
                    if(written<0 || (size_t)written>=sizeof(kind) ||
                       !minias_make_private_symbol_name(
                           replacement,sizeof(replacement),kind,generation))
                        goto oom;
                }
                written=(int)strlen(replacement);
                if (written < 0 || (size_t)written >= sizeof(replacement) ||
                    !append_rewritten_text(&buffer,
                                           &size,
                                           &capacity,
                                           replacement,
                                           (size_t)written)) {
                    goto oom;
                }
                ++p;
                continue;
            }
            p = digits;
        }
        if (!append_rewritten_text(&buffer, &size, &capacity, p, 1U)) {
            goto oom;
        }
        ++p;
    }

    if (buffer == NULL) {
        buffer = minias_strdup("");
        if (buffer == NULL) {
            goto oom;
        }
    }
    *out = buffer;
    return true;

oom:
    free(buffer);
    minias_set_error(as, "out-of-memory:numeric-label-rewrite");
    return false;
}

static bool push_section(MiniAs *as, char *args, size_t line) {
    if (!minias_grow_array((void **)&as->section_stack,
                    &as->section_stack_capacity,
                    sizeof(*as->section_stack),
                    as->section_stack_count + 1U)) {
        minias_set_error(as, "out-of-memory:section-stack");
        return false;
    }
    as->section_stack[as->section_stack_count].section = as->current_section;
    as->section_stack[as->section_stack_count].subsection = as->current_subsection;
    ++as->section_stack_count;
    if (!parse_section_directive(as, args, line)) {
        --as->section_stack_count;
        return false;
    }
    return true;
}

static bool pop_section(MiniAs *as, size_t line) {
    int old_current;

    if (as->section_stack_count == 0U) {
        minias_set_error(as, "bad-directive:.popsection:line=%zu", line);
        return false;
    }
    {
        uint32_t old_subsection = as->current_subsection;
        MiniAsSectionState restored = as->section_stack[--as->section_stack_count];

        old_current = as->current_section;
        as->current_section = restored.section;
        as->current_subsection = restored.subsection;
        as->previous_section = old_current;
        as->previous_subsection = old_subsection;
    }
    return minias_ensure_subsection(as, as->current_section, as->current_subsection) != NULL;
}

static bool define_label(MiniAs *as, char *label, size_t line) {
    char canonical[96];
    size_t numeric_id;
    MiniAsSymbol *symbol;

    label = minias_trim(label);
    if (*label == '\0') {
        minias_set_error(as, "empty-label:line=%zu", line);
        return false;
    }
    if (parse_numeric_label_id(label, &numeric_id)) {
        int written;
        size_t generation;

        if (!ensure_numeric_label_slot(as, numeric_id)) {
            return false;
        }
        generation = ++as->numeric_label_counts[numeric_id];
        {
            char kind[40];
            written=snprintf(kind,sizeof(kind),"num_%zu",numeric_id);
            if(written<0 || (size_t)written>=sizeof(kind) ||
               !minias_make_private_symbol_name(
                   canonical,sizeof(canonical),kind,generation)) {
                minias_set_error(as,"numeric-label-too-long:line=%zu",line);
                return false;
            }
        }
        label = canonical;
    }
    symbol = minias_get_symbol(as, label, true);
    if (symbol == NULL) {
        return false;
    }
    if (symbol->defined || symbol->alias_pending) {
        minias_set_error(as, "duplicate-symbol:%s:line=%zu", symbol->name, line);
        return false;
    }
    symbol->defined = true;
    symbol->section = as->current_section;
    symbol->subsection = as->current_subsection;
    symbol->has_fragment_anchor = true;
    symbol->fragment_anchor = as->stmt_count;
    if (!minias_current_location(as, &symbol->value)) {
        symbol->defined = false;
        return false;
    }
    return true;
}

/* Materialize real GNU literal pools at the requested location, NOT by
 * ignoring .ltorg: each retained request gets an aligned DATA fragment,
 * a fragment-anchored local label and a normal ABS32/ABS64 relocation.
 * Zero-fill inside executable sections matches GNU GAS, not NOP padding. */
static bool flush_literal_pools(MiniAs *as,size_t line,bool all) {
    int original_section=as->current_section;
    uint32_t original_subsection=as->current_subsection;
    size_t i;
    unsigned width;
    const bool width_order=as->target->literal_pool_order==
                           MINIAS_LITERAL_POOL_WIDTH_ASC;
    /* GNU GAS scheduling is TargetInfo policy, not a hard-coded AArch64
     * rule inside the Common assembler.  Each selected entry becomes an
     * owned ALIGN fragment, symbol anchor, and typed DATA fragment below;
     * no synthetic .balign/.long/.quad source text is reparsed. */
    for(width=4U;width<=8U;width*=2U) {
      for(i=0U;i<as->literal_count;++i) {
        MiniAsLiteral *entry=&as->literals[i];
        MiniAsStmt *fragment;
        MiniAsMCData *data;
        MiniAsSymbol *target;
        uint64_t current,pad;
        if(width_order && entry->width!=width)continue;
        if(!width_order && width!=4U)continue;
        if(entry->emitted ||
           (!all && (entry->section!=as->current_section ||
                     entry->subsection!=as->current_subsection)))continue;
        if(all) {
            as->current_section=entry->section;
            as->current_subsection=entry->subsection;
            if(!minias_ensure_subsection(as,as->current_section,as->current_subsection))
                goto failure;
        }
        if(!minias_current_location(as,&current))goto failure;
        pad=((uint64_t)entry->width-(current&((uint64_t)entry->width-1U)))&
            ((uint64_t)entry->width-1U);
        if(!add_stmt(as,MINIAS_FRAGMENT_ALIGNMENT,".literal_pool_align","",line,
                     (uint32_t)pad,entry->width))goto failure;
        fragment=&as->stmts[as->stmt_count-1U];
        fragment->align_has_fill=true;
        fragment->align_fill=0U;
        if((uint64_t)entry->width>as->sections[(size_t)entry->section].align)
            as->sections[(size_t)entry->section].align=entry->width;
        if(!advance_current_location(as,pad) ||
           !define_label(as,entry->label,line))goto failure;

        /* Literal payload is a standard typed MCExpr+SymbolID. Data bytes
         * and relocation fixups follow exactly the ordinary DATA emitter;
         * the pool cannot depend on another pass over synthetic strings. */
        if(!add_stmt(as,MINIAS_FRAGMENT_FIXED_DATA,".literal_pool_data","",line,
                     entry->width,1U))goto failure;
        fragment=&as->stmts[as->stmt_count-1U];
        data=calloc(1U,sizeof(*data));
        if(data==NULL) {
            minias_set_error(as,"out-of-memory:literal-pool-data");
            goto failure;
        }
        data->fields=calloc(1U,sizeof(*data->fields));
        if(data->fields==NULL) {
            free(data);
            minias_set_error(as,"out-of-memory:literal-pool-fields");
            goto failure;
        }
        data->width=entry->width;data->count=1U;
        if(entry->expression.name_id>=as->symbol_name_count ||
           as->symbol_names[entry->expression.name_id].name==NULL) {
            minias_set_error(as,"literal-pool-symbol-ref-invalid");
            minias_mcdata_destroy(data);goto failure;
        }
        target=minias_get_symbol(as,
                     as->symbol_names[entry->expression.name_id].name,true);
        if(target==NULL) {minias_mcdata_destroy(data);goto failure;}
        data->fields[0].kind=MINIAS_MC_EXPR_SYMBOL;
        data->fields[0].has_lhs=true;
        data->fields[0].lhs_symbol_index=(size_t)(target-as->symbols);
        data->fields[0].lhs_addend=entry->expression.addend;
        fragment->mcdata=data;
        if(!advance_current_location(as,entry->width))goto failure;
        entry->emitted=true;
      }
    }
    as->current_section=original_section;
    as->current_subsection=original_subsection;
    return true;
failure:
    as->current_section=original_section;
    as->current_subsection=original_subsection;
    return false;
}

/* Tentative COMMON definitions have ELF st_value=alignment, st_size=length.
 * Explicit .local symbols instead acquire real .bss space through the
 * existing Common fragment/section layout and symbol-anchor pipeline. */
static bool parse_comm(MiniAs *as, const char *args, size_t line) {
    char *copy=minias_strdup(args);
    char *name,*size_text,*align_text=NULL,*first,*second;
    MiniAsSymbol *symbol;
    uint64_t alignment=1U;
    int64_t size,alignment_value;
    size_t k;
    if(!copy)return false;
    first=strchr(copy,',');
    if(!first)goto invalid;
    *first++='\0';
    second=strchr(first,',');
    if(second) {
        *second++='\0';
        if(strchr(second,','))goto invalid;
        align_text=minias_trim(second);
        if(!*align_text)goto invalid;
    }
    name=minias_trim(copy);size_text=minias_trim(first);
    if(!minias_expr_symbol_start(*name) || !*size_text)goto invalid;
    for(k=1U;name[k];++k)
        if(!minias_expr_symbol_continue(name[k]))goto invalid;
    if(!minias_evaluate_absolute_expression(as,size_text,&size) || size<0)
        goto invalid;
    if(align_text) {
        if(!minias_evaluate_absolute_expression(as,align_text,&alignment_value) ||
           alignment_value<=0)goto invalid;
        alignment=(uint64_t)alignment_value;
    } else {
        /* GNU x86 uses min(nextpow2(size),16). This default is a target
         * descriptor; unverified target dialects must fail closed. */
        unsigned cap=as->target->common_default_alignment_cap;
        if(cap==0U || (cap&(cap-1U))!=0U)goto invalid;
        while(alignment<(uint64_t)size && alignment<(uint64_t)cap)
            alignment*=2U;
    }
    if((alignment&(alignment-1U))!=0U)goto invalid;
    symbol=minias_get_symbol(as,name,true);
    if(!symbol){free(copy);return false;}
    if(symbol->alias_pending || symbol->type==MINIAS_STT_FUNC ||
       symbol->bind==MINIAS_STB_WEAK)goto invalid;
    if(symbol->defined) {
        /* GNU GAS retains first tentative size, maxes alignment. */
        if(symbol->section!=MINIAS_SECTION_COMMON)goto invalid;
        if(alignment>symbol->value)symbol->value=alignment;
        free(copy);return true;
    }
    if(symbol->explicit_local_binding) {
        MiniAsPendingLocalCommon *pending;
        int bss;
        if((uint64_t)size>UINT32_MAX || alignment>UINT32_MAX)goto invalid;
        bss=minias_ensure_section(as,".bss",MINIAS_SHT_NOBITS,
                                  MINIAS_SHF_ALLOC|MINIAS_SHF_WRITE,1U);
        if(bss<0){free(copy);return false;}
        if(!minias_grow_array((void **)&as->pending_local_commons,
                &as->pending_local_common_capacity,
                sizeof(*as->pending_local_commons),
                as->pending_local_common_count+1U)) {
            minias_set_error(as,"out-of-memory:pending-local-common");
            free(copy);return false;
        }
        pending=&as->pending_local_commons[as->pending_local_common_count++];
        pending->symbol_index=(size_t)(symbol-as->symbols);
        pending->size=(uint64_t)size;
        pending->alignment=alignment;
        pending->line=line;
        pending->source_site=minias_source_current_site(as);
        /* Mark this SymbolID as defined immediately, preserving GNU's
         * duplicate-symbol and late .globl/.hidden semantics, but do not
         * anchor its BSS offset until the complete subsection-0 source
         * stream exists. It will become a real Fragment anchor at EOF. */
        symbol->defined=true;
        symbol->section=bss;
        symbol->subsection=0U;
        symbol->value=0U;
        symbol->type=MINIAS_STT_OBJECT;
        symbol->size=(uint64_t)size;
        free(copy);return true;
    }
    symbol->defined=true;
    symbol->bind=MINIAS_STB_GLOBAL;
    symbol->type=MINIAS_STT_OBJECT;
    symbol->section=MINIAS_SECTION_COMMON;
    symbol->subsection=0U;
    symbol->size=(uint64_t)size;
    symbol->value=alignment;
    free(copy);return true;
invalid:
    minias_set_error(as,"unsupported-common:%s:line=%zu",args,line);
    free(copy);return false;
}

/* GAS allocates local .comm symbols after every explicit .bss subsection-0
 * declaration, even if the source's .comm appeared first. It does NOT wait
 * until numbered subsections 1+ have been laid out. Scheduling these typed
 * ALIGN/ZERO Fragments at source EOF under the same BSS subsection gives
 * the shared MCLayout solver the exact final allocation order and keeps the
 * existing SymbolID/ELF REL section-fold path unmodified. */
static bool flush_pending_local_commons(MiniAs *as) {
    int saved_section=as->current_section;
    uint32_t saved_subsection=as->current_subsection;
    MiniAsStableSiteId saved_source_site=minias_source_current_site(as);
    bool ok=true;
    size_t i;
    int bss;
    if(as->pending_local_common_count==0U)return true;
    bss=minias_find_section(as,".bss");
    if(bss<0) {
        minias_set_error(as,"pending-local-common-missing-bss");
        return false;
    }
    as->current_section=bss;
    as->current_subsection=0U;
    if(minias_ensure_subsection(as,bss,0U)==NULL)ok=false;
    for(i=0U;ok && i<as->pending_local_common_count;++i) {
        const MiniAsPendingLocalCommon *pending=&as->pending_local_commons[i];
        MiniAsSymbol *symbol;
        uint64_t here;
        char align_buf[32],size_buf[32];
        if(pending->symbol_index>=as->symbol_count) {
            minias_set_error(as,"pending-local-common-invalid-symbol");
            ok=false;break;
        }
        symbol=&as->symbols[pending->symbol_index];
        if(!symbol->defined || symbol->section!=bss ||
           symbol->has_fragment_anchor) {
            minias_set_error(as,"pending-local-common-mutated:%s",symbol->name);
            ok=false;break;
        }
        minias_source_restore_site(as,pending->source_site);
        (void)snprintf(align_buf,sizeof(align_buf),"%llu",
                       (unsigned long long)pending->alignment);
        (void)snprintf(size_buf,sizeof(size_buf),"%llu",
                       (unsigned long long)pending->size);
        if(!handle_align(as,".balign",align_buf,pending->line) ||
           !minias_current_location(as,&here)) {
            ok=false;break;
        }
        symbol->value=here;
        symbol->subsection=0U;
        symbol->has_fragment_anchor=true;
        symbol->fragment_anchor=as->stmt_count;
        symbol->fragment_anchor_addend=0;
        if(!add_data_stmt(as,".zero",size_buf,pending->line)) {
            ok=false;break;
        }
    }
    as->current_section=saved_section;
    as->current_subsection=saved_subsection;
    minias_source_restore_site(as,saved_source_site);
    return ok;
}

/* GNU .reloc uses a section-relative OFFSET and symbolic relocation
 * name; no object bytes are emitted by this directive. The Target owns
 * the relocation name->semantic fixup mapping, and the existing Common
 * Fixup/ELF path serializes the entry. Never invent a numeric ELF type. */
static bool handle_named_reloc(MiniAs *as,const char *args,size_t line) {
    char *copy=minias_strdup(args),*comma1,*comma2,*offset_text,*name,*symbol=NULL;
    MiniAsFixupKind kind=MINIAS_FIXUP_NONE;
    MiniAsSymbolExpr expression;
    unsigned width=0U;
    int64_t offset;
    bool ok;
    if(!copy){minias_set_error(as,"out-of-memory:.reloc");return false;}
    comma1=strchr(copy,',');
    if(!comma1){free(copy);goto invalid;}
    *comma1++='\0';
    comma2=strchr(comma1,',');
    if(comma2)*comma2++='\0';
    offset_text=minias_trim(copy);
    name=minias_trim(comma1);
    if(comma2)symbol=minias_trim(comma2);
    ok=as->current_section>=0 && as->current_subsection==0U &&
       as->target->parse_named_relocation &&
       as->target->parse_named_relocation(minias_target_abi_view(as),name,&kind,&width) &&
       minias_evaluate_absolute_expression(as,offset_text,&offset) && offset>=0 &&
       (symbol==NULL || (*symbol!='\0' && !strchr(symbol,',') &&
                         minias_parse_symbol_addend(symbol,&expression)));
    if(ok && symbol!=NULL)ok=minias_add_section_fixup(as,
                 MINIAS_FIXUP_ORIGIN_EXPLICIT_SECTION,
                 as->current_section,(uint64_t)offset,
                 kind,width,expression.name,expression.addend,false);
    else if(ok)ok=minias_record_fixup(as,as->current_section,
                  (uint64_t)offset,kind,width,NULL,SIZE_MAX,0,
                  MINIAS_FIXUP_ORIGIN_EXPLICIT_SECTION,true,false,SIZE_MAX);
    free(copy);
    if(ok)return true;
invalid:
    minias_set_error(as,"bad-directive:.reloc:%s:line=%zu",args,line);
    return false;
}

/* GAS `.ident` is NOT parser metadata. It emits a mergeable string in the
 * ELF .comment section. If `.ident` creates the section, GAS seeds it with
 * one leading NUL; if source code already created .comment, no seed is
 * added. The payload is ordinary immutable MC DATA, so it participates in
 * the exact same Section/Subsection/Layout/ELF lifetime as all other data.
 * An implicit section switch must not change `.previous` or the current
 * source subsection. */
static bool handle_ident(MiniAs *as,char *args,size_t line) {
    const int prior=minias_find_section(as,".comment");
    const int section=minias_ensure_section(as,".comment",
                      MINIAS_SHT_PROGBITS,
                      MINIAS_SHF_MERGE|MINIAS_SHF_STRINGS,1U);
    const int saved_section=as->current_section;
    const uint32_t saved_subsection=as->current_subsection;
    bool ok;
    char seed[]="1";
    if(section<0)return false;
    /* GAS upgrades an existing .comment to SHF_MERGE|SHF_STRINGS and
     * sh_entsize=1 when `.ident` is used, preserving any explicit flags
     * such as SHF_ALLOC. A later section declaration does not undo that. */
    as->sections[(size_t)section].flags |=
        MINIAS_SHF_MERGE|MINIAS_SHF_STRINGS;
    as->sections[(size_t)section].entry_size=1U;
    as->current_section=section;
    as->current_subsection=0U;
    ok=minias_ensure_subsection(as,section,0U)!=NULL &&
       (prior>=0 || add_data_stmt(as,".zero",seed,line)) &&
       add_data_stmt(as,".asciz",args,line);
    as->current_section=saved_section;
    as->current_subsection=saved_subsection;
    if(!ok && as->error[0]=='\0')
        minias_set_error(as,"bad-directive:.ident:line=%zu",line);
    return ok;
}


static bool lower_source_statement(MiniAs *as, char *text, size_t line) {
    char *space;
    char *op;
    char *args;
    uint32_t size;
    bool had_leading_label=false;
    bool source_handled=false;
    MiniAsCommonDirectiveKind directive_kind=MINIAS_COMMON_DIRECTIVE_OTHER;

    text = minias_trim(text);
    if (*text == '\0') {
        return true;
    }

    /* GNU GCC/Clang inline-assembler boundaries are exact standalone
     * source markers. AArch64 uses # for immediate operands, so do not
     * treat arbitrary #foo or #number as a comment. */
    if (strcmp(text, "#APP") == 0 || strcmp(text, "#NO_APP") == 0 ||
        strcmp(text, "#NOAPP") == 0) return true;
    /* A leading '# ' begins a GNU preprocessor line or comment; a target
     * immediate such as '#42' is still an operand, never a comment. */
    if(text[0]=='#' && (text[1]=='#' ||
                        isspace((unsigned char)text[1])))return true;

    /* Conditional grammar and stack state belong to source expansion.
     * Dispatch before labels so inactive arms cannot define symbols. */
    if(!minias_source_try_conditional(as,text,line,&source_handled))return false;
    if(source_handled)return true;
    if (!minias_source_conditional_active(as)) {
        return true;
    }

    for (;;) {
        char *token_end = text;
        char *colon;

        while (*token_end != '\0' && *token_end != ':' &&
               !isspace((unsigned char)*token_end)) {
            ++token_end;
        }
        colon = token_end;
        while (*colon == ' ' || *colon == '\t') {
            ++colon;
        }
        if (*colon != ':') {
            break;
        }
        *token_end = '\0';
        had_leading_label=true;
        if (!define_label(as, text, line)) {
            return false;
        }
        text = minias_trim(colon + 1);
        if (*text == '\0') {
            return true;
        }
    }
    /* A GNU statement after LABEL: still starts a fresh directive.
     * Reenter the same shared conditional dispatcher instead of treating
     * '0: .ifc \arg,x' as an unknown target instruction/directive. */
    if(had_leading_label)return lower_source_statement(as,text,line);

    {
        char *glued=NULL;
        /* GNU's .inst(EXPR) and .long(EXPR) syntax permits interior
         * whitespace and nested C-preprocessed arithmetic. It must be
         * recognized BEFORE normal token splitting, which would otherwise
         * treat the first space inside EXPR as the end of the directive.
         * The width hook is shared and TargetInfo-owned: no A64-specific
         * lexer path and no accepting arbitrary unknown .directives. */
        if(text[0]=='.') {
            char *open=strchr(text,'(');
            char *space_first=strpbrk(text," \t");
            if(open && (!space_first || open<space_first)) {
                char *end=text+strlen(text);
                while(end>open+1U && isspace((unsigned char)end[-1]))--end;
                if(end>open+1U && end[-1]==')') {
                    *open='\0';
                    if(data_width(as,text)!=0U) {
                        end[-1]='\0';
                        glued=minias_trim(open+1U);
                    }else *open='(';
                }
            }
        }
        if(glued)args=glued;
        else {
            space=text;
            while(*space!='\0' && !isspace((unsigned char)*space))
                ++space;
            if(*space) {
                *space='\0';
                args=minias_trim(space+1U);
            }else args=space;
        }
    }
    op = text;

    /* GNU GAS also accepts assignments without surrounding spaces:
     * vector=0x20 and vector=vector+1 inside .rept. Both spellings must
     * reach the SAME mutable Common .set owner, not Target ISA parsing.
     * Keep `.L2=.` anchored at the current fragment boundary. */
    if ((args[0] == '=' && args[1] != '=') ||
        (strchr(op, '=') != NULL && args[0] == '\0')) {
        char *attached = strchr(op, '=');
        char *rhs;
        char *set_args;
        if (attached != NULL) {
            *attached++ = '\0';
            rhs = minias_trim(attached);
        } else {
            rhs = minias_trim(args + 1U);
        }
        size_t name_len = strlen(op), rhs_len = strlen(rhs), k;
        bool ok;
        if (!rhs_len || !name_len ||
            !(isalpha((unsigned char)op[0]) ||
              op[0] == '.' || op[0] == '_' || op[0] == '$')) {
            minias_set_error(as,"bad-gnu-assignment:line=%zu",line);
            return false;
        }
        for (k=1U;k<name_len;++k) {
            if (!(isalnum((unsigned char)op[k]) ||
                  op[k] == '.' || op[k] == '_' || op[k] == '$')) {
                minias_set_error(as,"bad-gnu-assignment-symbol:line=%zu",line);
                return false;
            }
        }
        if (!strcmp(rhs,".")) return define_label(as,op,line);
        if (name_len > SIZE_MAX-rhs_len-2U) {
            minias_set_error(as,"gnu-assignment-too-long:line=%zu",line);
            return false;
        }
        set_args=malloc(name_len+rhs_len+2U);
        if (!set_args) {
            minias_set_error(as,"out-of-memory:gnu-assignment");
            return false;
        }
        memcpy(set_args,op,name_len);
        set_args[name_len]=',';
        memcpy(set_args+name_len+1U,rhs,rhs_len+1U);
        if (!is_persistent_symbol_assignment(rhs))
            as->layout_sensitive=true;
        ok=parse_set(as,set_args,line);
        free(set_args);
        return ok;
    }

    /* Macro lookup/expansion is a source-layer action, before Target
     * directives so defined macro names keep their GNU precedence. */
    if(!minias_source_try_macro_invocation(as,op,args,line,&source_handled))
        return false;
    if(source_handled)return true;

    if (strcmp(op, ".set") == 0 || strcmp(op, ".equ") == 0) {
        const char *comma = strchr(args, ',');
        if (comma == NULL ||
            !is_persistent_symbol_assignment(comma + 1U))
            as->layout_sensitive = true;
    }
    /* Space/skip/zero depend on Fragment layout, not parser control flow:
     * the shared MCLayout fixed point resolves dot and forward symbols.
     * Dot-dependent alignment grammar remains conservatively reparsed. */
    if ((strcmp(op, ".align") == 0 || strcmp(op, ".balign") == 0 ||
         strcmp(op, ".p2align") == 0) &&
        strchr(args, '.') != NULL) {
        as->layout_sensitive = true;
    }
    /* Target syntax can be a dotted directive (`.option`) or a bare
     * identifier followed by a target directive (`alias .req x0`). Common
     * cannot enumerate the latter forms without leaking ISA grammars. Ask
     * the owning Target first for EVERY non-macro statement; an unhandled
     * instruction then reaches the mandatory parsed-MCInst interface. */
    if (as->target->parse_target_directive != NULL) {
        bool handled = false;
        MiniAsDirectiveContext directive={as};
        if (!as->target->parse_target_directive(&directive, op, args,
                                                line, &handled)) {
            return false;
        }
        if (handled) return true;
    }
    if (op[0] == '.') {
        directive_kind=common_directive_kind(as,op);
        if (as->target->validate_common_directive != NULL) {
            MiniAsDirectiveContext directive={as};
            if(!as->target->validate_common_directive(&directive,
                                                      directive_kind,op,line))
                return false;
        }
    }
    if (op[0] == '.' && strcmp(op, ".insn") != 0) {
        if (strcmp(op, ".cfi_sections") == 0 ||
            ((as->cfi_debug_frame || as->cfi_eh_frame) &&
             strncmp(op, ".cfi_", 5U) == 0) ||
            (as->target->cfi_implicit_eh_frame &&
             strncmp(op, ".cfi_", 5U) == 0)) {
            /* Every enabled backend must emit real .eh_frame records;
             * silent CFI boundary-only acceptance is not correctness. */
            if (as->target->cfi_implicit_eh_frame &&
                !as->cfi_debug_frame && !as->cfi_eh_frame &&
                strcmp(op, ".cfi_sections") != 0)
                as->cfi_eh_frame = true;
            return minias_cfi_directive(as,op,args,line);
        }
        if (!strcmp(op,".file") || !strcmp(op,".loc"))
            return minias_debug_directive(as,op,args,line);
        /* Never accept CFI boundaries without a CIE/FDE output plan. A
         * fifth Target lacking implicit EH must declare .cfi_sections or
         * explicitly reject the source, not silently lose unwind data. */
        if (strncmp(op,".cfi_",5U)==0) {
            minias_set_error(as,"unsupported-target-cfi:%s:line=%zu",op,line);
            return false;
        }
        if (strcmp(op, ".text") == 0) {
            return switch_section(as,
                                  ".text",
                                  NULL,
                                  NULL,
                                  false,0U,0U,
                                  MINIAS_SHT_PROGBITS,
                                  MINIAS_SHF_ALLOC | MINIAS_SHF_EXECINSTR,
                                  minias_target_exec_alignment(as));
        }
        if (strcmp(op, ".data") == 0) {
            return switch_section(as,
                                  ".data",
                                  NULL,
                                  NULL,
                                  false,0U,0U,
                                  MINIAS_SHT_PROGBITS,
                                  MINIAS_SHF_ALLOC | MINIAS_SHF_WRITE,
                                  1U);
        }
        if (strcmp(op, ".bss") == 0) {
            return switch_section(as,
                                  ".bss",
                                  NULL,
                                  NULL,
                                  false,0U,0U,
                                  MINIAS_SHT_NOBITS,
                                  MINIAS_SHF_ALLOC | MINIAS_SHF_WRITE,
                                  1U);
        }
        if (strcmp(op, ".section") == 0) {
            return parse_section_directive(as, args, line);
        }
        if (strcmp(op, ".globl") == 0 || strcmp(op, ".global") == 0) {
            return handle_symbol_list(as, args, MINIAS_STB_GLOBAL, true, 0U, false);
        }
        if (strcmp(op, ".local") == 0) {
            return handle_symbol_list(as,args,MINIAS_STB_LOCAL,true,0U,false);
        }
        if (strcmp(op, ".weak") == 0) {
            return handle_symbol_list(as, args, MINIAS_STB_WEAK, true, 0U, false);
        }
        if (strcmp(op, ".extern") == 0) {
            return handle_symbol_list(as, args, MINIAS_STB_GLOBAL, true, 0U, false);
        }
        if (strcmp(op, ".hidden") == 0) {
            return handle_symbol_list(as, args, 0U, false, MINIAS_STV_HIDDEN, true);
        }
        if (strcmp(op, ".internal") == 0) {
            return handle_symbol_list(as, args, 0U, false, MINIAS_STV_INTERNAL, true);
        }
        if (strcmp(op, ".protected") == 0) {
            return handle_symbol_list(as, args, 0U, false, MINIAS_STV_PROTECTED, true);
        }
        if (strcmp(op, ".type") == 0) {
            return parse_type(as, args, line);
        }
        if (strcmp(op, ".comm") == 0) {
            return parse_comm(as, args, line);
        }
        if (strcmp(op, ".size") == 0) {
            return parse_size(as, args, line);
        }
        if (strcmp(op, ".equ") == 0) {
            return parse_equ(as, args, line);
        }
        if (strcmp(op, ".set") == 0) {
            return parse_set(as, args, line);
        }
        if (strcmp(op, ".reloc") == 0) {
            return handle_named_reloc(as,args,line);
        }
        if (strcmp(op,".ltorg")==0 || strcmp(op,".pool")==0) {
            if(!as->target->supports_literal_pools || *args) {
                minias_set_error(as,"unsupported-directive:%s:line=%zu",
                                 op,line);
                return false;
            }
            as->layout_sensitive=true;
            return flush_literal_pools(as,line,false);
        }
        if (strcmp(op, ".previous") == 0) {
            int swap_section = as->current_section;
            uint32_t swap_subsection = as->current_subsection;
            if (as->previous_section < 0 ||
                (size_t)as->previous_section >= as->section_count) {
                minias_set_error(as, "bad-directive:.previous:line=%zu", line);
                return false;
            }
            as->current_section = as->previous_section;
            as->current_subsection = as->previous_subsection;
            as->previous_section = swap_section;
            as->previous_subsection = swap_subsection;
            return minias_ensure_subsection(as,
                                     as->current_section,
                                     as->current_subsection) != NULL;
        }
        if (strcmp(op, ".subsection") == 0) {
            uint64_t number;
            if (!minias_parse_u64(args, &number) || number > 8192U) {
                minias_set_error(as, "bad-directive:.subsection:%s:line=%zu", args, line);
                return false;
            }
            if ((uint32_t)number != as->current_subsection) {
                as->previous_section = as->current_section;
                as->previous_subsection = as->current_subsection;
                as->current_subsection = (uint32_t)number;
            }
            return minias_ensure_subsection(as,
                                     as->current_section,
                                     as->current_subsection) != NULL;
        }
        if (strcmp(op, ".pushsection") == 0) {
            return push_section(as, args, line);
        }
        if (strcmp(op, ".popsection") == 0) {
            return pop_section(as, line);
        }
        if(!minias_source_try_control_directive(as,op,args,line,
                                                &source_handled))return false;
        if(source_handled)return true;
        if (strcmp(op, ".ident") == 0) {
            return handle_ident(as,args,line);
        }
        if (strcmp(op, ".align") == 0 || strcmp(op, ".balign") == 0 ||
            strcmp(op, ".p2align") == 0) {
            return handle_align(as, op, args, line);
        }
        if (directive_kind == MINIAS_COMMON_DIRECTIVE_ORG) {
            char *rewritten_args = NULL;
            bool ok;

            if (!minias_numeric_refs_rewrite(as,
                                            args,
                                            &rewritten_args,
                                            line)) {
                return false;
            }
            ok = handle_org(as, rewritten_args, line);
            free(rewritten_args);
            return ok;
        }
        if (directive_kind == MINIAS_COMMON_DIRECTIVE_RAW_DATA) {
            char *rewritten_args = NULL;
            bool ok;

            if (!minias_numeric_refs_rewrite(as,
                                            args,
                                            &rewritten_args,
                                            line)) {
                return false;
            }
            if(strcmp(op,".inst")==0 &&
               !minias_maybe_align_instruction(as,line)) {
                free(rewritten_args);
                return false;
            }
            ok = add_data_stmt(as, op, rewritten_args, line);
            if(ok && strcmp(op,".inst")==0 &&
               as->target->implicit_instruction_alignment!=0U) {
                MiniAsSubsection *sub=minias_ensure_subsection(
                    as,as->current_section,as->current_subsection);
                if(sub==NULL)ok=false;
                else sub->pending_instruction_alignment=false;
            }
            free(rewritten_args);
            return ok;
        }
        minias_set_error(as, "unsupported-directive:%s:line=%zu", op, line);
        return false;
    }

    {
        char *rewritten_args = NULL;
        size_t statement_id;
        bool ok;
        if (!minias_numeric_refs_rewrite(as, args, &rewritten_args, line))
            return false;
        if(!minias_maybe_align_instruction(as,line)) {
            free(rewritten_args);
            return false;
        }
        statement_id=as->stmt_count;
        /* ISA/ABI validation is owned by the target's one typed parser.
         * The obsolete pre-parse validation callback had no live Target. */
        ok = add_stmt(as, MINIAS_FRAGMENT_INSTRUCTION, op, rewritten_args,
                      line, 0U, 1U);
        if (ok) {
            const MiniAsMeasureView measure = { .target_state=as->target_state };
            MiniAsMeasureErrorSink errors = { .as=as };
            ok = as->target->measure_mcinst(
                &measure, &errors, &as->stmts[statement_id], &size);
        }
        if (!ok) {
            free(rewritten_args);
            return false;
        }
        size_t forced_minimum_size=0U;
        if(!minias_source_graph_lookup_forced_branch(as,
                                              &as->stmts[statement_id],
                                              &forced_minimum_size)) {
            free(rewritten_args);
            return false;
        }
        /* A source-replayed branch keeps its monotone minimum *alternative*,
         * not merely a boolean "forced long" bit.  RV C16 -> B32 -> long8
         * needs two reparses when a dot-sensitive .set/.if is present.
         * Each step is still supplied by the Target's typed MC plan. */
        while (size<forced_minimum_size) {
            MiniAsTargetRelaxPlan plan;
            as->stmts[statement_id].size=size;
            if (!minias_query_target_relax_plan(
                    as,&as->stmts[statement_id],&plan) ||
                plan.terminal_size<=size ||
                plan.terminal_size>forced_minimum_size) {
                minias_set_error(as, "invalid-target-relaxation:%s:line=%zu",
                                 op, line);
                free(rewritten_args);
                return false;
            }
            size = plan.terminal_size;
        }
        as->stmts[statement_id].size = size;
        free(rewritten_args);
    }
    return advance_current_location(as, size);
}

/* Common consumes one Source-owned, stable-site statement event. No private
 * session pointer or lowerer function is exported into the Source module.
 * Label recursion stays within the same single Common parser. */
static bool consume_source_statement(void *user,
                                     const MiniAsSourceStatementEvent *event) {
    MiniAs *as=(MiniAs *)user;
    if(as==NULL || event==NULL || event->text==NULL ||
       event->site==MINIAS_SITE_NONE ||
       event->site!=minias_source_current_site(as)) {
        if(as!=NULL)minias_set_error(as,"invalid-source-statement-site");
        return false;
    }
    return lower_source_statement(as,event->text,event->line);
}

bool minias_common_parse_source_graph(MiniAs *as,
                                       MiniAsSourceGraph *graph) {
    const MiniAsSourceStatementSink statements={
        .user=as,
        .consume=consume_source_statement
    };
    if(!minias_source_context_process_graph(as,graph,&statements))
        return false;
    if(minias_source_context_layout_sensitive(as->source))
        as->layout_sensitive=true;
    if(as->cfi_active) {
        minias_set_error(as,"unterminated-directive:.cfi_startproc");
        return false;
    }
    /* SourceGraph owns the synthetic EOF source identity; Common retains
     * the actual literal-pool and local-COMMON fragment materialization. */
    if(!minias_source_graph_begin_eof(as))return false;
    if(as->target->supports_literal_pools &&
       !flush_literal_pools(as,minias_source_graph_physical_lines(graph),true))
        return false;
    if(!flush_pending_local_commons(as))return false;
    if(!minias_source_graph_validate_fragment_sites(
           as,as->stmts,as->stmt_count))return false;
    /* Finalization already refreshes the alias DAG after assigning final
     * subsection bases. Its subsequent .size end-anchor work changes no
     * alias source values, so a second whole-graph walk is redundant. */
    return minias_finalize_subsection_layout(as);
}

