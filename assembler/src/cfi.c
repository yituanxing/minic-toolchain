#include "minias_internal.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Common persistent DWARF .debug_frame emission. ELF relocations are the
 * existing semantic MINIAS_FIXUP_ABS, resolved by the target ABI mapping.
 * CFI instructions retain anchored labels, not first-pass byte offsets:
 * relaxation, subsection concatenation and macros therefore share exactly
 * the same stable fragment identities as text and data.
 *
 * Deliberately fail closed on unimplemented .eh_frame, expressions and CFI
 * opcodes; treating source unwind metadata as no-ops is not valid assembly.
 */
enum {
    CFI_DEF_CFA = 1, CFI_DEF_CFA_OFFSET, CFI_OFFSET, CFI_RESTORE,
    CFI_REMEMBER, CFI_RESTORE_STATE, CFI_NEGATE_RA,
    CFI_DEF_CFA_REGISTER, CFI_UNDEFINED, CFI_SAME_VALUE
};

static bool cfi_grow(void **where, size_t *capacity, size_t count,
                     size_t item_size) {
    size_t limit = *capacity == 0U ? 16U : *capacity;
    void *next;
    if (count > SIZE_MAX / item_size) return false;
    while (limit < count) {
        if (limit > SIZE_MAX / 2U) return false;
        limit *= 2U;
    }
    if (limit == *capacity) return true;
    next = realloc(*where, limit * item_size);
    if (next == NULL) return false;
    *where = next;
    *capacity = limit;
    return true;
}

static bool cfi_integer(const char *text, int64_t *value) {
    char *end;
    long long number;
    if (*text == '#') ++text;
    if (*text == '\0') return false;
    errno = 0;
    number = strtoll(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0') return false;
    *value = (int64_t)number;
    return true;
}

static bool cfi_register(const char *text, unsigned *reg) {
    int64_t number;
    if (!cfi_integer(text, &number) || number < 0 || number > 255)
        return false;
    *reg = (unsigned)number;
    return true;
}

static bool cfi_two(const char *text, unsigned *reg, int64_t *value) {
    char left[40], right[64];
    const char *comma = strchr(text, ',');
    size_t count;
    if (comma == NULL || strchr(comma + 1U, ',') != NULL) return false;
    count = (size_t)(comma - text);
    while (count != 0U && text[count-1U] == ' ') --count;
    if (count == 0U || count >= sizeof(left)) return false;
    memcpy(left,text,count);left[count]='\0';
    ++comma;
    while (*comma == ' ' || *comma == '\t') ++comma;
    count = strlen(comma);
    while (count != 0U && (comma[count-1U] == ' ' ||
                           comma[count-1U] == '\t')) --count;
    if (count == 0U || count >= sizeof(right)) return false;
    memcpy(right,comma,count);right[count]='\0';
    return cfi_register(left,reg) && cfi_integer(right,value);
}

/* Define a hidden source-location anchor at the current fragment boundary. */
bool minias_anchor_here(MiniAs *as, size_t *index) {
    char name[72];
    MiniAsSymbol *sym;
    size_t i;
    int rc;
    rc = snprintf(name,sizeof(name),".Lminias_cfi_%zu",
                  as->cfi_anchor_counter++);
    if (rc < 0 || (size_t)rc >= sizeof(name)) return false;
    sym = minias_get_symbol(as,name,true);
    if (sym == NULL || sym->defined) return false;
    *index = (size_t)(sym-as->symbols);
    sym->defined = true;
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
    if (!cfi_grow((void **)&as->cfi_ops,&as->cfi_op_capacity,
                  as->cfi_op_count+1U,sizeof(*as->cfi_ops))) return false;
    if (!minias_anchor_here(as,&anchor)) return false;
    op=&as->cfi_ops[as->cfi_op_count++];
    op->symbol_index=anchor;op->kind=kind;op->reg=reg;op->value=value;
    proc=&as->cfi_procs[as->cfi_proc_count-1U];
    ++proc->op_count;
    return true;
}

bool minias_cfi_directive(MiniAs *as,const char *op,
                          const char *args,size_t line) {
    MiniAsCfiProc *proc;
    int64_t value=0;
    unsigned reg=0U, kind=0U;
    if (!strcmp(op,".cfi_sections")) {
        bool debug=!strcmp(args,".debug_frame");
        bool eh=!strcmp(args,".eh_frame");
        if (as->cfi_active || as->cfi_proc_count != 0U ||
            (as->cfi_debug_frame || as->cfi_eh_frame) ||
            (!debug && !eh)) {
            minias_set_error(as,"unsupported-cfi-sections:%s:line=%zu",args,line);
            return false;
        }
        if (as->target->cfi_code_align == 0U ||
            as->target->cfi_data_align == 0 ||
            as->target->cfi_address_size != 8U ||
            (eh && as->target->elf_machine!=62U)) {
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
            !cfi_grow((void **)&as->cfi_procs,&as->cfi_proc_capacity,
                      as->cfi_proc_count+1U,sizeof(*as->cfi_procs)) ||
            !minias_anchor_here(as,&anchor)) {
            minias_set_error(as,"bad-cfi-startproc:line=%zu",line);
            return false;
        }
        proc=&as->cfi_procs[as->cfi_proc_count++];
        memset(proc,0,sizeof(*proc));
        proc->begin_symbol=anchor;
        proc->end_symbol=SIZE_MAX;
        proc->first_op=as->cfi_op_count;
        proc->simple=strcmp(args,"simple")==0;
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
    if (!strcmp(op,".cfi_def_cfa") || !strcmp(op,".cfi_offset")) {
        if (!cfi_two(args,&reg,&value) ||
            (!strcmp(op,".cfi_def_cfa") && value<0) ||
            (!strcmp(op,".cfi_offset") &&
             (value>0 || value%as->target->cfi_data_align != 0))) goto bad;
        kind=!strcmp(op,".cfi_def_cfa")?CFI_DEF_CFA:CFI_OFFSET;
    } else if (!strcmp(op,".cfi_def_cfa_offset") ||
               !strcmp(op,".cfi_def_cfa_register") ||
               !strcmp(op,".cfi_restore") ||
               !strcmp(op,".cfi_undefined") ||
               !strcmp(op,".cfi_same_value")) {
        if (!cfi_integer(args,&value) || value<0 ||
            (!strcmp(op,".cfi_def_cfa_offset")?false:value>255))goto bad;
        if (!strcmp(op,".cfi_def_cfa_offset"))
            kind=CFI_DEF_CFA_OFFSET;
        else {
            reg=(unsigned)value;
            kind=!strcmp(op,".cfi_def_cfa_register")?CFI_DEF_CFA_REGISTER:
                 !strcmp(op,".cfi_restore")?CFI_RESTORE:
                 !strcmp(op,".cfi_undefined")?CFI_UNDEFINED:CFI_SAME_VALUE;
        }
    } else if (!strcmp(op,".cfi_remember_state") && !*args) {
        kind=CFI_REMEMBER;
        ++proc->state_depth;
    } else if (!strcmp(op,".cfi_restore_state") && !*args &&
               proc->state_depth>0U) {
        kind=CFI_RESTORE_STATE;
        --proc->state_depth;
    } else if (!strcmp(op,".cfi_window_save") && !*args &&
               as->target->cfi_negate_ra_opcode!=0U) {
        kind=CFI_NEGATE_RA;
    } else goto bad;
    if (cfi_append(as,kind,reg,value)) return true;
    minias_set_error(as,"out-of-memory:cfi-op:line=%zu",line);
    return false;
bad:
    minias_set_error(as,"unsupported-dwarf-cfi:%s:%s:line=%zu",op,args,line);
    return false;
}

static bool put8(MiniAs *as,int sec,uint8_t value) {
    return minias_section_append(as,sec,&value,1U);
}
static bool put32(MiniAs *as,int sec,uint32_t value) {
    unsigned char b[4];
    unsigned i;
    for (i=0U;i<4U;++i)b[i]=(unsigned char)(value>>(i*8U));
    return minias_section_append(as,sec,b,4U);
}
static bool put64(MiniAs *as,int sec,uint64_t value) {
    unsigned char b[8];
    unsigned i;
    for (i=0U;i<8U;++i)b[i]=(unsigned char)(value>>(i*8U));
    return minias_section_append(as,sec,b,8U);
}
static bool uleb(MiniAs *as,int sec,uint64_t value) {
    do {
        uint8_t byte=(uint8_t)(value&0x7fU);
        value>>=7U;
        if(value)byte|=0x80U;
        if(!put8(as,sec,byte))return false;
    }while(value);
    return true;
}
static bool sleb(MiniAs *as,int sec,int64_t value) {
    for(;;) {
        uint8_t byte=(uint8_t)((uint64_t)value&0x7fU);
        bool last;
        value>>=7U;
        last=(value==0 && (byte&0x40U)==0U) ||
             (value==-1 && (byte&0x40U)!=0U);
        if(!last)byte|=0x80U;
        if(!put8(as,sec,byte))return false;
        if(last)return true;
    }
}
static bool finish_length(MiniAs *as,int sec,size_t begin) {
    MiniAsSection *section=&as->sections[(size_t)sec];
    uint32_t length;
    unsigned i;
    while((section->size-begin)%4U!=0U)
        if(!put8(as,sec,0U))return false;
    section=&as->sections[(size_t)sec];
    if(section->size-begin-4U>UINT32_MAX)return false;
    length=(uint32_t)(section->size-begin-4U);
    for(i=0U;i<4U;++i)
        section->data[begin+i]=(unsigned char)(length>>(i*8U));
    return true;
}
static bool advance_cfi(MiniAs *as,int sec,uint64_t from,uint64_t to) {
    uint64_t delta;
    unsigned align=as->target->cfi_code_align;
    if(to<from || (to-from)%align!=0U)return false;
    delta=(to-from)/align;
    if(delta==0U)return true;
    if(delta<=63U)return put8(as,sec,(uint8_t)(0x40U|(uint8_t)delta));
    if(delta<=255U)return put8(as,sec,2U) && put8(as,sec,(uint8_t)delta);
    if(delta<=65535U)
        return put8(as,sec,3U) && put8(as,sec,(uint8_t)delta) &&
               put8(as,sec,(uint8_t)(delta>>8U));
    if(delta>UINT32_MAX)return false;
    return put8(as,sec,4U) && put32(as,sec,(uint32_t)delta);
}
static bool encode_cfi(MiniAs *as,int sec,const MiniAsCfiOp *op) {
    uint64_t magnitude;
    switch(op->kind) {
        case CFI_DEF_CFA:
            return put8(as,sec,0x0cU) && uleb(as,sec,op->reg) &&
                   uleb(as,sec,(uint64_t)op->value);
        case CFI_DEF_CFA_OFFSET:
            return put8(as,sec,0x0eU) && uleb(as,sec,(uint64_t)op->value);
        case CFI_OFFSET:
            magnitude=(uint64_t)(op->value/as->target->cfi_data_align);
            if(op->reg<64U)
                return put8(as,sec,(uint8_t)(0x80U|op->reg)) &&
                       uleb(as,sec,magnitude);
            return put8(as,sec,0x05U) && uleb(as,sec,op->reg) &&
                   uleb(as,sec,magnitude);
        case CFI_RESTORE:
            if(op->reg<64U)
                return put8(as,sec,(uint8_t)(0xc0U|op->reg));
            return put8(as,sec,0x06U) && uleb(as,sec,op->reg);
        case CFI_REMEMBER: return put8(as,sec,0x0aU);
        case CFI_RESTORE_STATE: return put8(as,sec,0x0bU);
        case CFI_NEGATE_RA:
            return put8(as,sec,(uint8_t)as->target->cfi_negate_ra_opcode);
        case CFI_DEF_CFA_REGISTER:
            return put8(as,sec,0x0dU) && uleb(as,sec,op->reg);
        case CFI_UNDEFINED:
            return put8(as,sec,0x07U) && uleb(as,sec,op->reg);
        case CFI_SAME_VALUE:
            return put8(as,sec,0x08U) && uleb(as,sec,op->reg);
        default: return false;
    }
}


/* One Common x86 SysV .eh_frame CIE. Other targets continue to require
 * independently validated ABI/encoding profiles instead of fake no-ops.
 * GNU GAS's zR CIE uses pcrel/sdata4 (0x1b) FDE initial locations. */
static bool emit_eh_cie(MiniAs *as,int section,bool simple,bool signal,
                        size_t *cie_begin) {
    size_t base=as->sections[(size_t)section].size;
    *cie_begin=base;
    if(!put32(as,section,0U) || !put32(as,section,0U) ||
       !put8(as,section,1U) ||
       !put8(as,section,'z') || !put8(as,section,'R') ||
       (signal && !put8(as,section,'S')) ||
       !put8(as,section,0U) ||
       !uleb(as,section,as->target->cfi_code_align) ||
       !sleb(as,section,as->target->cfi_data_align) ||
       !put8(as,section,(uint8_t)as->target->cfi_return_reg) ||
       !uleb(as,section,1U) || !put8(as,section,0x1bU))return false;
    if(!simple) {
        unsigned reg=as->target->cfi_return_reg;
        if(reg>=64U || as->target->cfi_stack_reg!=7U ||
           as->target->cfi_data_align!=-8 ||
           !put8(as,section,0x0cU) ||
           !uleb(as,section,as->target->cfi_stack_reg) ||
           !uleb(as,section,8U) ||
           !put8(as,section,(uint8_t)(0x80U|reg)) ||
           !uleb(as,section,1U))return false;
    }
    return finish_length(as,section,base);
}

bool minias_cfi_emit(MiniAs *as) {
    size_t i;
    int section;
    MiniAsSection *data;
    bool eh=as->cfi_eh_frame;
    bool cie_known[2][2]={{false,false},{false,false}};
    size_t cie_offset[2][2]={{0U,0U},{0U,0U}};
    if((!as->cfi_debug_frame && !eh) || as->cfi_proc_count==0U)
        return true;
    if(eh && as->target->elf_machine!=62U) {
        minias_set_error(as,"unsupported-target-eh-frame");
        return false;
    }
    section=minias_ensure_section(as,eh?".eh_frame":".debug_frame",
        MINIAS_SHT_PROGBITS,eh?MINIAS_SHF_ALLOC:0U,8U);
    if(section<0)return false;
    data=&as->sections[(size_t)section];
    if(!eh) {
        /* DWARF32 CIE v3, absolute CIE pointer for .debug_frame.
         * Original Common ownership and byte format remain unchanged. */
        if(!put32(as,section,0U) ||
           !put32(as,section,UINT32_MAX) ||
           !put8(as,section,3U) ||
           !put8(as,section,0U) ||
           !uleb(as,section,as->target->cfi_code_align) ||
           !sleb(as,section,as->target->cfi_data_align) ||
           !uleb(as,section,as->target->cfi_return_reg) ||
           !put8(as,section,0x0cU) ||
           !uleb(as,section,as->target->cfi_stack_reg) ||
           !uleb(as,section,0U) ||
           !finish_length(as,section,0U))return false;
    }
    for(i=0U;i<as->cfi_proc_count;++i) {
        const MiniAsCfiProc *proc=&as->cfi_procs[i];
        const MiniAsSymbol *begin,*end;
        size_t j,base,ptr;
        uint64_t loc;
        if(proc->begin_symbol>=as->symbol_count ||
           proc->end_symbol>=as->symbol_count)return false;
        begin=&as->symbols[proc->begin_symbol];
        end=&as->symbols[proc->end_symbol];
        if(!begin->defined || !end->defined ||
           begin->section<0 || begin->section!=end->section ||
           end->value<begin->value ||
           end->value-begin->value>UINT32_MAX) {
            minias_set_error(as,"invalid-cfi-procedure-layout");
            return false;
        }
        if(eh) {
            unsigned simple=proc->simple?1U:0U;
            unsigned signal=proc->signal_frame?1U:0U;
            if(!cie_known[simple][signal]) {
                if(!emit_eh_cie(as,section,proc->simple,
                                proc->signal_frame,
                                &cie_offset[simple][signal]))return false;
                cie_known[simple][signal]=true;
            }
        }
        data=&as->sections[(size_t)section];
        base=data->size;
        if(eh) {
            size_t cie=cie_offset[proc->simple?1U:0U]
                                   [proc->signal_frame?1U:0U];
            if(base+4U<cie || base+4U-cie>UINT32_MAX)return false;
            if(!put32(as,section,0U) ||
               !put32(as,section,(uint32_t)(base+4U-cie)))return false;
            ptr=as->sections[(size_t)section].size;
            if(!put32(as,section,0U) ||
               !put32(as,section,(uint32_t)(end->value-begin->value)) ||
               !uleb(as,section,0U) ||
               !minias_add_fixup(as,section,ptr,MINIAS_FIXUP_PREL32,4U,
                                 begin->name,0))return false;
        } else {
            if(!put32(as,section,0U) ||
               !put32(as,section,0U))return false;
            ptr=as->sections[(size_t)section].size;
            if(!put64(as,section,0U) ||
               !put64(as,section,end->value-begin->value) ||
               !minias_add_fixup(as,section,ptr,MINIAS_FIXUP_ABS,8U,
                                 begin->name,0))return false;
        }
        loc=begin->value;
        for(j=0U;j<proc->op_count;++j) {
            const MiniAsCfiOp *op=&as->cfi_ops[proc->first_op+j];
            const MiniAsSymbol *symbol;
            if(op->symbol_index>=as->symbol_count)return false;
            symbol=&as->symbols[op->symbol_index];
            if(!symbol->defined || symbol->section!=begin->section ||
               symbol->value>end->value ||
               !advance_cfi(as,section,loc,symbol->value) ||
               !encode_cfi(as,section,op)) {
                minias_set_error(as,"invalid-cfi-event-layout");
                return false;
            }
            loc=symbol->value;
        }
        /* GNU x86_64 .eh_frame FDEs end on an absolute 8-byte
         * section boundary. CIEs still use DWARF32 4-byte padding;
         * relative-to-FDE padding is insufficient when a simple CIE
         * occupies 20 bytes, leaving the next FDE at offset 0x14. */
        if(eh) {
            while((as->sections[(size_t)section].size&7U)!=0U)
                if(!put8(as,section,0U))return false;
        }
        if(!finish_length(as,section,base))return false;
        data=&as->sections[(size_t)section];
    }
    return true;
}
