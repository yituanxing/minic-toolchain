#include "minias_internal.h"

#include <limits.h>
#include <stdint.h>
#include <stddef.h>

/* Canonical DWARF CFI serializer. The parser never writes an output section,
 * the Target never encodes DW_CFA bytes, and CFI source instructions are
 * immutable anchored events. A Target contributes its CFI ABI profile and
 * ELF relocation mapping, not a private copy of this writer. */

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
/* Debug FDE initial location and address-range fields follow the effective
 * Target ABI pointer width, not the host size_t or an invented ELF64 rule. */
static bool put_cfi_address(MiniAs *as,int sec,uint64_t value) {
    unsigned size=minias_cfi_abi(as).address_size;
    if(size==4U) {
        if(value>UINT32_MAX)return false;
        return put32(as,sec,(uint32_t)value);
    }
    if(size==8U)return put64(as,sec,value);
    minias_set_error(as,"unsupported-cfi-address-width:%u",size);
    return false;
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
    unsigned align=minias_cfi_abi(as).code_align;
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
            magnitude=(uint64_t)(op->value/minias_cfi_abi(as).data_align);
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
            return put8(as,sec,(uint8_t)minias_cfi_abi(as).negate_ra_opcode);
        case CFI_DEF_CFA_REGISTER:
            return put8(as,sec,0x0dU) && uleb(as,sec,op->reg);
        case CFI_UNDEFINED:
            return put8(as,sec,0x07U) && uleb(as,sec,op->reg);
        case CFI_SAME_VALUE:
            return put8(as,sec,0x08U) && uleb(as,sec,op->reg);
        case CFI_REGISTER:
            return put8(as,sec,0x09U) && uleb(as,sec,op->reg) &&
                   uleb(as,sec,(uint64_t)op->value);
        case CFI_VAL_OFFSET:
            magnitude=(uint64_t)(op->value/minias_cfi_abi(as).data_align);
            return put8(as,sec,0x14U) && uleb(as,sec,op->reg) &&
                   uleb(as,sec,magnitude);
        case CFI_ESCAPE:
            return op->bytes!=NULL && op->byte_count!=0U &&
                   minias_section_append(as,sec,op->bytes,op->byte_count);
        default: return false;
    }
}


/* GNU DWARF32 zR CIE: one engine for every Target psABI.
 * Only TargetInfo specifies initial CFA/RA policy and relocation mapping;
 * CFI bytes, CIE variant reuse and anchored FDEs remain Common-owned. */
static bool emit_eh_cie(MiniAs *as,int sec,bool simple,bool signal,
                        size_t *cie_begin) {
    MiniAsCfiABI abi=minias_cfi_abi(as);
    size_t begin=as->sections[(size_t)sec].size;
    *cie_begin=begin;
    if(abi.code_align==0U || abi.data_align>=0 ||
       abi.return_reg>255U || abi.address_size==0U)return false;
    if(!put32(as,sec,0U) || !put32(as,sec,0U) ||
       !put8(as,sec,1U) ||
       !put8(as,sec,'z') || !put8(as,sec,'R') ||
       (signal && !put8(as,sec,'S')) || !put8(as,sec,0U) ||
       !uleb(as,sec,abi.code_align) || !sleb(as,sec,abi.data_align) ||
       !put8(as,sec,(uint8_t)abi.return_reg) ||
       !uleb(as,sec,1U) || !put8(as,sec,0x1bU))return false;
    if(!simple) {
        if(!put8(as,sec,0x0cU) ||
           !uleb(as,sec,abi.stack_reg) ||
           !uleb(as,sec,abi.initial_cfa_offset))return false;
        if(abi.initial_ra_saved) {
            unsigned magnitude=(unsigned)(-abi.data_align);
            if(abi.return_reg>=64U || magnitude==0U ||
               abi.initial_cfa_offset%magnitude!=0U ||
               !put8(as,sec,(uint8_t)(0x80U|abi.return_reg)) ||
               !uleb(as,sec,abi.initial_cfa_offset/magnitude))return false;
        }
    }
    return finish_length(as,sec,begin);
}

/* One serializer invocation per selected output section. In particular,
 * selecting both .eh_frame and .debug_frame must not mean choosing whichever
 * boolean happens to be true: they have different CIE pointers and relocation
 * policies, but share precisely the same finalized CFI event stream. */
static bool cfi_emit_section(MiniAs *as,bool eh) {
    size_t i;
    int section;
    MiniAsSection *data;
    bool cie_known[2][2]={{false,false},{false,false}};
    size_t cie_offset[2][2]={{0U,0U},{0U,0U}};
    if(eh && (!as->target->cfi_implicit_eh_frame ||
              minias_cfi_abi(as).address_size!=(as->output_elf32?4U:8U) ||
              as->target->supports_fixup==NULL ||
              !as->target->supports_fixup(MINIAS_FIXUP_PREL32,4U))) {
        minias_set_error(as,"unsupported-target-eh-frame");
        return false;
    }
    section=minias_ensure_section(as,eh?".eh_frame":".debug_frame",
        MINIAS_SHT_PROGBITS,eh?MINIAS_SHF_ALLOC:0U,
        eh?minias_cfi_abi(as).address_size:8U);
    if(section<0)return false;
    data=&as->sections[(size_t)section];
    if(!eh) {
        /* DWARF32 CIE v3, absolute CIE pointer for .debug_frame.
         * Original Common ownership and byte format remain unchanged. */
        if(!put32(as,section,0U) ||
           !put32(as,section,UINT32_MAX) ||
           !put8(as,section,3U) ||
           !put8(as,section,0U) ||
           !uleb(as,section,minias_cfi_abi(as).code_align) ||
           !sleb(as,section,minias_cfi_abi(as).data_align) ||
           !uleb(as,section,minias_cfi_abi(as).return_reg) ||
           !put8(as,section,0x0cU) ||
           !uleb(as,section,minias_cfi_abi(as).stack_reg) ||
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
               !minias_add_section_fixup(as,
               MINIAS_FIXUP_ORIGIN_SYNTHETIC_SECTION,section,ptr,MINIAS_FIXUP_PREL32,4U,
                                 begin->name,0))return false;
        } else {
            if(!put32(as,section,0U) ||
               !put32(as,section,0U))return false;
            ptr=as->sections[(size_t)section].size;
            if(!put_cfi_address(as,section,0U) ||
               !put_cfi_address(as,section,end->value-begin->value) ||
               !minias_add_section_fixup(as,
               MINIAS_FIXUP_ORIGIN_SYNTHETIC_SECTION,section,ptr,MINIAS_FIXUP_ABS,
                                 minias_cfi_abi(as).address_size,
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
            unsigned align=minias_cfi_abi(as).address_size;
            if(align==0U || (align&(align-1U))!=0U)return false;
            while((as->sections[(size_t)section].size&
                   (size_t)(align-1U))!=0U)
                if(!put8(as,section,0U))return false;
        }
        if(!finish_length(as,section,base))return false;
        data=&as->sections[(size_t)section];
    }
    return true;
}

bool minias_cfi_emit(MiniAs *as) {
    if(as->cfi_proc_count==0U)return true;
    if(as->cfi_eh_frame && !cfi_emit_section(as,true))return false;
    if(as->cfi_debug_frame && !cfi_emit_section(as,false))return false;
    return true;
}
