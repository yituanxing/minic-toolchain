#include "minias_internal.h"

#include <limits.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

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
/* Some psABIs preserve linker-visible CFI code-distance arithmetic even when
 * the two labels are already defined in the current object.  GAS emits a
 * fixed-width zero operand followed by SET/ADD and SUB relocations; emitting
 * the numeric delta loses the linker's relocation/relaxation contract. */
static bool cfi_advance_fixups(MiniAs *as,int sec,size_t field,
                              size_t from_symbol,size_t to_symbol,
                              unsigned width) {
    MiniAsFixupKind positive=as->target->cfi_advance_reloc_mode==2U ?
        MINIAS_FIXUP_SET : MINIAS_FIXUP_ADD;
    if(from_symbol>=as->symbol_count || to_symbol>=as->symbol_count ||
       (as->target->cfi_advance_reloc_mode!=1U &&
        as->target->cfi_advance_reloc_mode!=2U))return false;
    return minias_add_section_fixup(as,MINIAS_FIXUP_ORIGIN_SYNTHETIC_SECTION,
               sec,field,positive,width,as->symbols[to_symbol].name,0) &&
           minias_add_section_fixup(as,MINIAS_FIXUP_ORIGIN_SYNTHETIC_SECTION,
               sec,field,MINIAS_FIXUP_SUB,width,as->symbols[from_symbol].name,0);
}

/* A compact DW_CFA_advance_loc embeds the delta in the low six bits of its
 * opcode.  That constant becomes stale if a linker-relaxable instruction
 * *between* the source anchors shrinks after the object has been written.
 * Inspect retained section/Fragment fixups, not source spellings or an
 * ISA-specific relocation number: the Target identifies its own markers. */
static bool cfi_span_has_linker_relaxation(MiniAs *as,size_t from_symbol,
                                          size_t to_symbol,bool *crosses) {
    const MiniAsSymbol *from=&as->symbols[from_symbol];
    const MiniAsSymbol *to=&as->symbols[to_symbol];
    size_t i;
    *crosses=false;
    if(as->target->cfi_linker_relaxation_marker==NULL ||
       from->value==to->value)return true;
    if(from->section!=to->section)return false;
    for(i=0U;i<as->fixup_count;++i) {
        const MiniAsFixup *fixup=&as->fixups[i];
        uint64_t site;
        if(fixup->section!=from->section ||
           !as->target->cfi_linker_relaxation_marker(fixup->kind))
            continue;
        if(fixup->origin==MINIAS_FIXUP_ORIGIN_FRAGMENT) {
            const MiniAsStmt *owner;
            if(fixup->fragment_id>=as->stmt_count)return false;
            owner=&as->stmts[fixup->fragment_id];
            if(owner->section!=fixup->section ||
               owner->offset>UINT64_MAX-fixup->fragment_offset)return false;
            site=owner->offset+fixup->fragment_offset;
        } else {
            site=fixup->offset;
        }
        if(site>=from->value && site<to->value) {
            *crosses=true;
            return true;
        }
    }
    return true;
}

static bool advance_cfi(MiniAs *as,int sec,size_t from_symbol,
                        size_t to_symbol,bool eh) {
    uint64_t delta;
    unsigned align=minias_cfi_abi(as).code_align;
    uint64_t from,to;
    bool reloc=eh && as->target->cfi_advance_reloc_mode!=0U;
    bool crosses_relaxation=false;
    size_t field;
    if(from_symbol>=as->symbol_count || to_symbol>=as->symbol_count)
        return false;
    from=as->symbols[from_symbol].value;
    to=as->symbols[to_symbol].value;
    if(to<from || (to-from)%align!=0U)return false;
    delta=(to-from)/align;
    if(delta==0U)return true;
    if(delta<=63U) {
        if(!reloc)return put8(as,sec,(uint8_t)(0x40U|(uint8_t)delta));
        if(!cfi_span_has_linker_relaxation(as,from_symbol,to_symbol,
                                           &crosses_relaxation))return false;
        if(!crosses_relaxation)
            return put8(as,sec,(uint8_t)(0x40U|(uint8_t)delta));
        /* Preserve GAS's shortest DW_CFA_advance_loc form. Both LoongArch
         * ADD6/SUB6 and RISC-V SET6/SUB6 touch just the low six opcode bits;
         * the surrounding 0x40 DWARF opcode is stable across relaxation. */
        {
            MiniAsFixupKind positive=as->target->cfi_advance_reloc_mode==2U ?
                MINIAS_FIXUP_CFI_SET6 : MINIAS_FIXUP_CFI_ADD6;
            if(!as->target->supports_fixup(positive,1U) ||
               !as->target->supports_fixup(MINIAS_FIXUP_CFI_SUB6,1U)) {
                minias_set_error(as,"unsupported-cfi-short-relocation");
                return false;
            }
            field=as->sections[(size_t)sec].size;
            return put8(as,sec,0x40U) &&
                   minias_add_section_fixup(as,
                     MINIAS_FIXUP_ORIGIN_SYNTHETIC_SECTION,
                     sec,field,positive,1U,as->symbols[to_symbol].name,0) &&
                   minias_add_section_fixup(as,
                     MINIAS_FIXUP_ORIGIN_SYNTHETIC_SECTION,
                     sec,field,MINIAS_FIXUP_CFI_SUB6,1U,
                     as->symbols[from_symbol].name,0);
        }
    }
    if(reloc && align!=1U) {
        minias_set_error(as,"unsupported-cfi-relocation-code-alignment");
        return false;
    }
    if(delta<=255U) {
        if(!put8(as,sec,2U))return false;
        field=as->sections[(size_t)sec].size;
        return put8(as,sec,reloc?0U:(uint8_t)delta) &&
               (!reloc || cfi_advance_fixups(as,sec,field,from_symbol,
                                              to_symbol,1U));
    }
    if(delta<=65535U) {
        if(!put8(as,sec,3U))return false;
        field=as->sections[(size_t)sec].size;
        return put8(as,sec,reloc?0U:(uint8_t)delta) &&
               put8(as,sec,reloc?0U:(uint8_t)(delta>>8U)) &&
               (!reloc || cfi_advance_fixups(as,sec,field,from_symbol,
                                              to_symbol,2U));
    }
    if(delta>UINT32_MAX)return false;
    if(!put8(as,sec,4U))return false;
    field=as->sections[(size_t)sec].size;
    return put32(as,sec,reloc?0U:(uint32_t)delta) &&
           (!reloc || cfi_advance_fixups(as,sec,field,from_symbol,
                                          to_symbol,4U));
}
static bool encode_cfi(MiniAs *as,int sec,const MiniAsCfiOp *op) {
    uint64_t magnitude;
    int64_t scaled;
    switch(op->kind) {
        case CFI_DEF_CFA:
            return put8(as,sec,0x0cU) && uleb(as,sec,op->reg) &&
                   uleb(as,sec,(uint64_t)op->value);
        case CFI_DEF_CFA_OFFSET:
            return put8(as,sec,0x0eU) && uleb(as,sec,(uint64_t)op->value);
        case CFI_OFFSET:
            scaled=op->value/minias_cfi_abi(as).data_align;
            /* On every current psABI data_align is negative.  A register
             * saved above CFA therefore has a negative DWARF factor and
             * requires the signed extended form; positive offsets are
             * accepted by GNU GAS and are common in hand-written stubs. */
            if(scaled<0)
                return put8(as,sec,0x11U) && uleb(as,sec,op->reg) &&
                       sleb(as,sec,scaled);
            magnitude=(uint64_t)scaled;
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
            scaled=op->value/minias_cfi_abi(as).data_align;
            if(scaled<0)
                return put8(as,sec,0x15U) && uleb(as,sec,op->reg) &&
                       sleb(as,sec,scaled);
            magnitude=(uint64_t)scaled;
            return put8(as,sec,0x14U) && uleb(as,sec,op->reg) &&
                   uleb(as,sec,magnitude);
        case CFI_ESCAPE:
            return op->bytes!=NULL && op->byte_count!=0U &&
                   minias_section_append(as,sec,op->bytes,op->byte_count);
        default: return false;
    }
}


/* The CIE owns all CFI events at the procedure's first PC, not just the
 * Target's default CFA/RA state.  A later DW_CFA_restore reverts to that
 * initial state; leaving these events in the FDE is not equivalent. */
static bool emit_initial_cfi(MiniAs *as,int sec,size_t first,size_t count) {
    size_t i;
    for(i=0U;i<count;++i)
        if(!encode_cfi(as,sec,&as->cfi_ops[first+i]))return false;
    return true;
}

/* GNU DWARF32 zR CIE: one engine for every Target psABI.
 * Only TargetInfo specifies initial CFA/RA policy and relocation mapping;
 * CFI bytes, CIE variant reuse and anchored FDEs remain Common-owned. */
static bool emit_eh_cie(MiniAs *as,int sec,bool simple,bool signal,
                        size_t first,size_t count,size_t *cie_begin) {
    MiniAsCfiABI abi=minias_cfi_abi(as);
    size_t begin=as->sections[(size_t)sec].size;
    *cie_begin=begin;
    if(abi.code_align==0U || abi.data_align>=0 ||
       abi.return_reg>255U || abi.address_size==0U)return false;
    if(!put32(as,sec,0U) || !put32(as,sec,0U) ||
       !put8(as,sec,(uint8_t)(as->target->cfi_eh_cie_version!=0U ?
                             as->target->cfi_eh_cie_version : 1U)) ||
       !put8(as,sec,'z') || !put8(as,sec,'R') ||
       (signal && !put8(as,sec,'S')) || !put8(as,sec,0U) ||
       !uleb(as,sec,abi.code_align) || !sleb(as,sec,abi.data_align) ||
       !put8(as,sec,(uint8_t)abi.return_reg) ||
       !uleb(as,sec,1U) || !put8(as,sec,0x1bU))return false;
    if(!simple) {
        if(as->target->cfi_initial_cfa_register_only &&
           abi.initial_cfa_offset==0U) {
            if(!put8(as,sec,0x0dU) ||
               !uleb(as,sec,abi.stack_reg))return false;
        } else if(!put8(as,sec,0x0cU) ||
                  !uleb(as,sec,abi.stack_reg) ||
                  !uleb(as,sec,abi.initial_cfa_offset))return false;
        if(abi.initial_ra_saved) {
            uint64_t magnitude=(uint64_t)(-(int64_t)abi.data_align);
            if(abi.return_reg>=64U || magnitude==0U ||
               abi.initial_cfa_offset%magnitude!=0U ||
               !put8(as,sec,(uint8_t)(0x80U|abi.return_reg)) ||
               !uleb(as,sec,abi.initial_cfa_offset/magnitude))return false;
        }
    }
    return emit_initial_cfi(as,sec,first,count) &&
           finish_length(as,sec,begin);
}

/* `.debug_frame` has the same Target-defined initial CFA and saved-RA
 * semantics as `.eh_frame`, but no zR augmentation.  In particular, GNU
 * distinguishes `.cfi_startproc` from `.cfi_startproc simple` with distinct
 * CIEs.  A single CIE with an invented CFA offset zero changes the initial
 * unwind state even when all the following FDE operations are well formed.
 * DWARF CIE v1 has a one-byte return column; all currently registered Target
 * psABIs have a return register in this range. */
static bool emit_debug_cie(MiniAs *as,int sec,bool simple,
                           size_t first,size_t count,size_t *cie_begin) {
    const MiniAsCfiABI abi=minias_cfi_abi(as);
    size_t begin=as->sections[(size_t)sec].size;
    *cie_begin=begin;
    if(abi.code_align==0U || abi.data_align>=0 ||
       abi.return_reg>UINT8_MAX)return false;
    if(!put32(as,sec,0U) || !put32(as,sec,UINT32_MAX) ||
       !put8(as,sec,1U) || !put8(as,sec,0U) ||
       !uleb(as,sec,abi.code_align) || !sleb(as,sec,abi.data_align) ||
       !put8(as,sec,(uint8_t)abi.return_reg))return false;
    if(!simple) {
        if(!put8(as,sec,0x0cU) || !uleb(as,sec,abi.stack_reg) ||
           !uleb(as,sec,abi.initial_cfa_offset))return false;
        if(abi.initial_ra_saved) {
            uint64_t scale=(uint64_t)(-(int64_t)abi.data_align);
            if(abi.return_reg>=64U || scale==0U ||
               abi.initial_cfa_offset%scale!=0U ||
               !put8(as,sec,(uint8_t)(0x80U|abi.return_reg)) ||
               !uleb(as,sec,abi.initial_cfa_offset/scale))return false;
        }
    }
    return emit_initial_cfi(as,sec,first,count) &&
           finish_length(as,sec,begin);
}

typedef struct MiniAsCfiCieVariant {
    size_t first_op;
    size_t prefix_count;
    size_t offset;
    bool simple;
    bool signal;
} MiniAsCfiCieVariant;

/* Keep the exact retained bytecode in the identity of a CIE.  GNU shares a
 * CIE between procedures with identical initial CFI but materializes a new
 * one when a pre-PC .cfi_def_cfa or .cfi_offset changes.  Comparing typed
 * events avoids concatenating source strings or accidentally coalescing
 * different initial unwind states. */
static bool cfi_same_initial(const MiniAs *as,
                             const MiniAsCfiCieVariant *candidate,
                             const MiniAsCfiProc *proc,size_t count,
                             bool eh) {
    size_t i;
    if(candidate->simple!=proc->simple ||
       (eh && candidate->signal!=proc->signal_frame) ||
       candidate->prefix_count!=count)return false;
    for(i=0U;i<count;++i) {
        const MiniAsCfiOp *a=&as->cfi_ops[candidate->first_op+i];
        const MiniAsCfiOp *b=&as->cfi_ops[proc->first_op+i];
        if(a->kind!=b->kind || a->reg!=b->reg || a->value!=b->value ||
           a->byte_count!=b->byte_count ||
           (a->byte_count!=0U &&
            (a->bytes==NULL || b->bytes==NULL ||
             memcmp(a->bytes,b->bytes,a->byte_count)!=0)))return false;
    }
    return true;
}

/* A Linux translation unit may have thousands of distinct function-start
 * CFI states.  Preserve GNU's first-seen CIE order but index the variants by
 * the complete retained initial-event stream, avoiding a quadratic search
 * across all earlier functions.  Hash collisions always compare typed data. */
static uint64_t cfi_hash_word(uint64_t hash,uint64_t value) {
    unsigned i;
    for(i=0U;i<8U;++i) {
        hash^=(uint8_t)value;
        hash*=UINT64_C(1099511628211);
        value>>=8U;
    }
    return hash;
}

static uint64_t cfi_initial_hash(const MiniAs *as,
                                 const MiniAsCfiProc *proc,
                                 size_t count,bool eh) {
    uint64_t hash=UINT64_C(14695981039346656037);
    size_t i,j;
    hash=cfi_hash_word(hash,proc->simple?1U:0U);
    hash=cfi_hash_word(hash,eh && proc->signal_frame?1U:0U);
    hash=cfi_hash_word(hash,count);
    for(i=0U;i<count;++i) {
        const MiniAsCfiOp *op=&as->cfi_ops[proc->first_op+i];
        hash=cfi_hash_word(hash,op->kind);
        hash=cfi_hash_word(hash,op->reg);
        hash=cfi_hash_word(hash,(uint64_t)op->value);
        hash=cfi_hash_word(hash,op->byte_count);
        for(j=0U;j<op->byte_count;++j) {
            hash^=op->bytes[j];
            hash*=UINT64_C(1099511628211);
        }
    }
    return hash;
}

/* One serializer invocation per selected output section. In particular,
 * selecting both .eh_frame and .debug_frame must not mean choosing whichever
 * boolean happens to be true: they have different CIE pointers and relocation
 * policies, but share precisely the same finalized CFI event stream. */
static bool cfi_emit_section_core(MiniAs *as,bool eh,
                                  MiniAsCfiCieVariant *variants,
                                  size_t *variant_slots,size_t slot_count) {
    size_t i,variant_count=0U;
    int section;
    MiniAsSection *data;
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
    for(i=0U;i<as->cfi_proc_count;++i) {
        const MiniAsCfiProc *proc=&as->cfi_procs[i];
        const MiniAsSymbol *begin,*end;
        size_t j,base,ptr,initial_count=0U,variant_index,slot;
        uint64_t signature;
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
        if(proc->first_op>as->cfi_op_count ||
           proc->op_count>as->cfi_op_count-proc->first_op)return false;
        for(j=0U;j<proc->op_count;++j) {
            const MiniAsCfiOp *op=&as->cfi_ops[proc->first_op+j];
            const MiniAsSymbol *anchor;
            if(op->symbol_index>=as->symbol_count)return false;
            anchor=&as->symbols[op->symbol_index];
            if(!anchor->defined || anchor->section!=begin->section ||
               anchor->value<begin->value || anchor->value>end->value)return false;
            if(anchor->value!=begin->value)break;
            ++initial_count;
        }
        signature=cfi_initial_hash(as,proc,initial_count,eh);
        slot=(size_t)(signature&(uint64_t)(slot_count-1U));
        while(variant_slots[slot]!=0U) {
            variant_index=variant_slots[slot]-1U;
            if(cfi_same_initial(as,&variants[variant_index],proc,
                                initial_count,eh))break;
            slot=(slot+1U)&(slot_count-1U);
        }
        if(variant_slots[slot]==0U) {
            variant_index=variant_count;
            MiniAsCfiCieVariant *new_variant=&variants[variant_count++];
            new_variant->first_op=proc->first_op;
            new_variant->prefix_count=initial_count;
            new_variant->simple=proc->simple;
            new_variant->signal=proc->signal_frame;
            if(eh) {
                if(!emit_eh_cie(as,section,proc->simple,proc->signal_frame,
                                proc->first_op,initial_count,
                                &new_variant->offset))return false;
            } else if(!emit_debug_cie(as,section,proc->simple,
                                      proc->first_op,initial_count,
                                      &new_variant->offset))return false;
            variant_slots[slot]=variant_index+1U;
        }
        data=&as->sections[(size_t)section];
        base=data->size;
        if(eh && i==0U && as->target->record_cfi_data_mapping!=NULL &&
           !as->target->record_cfi_data_mapping(as,section,(uint64_t)base))
            return false;
        /* Target mapping-symbol creation can reallocate the Symbol table. */
        begin=&as->symbols[proc->begin_symbol];
        end=&as->symbols[proc->end_symbol];
        if(eh) {
            size_t cie=variants[variant_index].offset;
            if(base+4U<cie || base+4U-cie>UINT32_MAX)return false;
            if(!put32(as,section,0U) ||
               !put32(as,section,(uint32_t)(base+4U-cie)))return false;
            ptr=as->sections[(size_t)section].size;
            if(!put32(as,section,0U) ||
               !put32(as,section,as->target->cfi_eh_range_relocations ?
                     0U : (uint32_t)(end->value-begin->value)) ||
               !uleb(as,section,0U) ||
               !minias_add_section_fixup(as,
               MINIAS_FIXUP_ORIGIN_SYNTHETIC_SECTION,section,ptr,MINIAS_FIXUP_PREL32,4U,
                                 begin->name,0))return false;
            if(as->target->cfi_eh_range_relocations &&
               (!minias_add_section_fixup(as,
                 MINIAS_FIXUP_ORIGIN_SYNTHETIC_SECTION,section,ptr+4U,
                 MINIAS_FIXUP_ADD,4U,end->name,0) ||
                !minias_add_section_fixup(as,
                 MINIAS_FIXUP_ORIGIN_SYNTHETIC_SECTION,section,ptr+4U,
                 MINIAS_FIXUP_SUB,4U,begin->name,0)))return false;
        } else {
            size_t cie=variants[variant_index].offset;
            if(cie>UINT32_MAX)return false;
            if(!put32(as,section,0U) ||
               !put32(as,section,(uint32_t)cie))return false;
            ptr=as->sections[(size_t)section].size;
            if(!put_cfi_address(as,section,0U) ||
               !put_cfi_address(as,section,end->value-begin->value) ||
               !minias_add_section_fixup(as,
               MINIAS_FIXUP_ORIGIN_SYNTHETIC_SECTION,section,ptr,MINIAS_FIXUP_ABS,
                                 minias_cfi_abi(as).address_size,
                                 begin->name,0))return false;
        }
        size_t loc_symbol=proc->begin_symbol;
        for(j=initial_count;j<proc->op_count;++j) {
            const MiniAsCfiOp *op=&as->cfi_ops[proc->first_op+j];
            const MiniAsSymbol *symbol;
            if(op->symbol_index>=as->symbol_count)return false;
            symbol=&as->symbols[op->symbol_index];
            if(!symbol->defined || symbol->section!=begin->section ||
               symbol->value>end->value ||
               !advance_cfi(as,section,loc_symbol,op->symbol_index,eh) ||
               !encode_cfi(as,section,op)) {
                minias_set_error(as,"invalid-cfi-event-layout");
                return false;
            }
            loc_symbol=op->symbol_index;
        }
        /* GNU x86_64 gives intermediate FDEs ordinary DWARF32 4-byte
         * padding but extends the last FDE to the section's 8-byte end.
         * A simple 20-byte CIE makes that final padding observable;
         * aligning every FDE to 8 would instead shift later GCC FDEs. */
        if(eh) {
            unsigned align=(i+1U==as->cfi_proc_count &&
                as->target->cfi_eh_final_alignment!=0U) ?
                as->target->cfi_eh_final_alignment :
                (as->target->cfi_eh_fde_alignment!=0U ?
                 as->target->cfi_eh_fde_alignment :
                 minias_cfi_abi(as).address_size);
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

static bool cfi_emit_section(MiniAs *as,bool eh) {
    MiniAsCfiCieVariant *variants;
    size_t *slots,slot_count=16U;
    bool ok;
    if(as->cfi_proc_count>SIZE_MAX/sizeof(*variants) ||
       as->cfi_proc_count>SIZE_MAX/2U)return false;
    while(slot_count<as->cfi_proc_count*2U) {
        if(slot_count>SIZE_MAX/2U)return false;
        slot_count*=2U;
    }
    variants=calloc(as->cfi_proc_count,sizeof(*variants));
    if(variants==NULL)return false;
    slots=calloc(slot_count,sizeof(*slots));
    if(slots==NULL) {free(variants);return false;}
    ok=cfi_emit_section_core(as,eh,variants,slots,slot_count);
    free(slots);
    free(variants);
    return ok;
}

bool minias_cfi_emit(MiniAs *as) {
    if(as->cfi_proc_count==0U)return true;
    if(as->cfi_eh_frame && !cfi_emit_section(as,true))return false;
    if(as->cfi_debug_frame && !cfi_emit_section(as,false))return false;
    return true;
}
