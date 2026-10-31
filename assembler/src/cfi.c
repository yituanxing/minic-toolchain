#include "minias_internal.h"

#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Common persistent DWARF CFI for .debug_frame and .eh_frame. ELF
 * relocations use existing semantic Fixup kinds and target ABI mapping.
 * CFI instructions retain anchored labels, not first-pass byte offsets:
 * relaxation, subsection concatenation and macros therefore share exactly
 * the same stable fragment identities as text and data.
 *
 * Unsupported CFI directives and operand forms fail closed; treating source
 * unwind metadata as no-ops is not valid assembly.
 */
MiniAsCfiABI minias_cfi_abi(const MiniAs *as) {
    MiniAsCfiABI abi;
    abi.code_align=as->target->cfi_code_align;
    abi.data_align=as->target->cfi_data_align;
    abi.return_reg=as->target->cfi_return_reg;
    abi.stack_reg=as->target->cfi_stack_reg;
    abi.address_size=as->target->cfi_address_size;
    abi.eh_fde_alignment=as->target->cfi_eh_fde_alignment;
    abi.eh_final_alignment=as->target->cfi_eh_final_alignment;
    abi.negate_ra_opcode=as->target->cfi_negate_ra_opcode;
    abi.initial_cfa_offset=as->target->cfi_initial_cfa_offset;
    abi.initial_ra_saved=as->target->cfi_initial_ra_saved;
    if(as->target->resolve_cfi_abi!=NULL)
        as->target->resolve_cfi_abi(as,&abi);
    return abi;
}


/* DWARF CFI operands are absolute GNU MCExprs, not necessarily decimal
 * literals.  Reuse Common's precedence parser and resolved .set/.equ graph;
 * undefined or relocatable symbols must never be treated as zero. */
static bool cfi_integer(MiniAs *as,const char *text,int64_t *value) {
    uint64_t bits;
    if (*text == '#') ++text;
    /* The absolute DATA evaluator rejects .text/.data label addresses,
     * whereas the general-purpose layout evaluator may legitimately use
     * them as locations.  CFI operands are never relocation expressions.
     * After proving absoluteness, prefer signed arithmetic (notably /, >>);
     * retain GNU's 64-bit wrapping constant behavior when signed overflow
     * prevents that second interpretation. */
    if(*text=='\0' || !minias_parse_absolute_data_bits(as,text,&bits))
        return false;
    if(minias_evaluate_absolute_operand(as,text,value))return true;
    *value=bits<=(uint64_t)INT64_MAX ? (int64_t)bits :
           -1-(int64_t)(UINT64_MAX-bits);
    return true;
}

static bool cfi_register(MiniAs *as,const char *text,unsigned *reg) {
    int64_t number;
    /* DW_CFA operands use ULEB128 register numbers.  The 8-bit limit
     * belongs only to DWARF v1's CIE return-address register field, not
     * to individual .cfi_* register operands.  GNU accepts registers
     * above 255 on all supported targets. */
    if (cfi_integer(as,text,&number)) {
        if(number<0 || (uint64_t)number>UINT_MAX)return false;
        *reg=(unsigned)number;
        return true;
    }
    return as->target->resolve_cfi_register!=NULL &&
           as->target->resolve_cfi_register(as,text,reg);
}

static bool cfi_two(MiniAs *as,const char *text,unsigned *reg,int64_t *value,
                    bool register_rhs) {
    char *copy=minias_strdup(text),*comma,*lhs,*rhs;
    bool ok=false;
    if(copy==NULL)return false;
    comma=strchr(copy,',');
    if(comma==NULL || strchr(comma+1U,',')!=NULL)goto done;
    *comma++='\0';
    lhs=minias_trim(copy);
    rhs=minias_trim(comma);
    if(*lhs && *rhs && cfi_register(as,lhs,reg)) {
        if(register_rhs) {
            unsigned destination;
            if(cfi_register(as,rhs,&destination)) {
                *value=(int64_t)destination;
                ok=true;
            }
        } else ok=cfi_integer(as,rhs,value);
    }
done:
    free(copy);
    return ok;
}

/* Define a hidden source-location anchor at the current fragment boundary. */
bool minias_anchor_here(MiniAs *as, size_t *index) {
    char name[72];
    MiniAsSymbol *sym;
    size_t i;
    if(!minias_make_private_symbol_name(name,sizeof(name),"cfi",
                                        as->cfi_anchor_counter++))return false;
    sym = minias_get_symbol(as,name,true);
    if (sym == NULL || sym->defined) return false;
    *index = (size_t)(sym-as->symbols);
    sym->defined = true;
    if (as->target->cfi_anchors_hidden)
        sym->visibility = MINIAS_STV_HIDDEN;
    sym->section = as->current_section;
    sym->subsection = as->current_subsection;
    sym->has_fragment_anchor = true;
    sym->fragment_anchor = as->stmt_count;
    for (i = 0U; i < as->subsection_count; ++i)
        if (as->subsections[i].section == as->current_section &&
            as->subsections[i].number == as->current_subsection) {
            sym->value = as->subsections[i].size;
            return true;
        }
    minias_set_error(as,"cfi-no-active-subsection");
    return false;
}

static bool cfi_append(MiniAs *as,unsigned kind,unsigned reg,int64_t value) {
    MiniAsCfiOp *op;
    MiniAsCfiProc *proc;
    size_t anchor;
    if (!minias_grow_array((void **)&as->cfi_ops,&as->cfi_op_capacity,
                           sizeof(*as->cfi_ops),as->cfi_op_count+1U)) return false;
    if (!minias_anchor_here(as,&anchor)) return false;
    op=&as->cfi_ops[as->cfi_op_count++];
    memset(op,0,sizeof(*op));
    op->symbol_index=anchor;op->kind=kind;op->reg=reg;op->value=value;
    proc=&as->cfi_procs[as->cfi_proc_count-1U];
    ++proc->op_count;
    return true;
}

/* Common tracks GNU CFA state independently of ISA encodings. A nested
 * remember/restore is a typed stack snapshot, not an instruction replay;
 * the newest unmatched remember owns the CFA offset to be restored. */
static bool cfi_saved_cfa(const MiniAs *as,const MiniAsCfiProc *proc,
                          int64_t *value,bool *known) {
    size_t j=proc->first_op+proc->op_count;
    size_t nested=0U;
    while(j>proc->first_op) {
        const MiniAsCfiOp *ev=&as->cfi_ops[--j];
        if(ev->kind==CFI_RESTORE_STATE)++nested;
        else if(ev->kind==CFI_REMEMBER) {
            if(nested==0U) {
                *value=ev->value;
                *known=ev->reg!=0U;
                return true;
            }
            --nested;
        }
    }
    return false;
}

/* GNU .cfi_escape accepts raw DWARF bytes. First validate the whole
 * sequence; malformed input cannot leave a partially appended event.
 * Each byte becomes a typed CFI event at the same Fragment position. */
static bool cfi_escape_directive(MiniAs *as,const char *args,size_t line) {
    uint8_t *bytes;
    size_t count=0U,cap=strlen(args);
    const char *p=args;
    if(cap==0U)goto bad;
    bytes=malloc(cap);
    if(!bytes) {
        minias_set_error(as,"out-of-memory:cfi-escape");
        return false;
    }
    while(*p) {
        char token[80];
        size_t n=0U;
        int64_t value;
        while(*p==' ' || *p=='\t')++p;
        while(*p && *p!=',') {
            if(n+1U>=sizeof(token))goto invalid;
            token[n++]=*p++;
        }
        while(n && (token[n-1U]==' ' || token[n-1U]=='\t'))--n;
        token[n]='\0';
        if(!n || !cfi_integer(as,token,&value) || value<0 || value>255)
            goto invalid;
        bytes[count++]=(uint8_t)value;
        if(*p==',') {
            ++p;
            if(!*p)goto invalid;
        }
    }
    /* One typed CFI event, one fragment anchor, regardless of byte count.
     * The retained payload is owned by the procedure's CFI event array;
     * the serializer never parses the source or fabricates byte anchors. */
    if(!cfi_append(as,CFI_ESCAPE,0U,0)) {
        free(bytes);
        minias_set_error(as,"cfi-escape-anchor:line=%zu",line);
        return false;
    }
    as->cfi_ops[as->cfi_op_count-1U].bytes=bytes;
    as->cfi_ops[as->cfi_op_count-1U].byte_count=count;
    return true;
invalid:
    free(bytes);
bad:
    minias_set_error(as,"unsupported-dwarf-cfi:.cfi_escape:%s:line=%zu",
                     args,line);
    return false;
}

/* GNU permits the same CFI event stream to be materialized in more than one
 * unwind section. Keep selection independent of serialization: the immutable
 * procedure/event anchors are reused by each output writer pass. */
static bool cfi_section_selection(const char *args,bool *debug,bool *eh) {
    const char *p=args;
    *debug=false;
    *eh=false;
    for(;;) {
        const char *begin;
        size_t len;
        bool *selected;
        while(*p==' ' || *p=='\t')++p;
        begin=p;
        while(*p && *p!=',' && *p!=' ' && *p!='\t')++p;
        len=(size_t)(p-begin);
        if(len==12U && memcmp(begin,".debug_frame",len)==0)
            selected=debug;
        else if(len==9U && memcmp(begin,".eh_frame",len)==0)
            selected=eh;
        else return false;
        if(*selected)return false;
        *selected=true;
        while(*p==' ' || *p=='\t')++p;
        if(*p=='\0')return true;
        if(*p!=',')return false;
        ++p;
        if(*p=='\0')return false;
    }
}

bool minias_cfi_directive(MiniAs *as,const char *op,
                          const char *args,size_t line) {
    MiniAsCfiProc *proc;
    int64_t value=0;
    unsigned reg=0U, kind=0U;
    if (!strcmp(op,".cfi_sections")) {
        bool debug=false,eh=false;
        if (as->cfi_active || as->cfi_proc_count != 0U ||
            (as->cfi_debug_frame || as->cfi_eh_frame) ||
            !cfi_section_selection(args,&debug,&eh)) {
            minias_set_error(as,"unsupported-cfi-sections:%s:line=%zu",args,line);
            return false;
        }
        if (minias_cfi_abi(as).code_align == 0U ||
            minias_cfi_abi(as).data_align == 0 ||
            minias_cfi_abi(as).address_size !=
                (as->output_elf32 ? 4U : 8U) ||
            (eh && !as->target->cfi_implicit_eh_frame)) {
            minias_set_error(as,"unsupported-target-dwarf-cfi:line=%zu",line);
            return false;
        }
        as->cfi_debug_frame=debug;
        as->cfi_eh_frame=eh;
        return true;
    }
    if (!as->cfi_debug_frame && !as->cfi_eh_frame) {
        minias_set_error(as,"cfi-debug-frame-not-selected:line=%zu",line);
        return false;
    }
    if (!strcmp(op,".cfi_startproc")) {
        size_t anchor;
        if (as->cfi_active ||
            (*args!='\0' && strcmp(args,"simple")!=0) ||
            as->current_section<0 ||
            !minias_grow_array((void **)&as->cfi_procs,&as->cfi_proc_capacity,
                               sizeof(*as->cfi_procs),as->cfi_proc_count+1U) ||
            !minias_anchor_here(as,&anchor)) {
            minias_set_error(as,"bad-cfi-startproc:line=%zu",line);
            return false;
        }
        proc=&as->cfi_procs[as->cfi_proc_count++];
        memset(proc,0,sizeof(*proc));
        proc->begin_symbol=anchor;
        proc->end_symbol=SIZE_MAX;
        proc->first_op=as->cfi_op_count;
        proc->return_column=minias_cfi_abi(as).return_reg;
        proc->simple=strcmp(args,"simple")==0;
        proc->cfa_known=true;
        proc->cfa_offset=proc->simple?0:
                           (int64_t)minias_cfi_abi(as).initial_cfa_offset;
        as->cfi_active=true;
        return true;
    }
    if (!as->cfi_active || as->cfi_proc_count==0U) {
        minias_set_error(as,"cfi-outside-procedure:%s:line=%zu",op,line);
        return false;
    }
    proc=&as->cfi_procs[as->cfi_proc_count-1U];
    if (!strcmp(op,".cfi_signal_frame") && !*args) {
        proc->signal_frame=true;
        return true;
    }
    if (!strcmp(op,".cfi_return_column")) {
        /* GNU emits this in the CIE, even if the directive appears after
         * the first instruction.  DWARF CIE v1 stores an 8-bit column;
         * reject overflow here instead of silently truncating it later. */
        if (!cfi_register(as,args,&reg) || reg>UINT8_MAX)goto bad;
        proc->return_column=reg;
        return true;
    }
    if (!strcmp(op,".cfi_endproc")) {
        size_t anchor;
        if (*args!='\0' || proc->state_depth!=0U ||
            !minias_anchor_here(as,&anchor)) {
            minias_set_error(as,"bad-cfi-endproc:line=%zu",line);
            return false;
        }
        proc->end_symbol=anchor;as->cfi_active=false;
        return true;
    }
    if(!strcmp(op,".cfi_escape"))
        return cfi_escape_directive(as,args,line);
    if (!strcmp(op,".cfi_def_cfa") || !strcmp(op,".cfi_offset") ||
        !strcmp(op,".cfi_rel_offset") ||
        !strcmp(op,".cfi_val_offset") || !strcmp(op,".cfi_register")) {
        bool def=!strcmp(op,".cfi_def_cfa");
        bool rel=!strcmp(op,".cfi_rel_offset");
        bool offset=rel || !strcmp(op,".cfi_offset");
        bool val=!strcmp(op,".cfi_val_offset");
        if(!cfi_two(as,args,&reg,&value,!def && !rel && !offset && !val) ||
           (def && value<0) ||
           (rel && (!proc->cfa_known ||
                    value<INT64_MIN+proc->cfa_offset)))goto bad;
        /* GNU .cfi_rel_offset is relative to the current CFA base
         * register; convert it to an actual CFA-relative save before the
         * ordinary signed/unsigned DW_CFA_offset encoding decision. */
        if(rel)value-=proc->cfa_offset;
        if(((offset || val) &&
            value%minias_cfi_abi(as).data_align!=0) ||
           (!def && !offset && !val &&
            (value<0 || (uint64_t)value>UINT_MAX)))
            goto bad;
        kind=def?CFI_DEF_CFA:offset?CFI_OFFSET:
             val?CFI_VAL_OFFSET:CFI_REGISTER;
        if(def) {
            proc->cfa_offset=value;
            proc->cfa_known=true;
        }
    } else if (!strcmp(op,".cfi_def_cfa_offset") ||
               !strcmp(op,".cfi_def_cfa_register") ||
               !strcmp(op,".cfi_restore") ||
               !strcmp(op,".cfi_undefined") ||
               !strcmp(op,".cfi_same_value")) {
        if (!strcmp(op,".cfi_def_cfa_offset")) {
            if(!cfi_integer(as,args,&value) || value<0)goto bad;
            kind=CFI_DEF_CFA_OFFSET;
            proc->cfa_offset=value;
            proc->cfa_known=true;
        }
        else {
            if(!cfi_register(as,args,&reg))goto bad;
            kind=!strcmp(op,".cfi_def_cfa_register")?CFI_DEF_CFA_REGISTER:
                 !strcmp(op,".cfi_restore")?CFI_RESTORE:
                 !strcmp(op,".cfi_undefined")?CFI_UNDEFINED:CFI_SAME_VALUE;
        }
    } else if(!strcmp(op,".cfi_adjust_cfa_offset")) {
        int64_t delta;
        if(!cfi_integer(as,args,&delta) || !proc->cfa_known ||
           (delta>0 && proc->cfa_offset>INT64_MAX-delta) ||
           (delta<0 && proc->cfa_offset<INT64_MIN-delta) ||
           proc->cfa_offset+delta<0)goto bad;
        proc->cfa_offset+=delta;
        value=proc->cfa_offset;
        kind=CFI_DEF_CFA_OFFSET;
    } else if(!strcmp(op,".cfi_remember_state") && !*args) {
        kind=CFI_REMEMBER;
        reg=proc->cfa_known?1U:0U;
        value=proc->cfa_offset;
        ++proc->state_depth;
    } else if(!strcmp(op,".cfi_restore_state") && !*args &&
              proc->state_depth>0U) {
        int64_t saved;
        bool known;
        if(!cfi_saved_cfa(as,proc,&saved,&known))goto bad;
        kind=CFI_RESTORE_STATE;
        proc->cfa_offset=saved;
        proc->cfa_known=known;
        --proc->state_depth;
    } else if ((!strcmp(op,".cfi_window_save") ||
                !strcmp(op,".cfi_negate_ra_state")) && !*args &&
               minias_cfi_abi(as).negate_ra_opcode!=0U) {
        kind=CFI_NEGATE_RA;
    } else goto bad;
    if (cfi_append(as,kind,reg,value)) return true;
    minias_set_error(as,"out-of-memory:cfi-op:line=%zu",line);
    return false;
bad:
    minias_set_error(as,"unsupported-dwarf-cfi:%s:%s:line=%zu",op,args,line);
    return false;
}

