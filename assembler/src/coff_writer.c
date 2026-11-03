#include "minias_coff_object_view.h"

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Microsoft PE/COFF, relocatable (not a PE image). Only the terminal writer
 * knows these wire constants; Common and the Target exchange semantic IDs. */
enum {
    COFF_FILE_HEADER_SIZE=20,
    COFF_SECTION_HEADER_SIZE=40,
    COFF_SYMBOL_SIZE=18,
    COFF_RELOCATION_SIZE=10,
    COFF_AMD64_MACHINE=0x8664,
    COFF_AMD64_ADDR64=0x0001,
    COFF_AMD64_ADDR32=0x0002,
    COFF_AMD64_REL32=0x0004,
    COFF_CLASS_EXTERNAL=2,
    COFF_CLASS_STATIC=3,
    COFF_SCN_CNT_CODE=0x00000020,
    COFF_SCN_CNT_INITIALIZED=0x00000040,
    COFF_SCN_CNT_UNINITIALIZED=0x00000080,
    COFF_SCN_MEM_DISCARDABLE=0x02000000,
    COFF_SCN_MEM_EXECUTE=0x20000000,
    COFF_SCN_MEM_READ=0x40000000
};
#define COFF_SCN_MEM_WRITE UINT32_C(0x80000000)

typedef struct CoffSectionPlan {
    size_t raw_offset;
    size_t relocation_offset;
    size_t relocation_count;
    size_t next_relocation;
    uint32_t characteristics;
    uint32_t long_name_offset;
} CoffSectionPlan;

typedef struct CoffStringTable {
    unsigned char *data;
    size_t size;
    size_t capacity;
} CoffStringTable;

typedef struct CoffFixupField {
    int section;
    uint64_t start;
    uint64_t end;
} CoffFixupField;

static int coff_compare_fixup_field(const void *left,const void *right) {
    const CoffFixupField *a=left,*b=right;
    if(a->section!=b->section)return a->section<b->section?-1:1;
    if(a->start!=b->start)return a->start<b->start?-1:1;
    if(a->end!=b->end)return a->end<b->end?-1:1;
    return 0;
}

static void coff_error(MiniAsCoffObjectView *object,const char *format,...) {
    va_list args;
    if(object==NULL || object->error[0]!='\0')return;
    va_start(args,format);
    (void)vsnprintf(object->error,sizeof(object->error),format,args);
    va_end(args);
}

static void coff_u16(unsigned char *out,uint16_t value) {
    out[0]=(unsigned char)value;
    out[1]=(unsigned char)(value>>8U);
}

static void coff_u32(unsigned char *out,uint32_t value) {
    for(unsigned i=0U;i<4U;++i)
        out[i]=(unsigned char)(value>>(8U*i));
}

static void coff_u64(unsigned char *out,uint64_t value) {
    for(unsigned i=0U;i<8U;++i)
        out[i]=(unsigned char)(value>>(8U*i));
}

static bool coff_advance(size_t *offset,size_t bytes) {
    if(bytes>UINT32_MAX || *offset>UINT32_MAX-bytes)return false;
    *offset+=bytes;
    return true;
}

static bool coff_align4(size_t *offset) {
    const size_t padding=(4U-(*offset&3U))&3U;
    return coff_advance(offset,padding);
}

static bool coff_strings_append(MiniAsCoffObjectView *object,
                                CoffStringTable *strings,const char *name,
                                uint32_t *offset) {
    size_t needed,capacity;
    unsigned char *next;
    const size_t count=strlen(name)+1U;
    if(count==0U || count>UINT32_MAX || strings->size>UINT32_MAX-count) {
        coff_error(object,"coff-string-table-overflow");
        return false;
    }
    needed=strings->size+count;
    if(needed>strings->capacity) {
        capacity=strings->capacity?strings->capacity:64U;
        while(capacity<needed) {
            if(capacity>SIZE_MAX/2U) {
                capacity=needed;
                break;
            }
            capacity*=2U;
        }
        next=realloc(strings->data,capacity);
        if(next==NULL) {
            coff_error(object,"out-of-memory:coff-string-table");
            return false;
        }
        strings->data=next;
        strings->capacity=capacity;
    }
    *offset=(uint32_t)strings->size;
    memcpy(strings->data+strings->size,name,count);
    strings->size=needed;
    return true;
}

/* Long section names use /<decimal string-table offset>; symbol names use
 * the separate zero/offset encoding. The same offset can serve both. */
static bool coff_section_name(MiniAsCoffObjectView *object,
                              unsigned char out[8],const char *name,
                              uint32_t string_offset) {
    const size_t length=strlen(name);
    if(length<=8U) {
        memcpy(out,name,length);
        return true;
    }
    {
        char text[9];
        int count=snprintf(text,sizeof(text),"/%u",string_offset);
        if(count<=0 || count>8) {
            coff_error(object,"coff-section-name-offset-overflow:%s",name);
            return false;
        }
        memcpy(out,text,(size_t)count);
    }
    return true;
}

static void coff_symbol_name(unsigned char out[8],const char *name,
                             uint32_t long_name_offset) {
    const size_t length=strlen(name);
    if(length<=8U)memcpy(out,name,length);
    else coff_u32(out+4,long_name_offset);
}

static bool coff_section_flags(MiniAsCoffObjectView *object,
                               const MiniAsSection *section,uint32_t *out) {
    const MiniAsSectionFlags flags=section->flags;
    const MiniAsSectionFlags supported=MINIAS_SECTION_FLAG_ALLOC|
        MINIAS_SECTION_FLAG_WRITE|MINIAS_SECTION_FLAG_EXECINSTR;
    const uint64_t alignment=section->align?section->align:1U;
    unsigned align_index=1U;
    uint32_t characteristics=COFF_SCN_MEM_READ;
    if((flags&~supported)!=0U || section->group_signature!=NULL ||
       section->group_comdat || section->link_order_reference!=NULL ||
       section->has_unique_id || section->entry_size!=0U ||
       (alignment&(alignment-1U))!=0U || alignment>8192U) {
        coff_error(object,"unsupported-coff-section-semantics:%s",section->name);
        return false;
    }
    switch(section->type) {
        case MINIAS_SECTION_KIND_NOBITS:
            if((flags&MINIAS_SECTION_FLAG_EXECINSTR)!=0U) {
                coff_error(object,"unsupported-coff-executable-nobits:%s",section->name);
                return false;
            }
            characteristics|=COFF_SCN_CNT_UNINITIALIZED;
            break;
        case MINIAS_SECTION_KIND_PROGBITS:
            characteristics|=(flags&MINIAS_SECTION_FLAG_EXECINSTR)?
                COFF_SCN_CNT_CODE:COFF_SCN_CNT_INITIALIZED;
            break;
        default:
            coff_error(object,"unsupported-coff-section-kind:%s",section->name);
            return false;
    }
    if((flags&MINIAS_SECTION_FLAG_EXECINSTR)!=0U)
        characteristics|=COFF_SCN_MEM_EXECUTE;
    if((flags&MINIAS_SECTION_FLAG_WRITE)!=0U)
        characteristics|=COFF_SCN_MEM_WRITE;
    if((flags&MINIAS_SECTION_FLAG_ALLOC)==0U)
        characteristics|=COFF_SCN_MEM_DISCARDABLE;
    for(uint64_t n=alignment;n>1U;n>>=1U)++align_index;
    characteristics|=(uint32_t)align_index<<20U;
    *out=characteristics;
    return true;
}

static bool coff_symbol_semantics(MiniAsCoffObjectView *object,
                                  const MiniAsSymbol *symbol,
                                  int16_t *section_number,uint32_t *value,
                                  uint16_t *type,uint8_t *storage_class) {
    if(symbol->name==NULL || symbol->visibility!=MINIAS_SYMBOL_VISIBILITY_DEFAULT ||
       symbol->bind==MINIAS_SYMBOL_BINDING_WEAK ||
       symbol->bind==MINIAS_SYMBOL_BINDING_GNU_UNIQUE ||
       symbol->type==MINIAS_SYMBOL_KIND_TLS ||
       symbol->type==MINIAS_SYMBOL_KIND_GNU_IFUNC ||
       symbol->alias_pending || symbol->unindexed_mapping ||
       (symbol->type!=MINIAS_SYMBOL_KIND_NOTYPE &&
        symbol->type!=MINIAS_SYMBOL_KIND_OBJECT &&
        symbol->type!=MINIAS_SYMBOL_KIND_FUNC)) {
        coff_error(object,"unsupported-coff-symbol-semantics:%s",
                   symbol->name?symbol->name:"<unnamed>");
        return false;
    }
    *storage_class=symbol->bind==MINIAS_SYMBOL_BINDING_GLOBAL?
        COFF_CLASS_EXTERNAL:COFF_CLASS_STATIC;
    *type=symbol->type==MINIAS_SYMBOL_KIND_FUNC?0x20U:0U;
    if(!symbol->defined) {
        if(symbol->bind==MINIAS_SYMBOL_BINDING_LOCAL) {
            coff_error(object,"undefined-local-coff-symbol:%s",symbol->name);
            return false;
        }
        *section_number=0;
        *value=0;
        return true;
    }
    if(symbol->section==MINIAS_SECTION_ABS) {
        *section_number=-1;
        if(symbol->value>UINT32_MAX)goto overflow;
        *value=(uint32_t)symbol->value;
        return true;
    }
    if(symbol->section==MINIAS_SECTION_COMMON) {
        /* COFF commons use undefined-external + size in Value. No safe
         * encoding exists for arbitrary ELF .comm alignment requests. */
        if(*storage_class!=COFF_CLASS_EXTERNAL || symbol->value>1U) {
            coff_error(object,"unsupported-coff-common-alignment:%s",symbol->name);
            return false;
        }
        if(symbol->size>UINT32_MAX)goto overflow;
        *section_number=0;
        *value=(uint32_t)symbol->size;
        return true;
    }
    if(symbol->section<0 ||
       (size_t)symbol->section>=object->model.section_count) {
        coff_error(object,"invalid-coff-symbol-section:%s",symbol->name);
        return false;
    }
    if(symbol->value>object->model.sections[symbol->section].layout_size &&
       object->model.sections[symbol->section].layout_size!=0U) {
        coff_error(object,"coff-symbol-outside-section:%s",symbol->name);
        return false;
    }
    if(symbol->value>UINT32_MAX)goto overflow;
    *section_number=(int16_t)(symbol->section+1);
    *value=(uint32_t)symbol->value;
    return true;
overflow:
    coff_error(object,"coff-symbol-value-overflow:%s",symbol->name);
    return false;
}

/* COFF has REL (field-embedded) addends. ELF-style PC32 is S+A-P whereas
 * IMAGE_REL_AMD64_REL32 resolves S+field-(P+4). Therefore field=A+4,
 * e.g. x86 CALL's ordinary ELF -4 becomes COFF zero. The borrowed emitted
 * section remains immutable; only the private COFF image is patched. */
static bool coff_embed_addend(MiniAsCoffObjectView *object,
                              unsigned char *field,uint16_t relocation,
                              int64_t addend) {
    if(relocation==COFF_AMD64_ADDR64) {
        coff_u64(field,(uint64_t)addend);
        return true;
    }
    if(relocation==COFF_AMD64_REL32) {
        if(addend>INT32_MAX-4 || addend<INT32_MIN-4LL) {
            coff_error(object,"coff-rel32-addend-overflow");
            return false;
        }
        addend+=4;
    }
    if(addend<INT32_MIN || addend>(relocation==COFF_AMD64_ADDR32?
                                    (int64_t)UINT32_MAX:(int64_t)INT32_MAX)) {
        coff_error(object,"coff-relocation-addend-overflow");
        return false;
    }
    coff_u32(field,(uint32_t)addend);
    return true;
}

bool minias_write_coff_object(MiniAsCoffObjectView *object,const char *path) {
    MiniAsObjectPlan plan={0};
    CoffSectionPlan *sections=NULL;
    CoffFixupField *fixup_fields=NULL;
    uint32_t *long_symbol_names=NULL;
    uint32_t *symbol_ordinals=NULL;
    unsigned char *function_aux=NULL;
    char **private_names=NULL;
    CoffStringTable strings={0};
    unsigned char *image=NULL;
    size_t image_size=0U,symbol_table_offset=0U;
    size_t symbol_records=0U,section_count,symbol_count,fixup_count;
    size_t i,private_suffix=0U;
    bool ok=false;

    if(object==NULL)return false;
    section_count=object->model.section_count;
    symbol_count=object->model.symbol_count;
    fixup_count=object->model.fixup_count;
    if(object->machine!=COFF_AMD64_MACHINE || object->target_abi.address_size!=8U ||
       object->map_fixup==NULL || path==NULL) {
        coff_error(object,"unsupported-coff-target-or-policy");
        return false;
    }
    if(section_count>INT16_MAX || section_count>SIZE_MAX/sizeof(*sections) ||
       fixup_count>SIZE_MAX/sizeof(*fixup_fields) ||
       symbol_count>SIZE_MAX/sizeof(*long_symbol_names) ||
       symbol_count>SIZE_MAX/sizeof(*private_names) ||
       symbol_count>SIZE_MAX/sizeof(*symbol_ordinals) ||
       section_count>(UINT32_MAX/2U) ||
       symbol_count>UINT32_MAX-2U*section_count) {
        coff_error(object,"coff-object-capacity-overflow");
        return false;
    }
    if(!minias_object_plan(&object->model,&plan,
                           object->error,sizeof(object->error)))return false;
    sections=calloc(section_count?section_count:1U,sizeof(*sections));
    fixup_fields=calloc(fixup_count?fixup_count:1U,
                        sizeof(*fixup_fields));
    long_symbol_names=calloc(symbol_count?symbol_count:1U,
                             sizeof(*long_symbol_names));
    symbol_ordinals=calloc(symbol_count?symbol_count:1U,
                           sizeof(*symbol_ordinals));
    function_aux=calloc(symbol_count?symbol_count:1U,
                         sizeof(*function_aux));
    private_names=calloc(symbol_count?symbol_count:1U,sizeof(*private_names));
    if(sections==NULL || fixup_fields==NULL ||
       long_symbol_names==NULL || symbol_ordinals==NULL ||
       function_aux==NULL || private_names==NULL) {
        coff_error(object,"out-of-memory:coff-object-plan");
        goto done;
    }
    strings.data=calloc(4U,1U);
    if(strings.data==NULL) {
        coff_error(object,"out-of-memory:coff-string-table");
        goto done;
    }
    strings.size=4U;
    strings.capacity=4U;

    for(i=0U;i<section_count;++i) {
        const MiniAsSection *source=&object->model.sections[i];
        const MiniAsObjectSectionPlan *borrow=&plan.sections[i];
        CoffSectionPlan *section=&sections[i];
        if(source->name==NULL || !*source->name ||
           !coff_section_flags(object,source,&section->characteristics))
            goto done;
        if(source->layout_size>UINT32_MAX || borrow->size>UINT32_MAX ||
           (source->type!=MINIAS_SECTION_KIND_NOBITS &&
            borrow->size!=0U && borrow->bytes==NULL)) {
            coff_error(object,"coff-section-size-overflow:%s",source->name);
            goto done;
        }
        if(source->type==MINIAS_SECTION_KIND_NOBITS) {
            for(size_t j=0U;borrow->bytes!=NULL && j<borrow->size;++j)
                if(borrow->bytes[j]!=0U) {
                    coff_error(object,"coff-nobits-has-data:%s",source->name);
                    goto done;
                }
            if(borrow->size!=source->layout_size) {
                coff_error(object,"coff-nobits-length-mismatch:%s",source->name);
                goto done;
            }
        }
        if(strlen(source->name)>8U &&
           !coff_strings_append(object,&strings,source->name,
                                &section->long_name_offset))goto done;
    }
    for(i=0U;i<symbol_count;++i) {
        const MiniAsSymbol *symbol=&object->model.symbols[i];
        const char *name=symbol->name;
        int16_t section_number;
        uint32_t value;
        uint16_t type;
        uint8_t storage;
        if(!coff_symbol_semantics(object,symbol,&section_number,&value,&type,&storage))
            goto done;
        /* Microsoft COFF defines an auxiliary function-definition record
         * after each defined external symbol whose Type is 0x20.  Ordinals
         * must account for these records before relocations are serialized. */
        if(section_number>0 && type==0x20U && storage==COFF_CLASS_EXTERNAL) {
            if(symbol->size>UINT32_MAX) {
                coff_error(object,"coff-function-size-overflow:%s",name);
                goto done;
            }
            function_aux[i]=1U;
        }
        if(minias_is_private_symbol_name(name)) {
            char candidate[96];
            size_t collision;
            int n;
            do {
                if(private_suffix==SIZE_MAX) {
                    coff_error(object,"coff-private-symbol-overflow");
                    goto done;
                }
                n=snprintf(candidate,sizeof(candidate),
                           ".Lminias_private_%zu",private_suffix++);
                if(n<=0 || (size_t)n>=sizeof(candidate)) {
                    coff_error(object,"coff-private-symbol-name-too-long");
                    goto done;
                }
            }while(object->model.find_symbol_id(object->model.reader,
                                                candidate,&collision));
            private_names[i]=minias_strdup(candidate);
            if(private_names[i]==NULL) {
                coff_error(object,"out-of-memory:coff-private-symbol");
                goto done;
            }
            name=private_names[i];
        }
        if(strlen(name)>8U &&
           !coff_strings_append(object,&strings,name,&long_symbol_names[i]))
            goto done;
    }
    for(i=0U;i<fixup_count;++i) {
        const MiniAsFixup *fixup=&object->model.fixups[i];
        CoffSectionPlan *section=&sections[(size_t)fixup->section];
        uint16_t reloc=0U;
        if(fixup->symbol_index==SIZE_MAX || fixup->kind==MINIAS_FIXUP_NONE ||
           object->model.sections[fixup->section].type==MINIAS_SECTION_KIND_NOBITS ||
           !object->map_fixup(object->target_abi,fixup->kind,
                              fixup->width,&reloc) ||
           !((reloc==COFF_AMD64_ADDR64 && fixup->width==8U) ||
             ((reloc==COFF_AMD64_ADDR32 || reloc==COFF_AMD64_REL32) &&
              fixup->width==4U))) {
            coff_error(object,"unsupported-coff-fixup:kind=%u:width=%u",
                       (unsigned)fixup->kind,fixup->width);
            goto done;
        }
        if(plan.fixups[i].final_offset>UINT32_MAX ||
           section->relocation_count==UINT16_MAX) {
            coff_error(object,"coff-section-relocation-overflow:%s",
                       object->model.sections[fixup->section].name);
            goto done;
        }
        fixup_fields[i]=(CoffFixupField){
            fixup->section,plan.fixups[i].final_offset,
            plan.fixups[i].final_offset+fixup->width
        };
        ++section->relocation_count;
    }
    qsort(fixup_fields,fixup_count,sizeof(*fixup_fields),
          coff_compare_fixup_field);
    for(i=1U;i<fixup_count;++i)
        if(fixup_fields[i-1U].section==fixup_fields[i].section &&
           fixup_fields[i-1U].end>fixup_fields[i].start) {
            coff_error(object,"overlapping-coff-relocation-fields:section=%d",
                       fixup_fields[i].section);
            goto done;
        }

    symbol_records=2U*section_count;
    for(i=0U;i<symbol_count;++i) {
        if(symbol_records>UINT32_MAX-1U-function_aux[i]) {
            coff_error(object,"coff-symbol-table-overflow");
            goto done;
        }
        symbol_ordinals[i]=(uint32_t)symbol_records;
        symbol_records+=1U+function_aux[i];
    }
    if(section_count>(UINT32_MAX-COFF_FILE_HEADER_SIZE)/COFF_SECTION_HEADER_SIZE) {
        coff_error(object,"coff-header-size-overflow");
        goto done;
    }
    image_size=COFF_FILE_HEADER_SIZE+section_count*COFF_SECTION_HEADER_SIZE;
    for(i=0U;i<section_count;++i) {
        if(object->model.sections[i].type==MINIAS_SECTION_KIND_NOBITS ||
           plan.sections[i].size==0U)continue;
        if(!coff_align4(&image_size))goto size_overflow;
        sections[i].raw_offset=image_size;
        if(!coff_advance(&image_size,plan.sections[i].size))goto size_overflow;
    }
    for(i=0U;i<section_count;++i) {
        CoffSectionPlan *section=&sections[i];
        if(section->relocation_count==0U)continue;
        if(!coff_align4(&image_size))goto size_overflow;
        section->relocation_offset=image_size;
        if(!coff_advance(&image_size,
                         section->relocation_count*COFF_RELOCATION_SIZE))
            goto size_overflow;
    }
    if(!coff_align4(&image_size))goto size_overflow;
    symbol_table_offset=image_size;
    if(symbol_records>UINT32_MAX/COFF_SYMBOL_SIZE ||
       !coff_advance(&image_size,symbol_records*COFF_SYMBOL_SIZE) ||
       !coff_advance(&image_size,strings.size))goto size_overflow;
    if(strings.size>UINT32_MAX)goto size_overflow;
    coff_u32(strings.data,(uint32_t)strings.size);
    image=calloc(image_size?image_size:1U,1U);
    if(image==NULL) {
        coff_error(object,"out-of-memory:coff-image");
        goto done;
    }
    coff_u16(image,(uint16_t)object->machine);
    coff_u16(image+2U,(uint16_t)section_count);
    coff_u32(image+8U,(uint32_t)symbol_table_offset);
    coff_u32(image+12U,(uint32_t)symbol_records);
    /* Timestamp=0, OptionalHeaderSize=0, Characteristics=0: relocatable. */

    for(i=0U;i<section_count;++i) {
        const MiniAsSection *source=&object->model.sections[i];
        const MiniAsObjectSectionPlan *borrow=&plan.sections[i];
        const CoffSectionPlan *section=&sections[i];
        unsigned char *header=image+COFF_FILE_HEADER_SIZE+
                              i*COFF_SECTION_HEADER_SIZE;
        if(!coff_section_name(object,header,source->name,
                              section->long_name_offset))goto done;
        /* VirtualSize and VirtualAddress are zero in a COFF .obj. For
         * NOBITS the logical length stays in SizeOfRawData and the section
         * auxiliary symbol, with PointerToRawData=0 and no file payload. */
        coff_u32(header+16U,(uint32_t)(source->type==MINIAS_SECTION_KIND_NOBITS?
                                      source->layout_size:borrow->size));
        coff_u32(header+20U,(uint32_t)section->raw_offset);
        coff_u32(header+24U,(uint32_t)section->relocation_offset);
        coff_u16(header+32U,(uint16_t)section->relocation_count);
        coff_u32(header+36U,section->characteristics);
        if(section->raw_offset!=0U)
            memcpy(image+section->raw_offset,borrow->bytes,borrow->size);
    }

    for(i=0U;i<fixup_count;++i) {
        const MiniAsFixup *fixup=&object->model.fixups[i];
        const size_t section_index=(size_t)fixup->section;
        CoffSectionPlan *section=&sections[section_index];
        const uint32_t offset=(uint32_t)plan.fixups[i].final_offset;
        uint16_t type=0U;
        unsigned char *rel=image+section->relocation_offset+
                           section->next_relocation*COFF_RELOCATION_SIZE;
        unsigned char *field=image+section->raw_offset+offset;
        ++section->next_relocation;
        if(!object->map_fixup(object->target_abi,fixup->kind,
                              fixup->width,&type) ||
           !coff_embed_addend(object,field,type,fixup->addend))goto done;
        coff_u32(rel,offset);
        coff_u32(rel+4U,symbol_ordinals[fixup->symbol_index]);
        coff_u16(rel+8U,type);
    }

    /* Each section gets a STATIC symbol and one section-definition aux
     * record (18 bytes). User symbol ordinals follow both records exactly. */
    for(i=0U;i<section_count;++i) {
        const MiniAsSection *source=&object->model.sections[i];
        const CoffSectionPlan *section=&sections[i];
        const MiniAsObjectSectionPlan *borrow=&plan.sections[i];
        unsigned char *symbol=image+symbol_table_offset+
                              (2U*i)*COFF_SYMBOL_SIZE;
        unsigned char *aux=symbol+COFF_SYMBOL_SIZE;
        coff_symbol_name(symbol,source->name,section->long_name_offset);
        coff_u16(symbol+12U,(uint16_t)(i+1U));
        symbol[16U]=COFF_CLASS_STATIC;
        symbol[17U]=1U;
        coff_u32(aux,(uint32_t)(source->type==MINIAS_SECTION_KIND_NOBITS?
                                source->layout_size:borrow->size));
        coff_u16(aux+4U,(uint16_t)section->relocation_count);
        /* aux Number is only meaningful for associative COMDAT. */
    }
    for(i=0U;i<symbol_count;++i) {
        const MiniAsSymbol *source=&object->model.symbols[i];
        const char *name=private_names[i]?private_names[i]:source->name;
        unsigned char *symbol=image+symbol_table_offset+
                              symbol_ordinals[i]*COFF_SYMBOL_SIZE;
        int16_t section_number=0;
        uint32_t value=0;
        uint16_t type=0;
        uint8_t storage_class=0;
        if(!coff_symbol_semantics(object,source,&section_number,&value,
                                  &type,&storage_class))goto done;
        coff_symbol_name(symbol,name,long_symbol_names[i]);
        coff_u32(symbol+8U,value);
        coff_u16(symbol+12U,(uint16_t)section_number);
        coff_u16(symbol+14U,type);
        symbol[16U]=storage_class;
        if(function_aux[i]) {
            /* No .bf or COFF line-number records: TagIndex and line pointer
             * stay zero.  Preserve the known .size, where supplied. */
            unsigned char *aux=symbol+COFF_SYMBOL_SIZE;
            symbol[17U]=1U;
            coff_u32(aux+4U,(uint32_t)source->size);
        }
    }
    {
        uint32_t next_function=0U;
        for(i=symbol_count;i>0U;--i) {
            const size_t symbol_index=i-1U;
            if(function_aux[symbol_index]) {
                unsigned char *aux=image+symbol_table_offset+
                    (symbol_ordinals[symbol_index]+1U)*COFF_SYMBOL_SIZE;
                coff_u32(aux+12U,next_function);
                next_function=symbol_ordinals[symbol_index];
            }
        }
    }
    memcpy(image+symbol_table_offset+symbol_records*COFF_SYMBOL_SIZE,
           strings.data,strings.size);
    ok=minias_object_publish_image(path,image,image_size,
                                   object->error,sizeof(object->error));
    goto done;

size_overflow:
    coff_error(object,"coff-object-image-overflow");
done:
    if(private_names!=NULL)
        for(i=0U;i<symbol_count;++i)free(private_names[i]);
    free(private_names);
    free(long_symbol_names);
    free(symbol_ordinals);
    free(function_aux);
    free(fixup_fields);
    free(sections);
    free(strings.data);
    free(image);
    minias_object_dispose_plan(&plan);
    return ok;
}
