#include "minias_internal.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * GNU .file/.loc -> genuine DWARF v4 .debug_line, using shared fragment
 * anchors, target-owned absolute ELF fixups and exact finalized instruction
 * offsets. All source debug directives remain semantically observable.
 * Unsupported view/line-state extensions fail closed rather than vanishing.
 */
static bool debug_grow(void **ptr,size_t *capacity,size_t need,size_t width) {
    size_t n=*capacity?*capacity:16U;
    void *p;
    while(n<need){if(n>SIZE_MAX/2U)return false;n*=2U;}
    if(n>SIZE_MAX/width)return false;
    if(n==*capacity)return true;
    p=realloc(*ptr,n*width);if(!p)return false;
    *ptr=p;*capacity=n;return true;
}
static bool debug_unsigned(const char *text,unsigned *out) {
    unsigned long val;
    char *end;
    if(!text || !*text || *text=='-')return false;
    errno=0;val=strtoul(text,&end,0);
    if(errno || end==text || *end || val>UINT_MAX)return false;
    *out=(unsigned)val;
    return true;
}
/* DWARF directive parsing must not depend on strtok's process-global
 * cursor. Assemblers with independent MiniAs contexts may be driven from
 * different threads, and the debug-line emitter itself already preserves
 * that invariant during location sorting. Tokens point into this invocation's
 * owned scratch buffer and are consumed before the buffer is freed. */
static char *debug_next_token(char **cursor) {
    char *start=*cursor;
    char *end;
    if(start==NULL)return NULL;
    while(*start==' ' || *start=='\t')++start;
    if(*start=='\0') {
        *cursor=NULL;
        return NULL;
    }
    end=start;
    while(*end!='\0' && *end!=' ' && *end!='\t')++end;
    if(*end!='\0') {
        *end='\0';
        *cursor=end+1U;
    }else *cursor=NULL;
    return start;
}
static bool debug_quoted(const char **current,char *out,size_t cap) {
    const char *p=*current;
    size_t n=0U;
    while(isspace((unsigned char)*p))++p;
    if(*p!='"')return false;
    ++p;
    while(*p && *p!='"') {
        char ch=*p++;
        if(ch=='\\' && *p)ch=*p++;
        if(n+1U>=cap)return false;
        out[n++]=ch;
    }
    if(*p!='"')return false;
    out[n]='\0';*current=p+1U;return true;
}
static bool parse_file(MiniAs *as,const char *args,size_t line) {
    const char *p=args;
    char *end;
    unsigned long index;
    char first[1024],second[1024];
    char *name;
    size_t len1,len2;
    if(*p=='"') {
        /* GNU .file "name.c" contributes an independent LOCAL ABS STT_FILE
         * entry in .symtab.  It is not a numbered .loc file, and may have
         * the same spelling as a user-defined symbol or another CU. */
        if(!debug_quoted(&p,first,sizeof(first)))goto invalid;
        while(isspace((unsigned char)*p))++p;
        if(*p!='\0')goto invalid;
        if(!debug_grow((void **)&as->cu_files,&as->cu_file_capacity,
                       as->cu_file_count+1U,sizeof(*as->cu_files))) {
            minias_set_error(as,"out-of-memory:cu-file-symbol");
            return false;
        }
        name=minias_strdup(first);
        if(!name) {
            minias_set_error(as,"out-of-memory:cu-file-name");
            return false;
        }
        as->cu_files[as->cu_file_count++]=name;
        return true;
    }
    errno=0;index=strtoul(p,&end,10);
    if(errno || end==p || index>8192UL || !isspace((unsigned char)*end))
        goto invalid;
    p=end;
    if(!debug_quoted(&p,first,sizeof(first)))goto invalid;
    while(isspace((unsigned char)*p))++p;
    if(*p) {
        if(!debug_quoted(&p,second,sizeof(second)))goto invalid;
        while(isspace((unsigned char)*p))++p;
        if(*p)goto invalid;
    } else second[0]='\0';
    if(index==0UL)return true; /* DWARF4's CU name is not file index 0. */
    len1=strlen(first);len2=strlen(second);
    name=malloc(len1+len2+2U);
    if(!name)return false;
    memcpy(name,first,len1);
    if(len2){
        name[len1]='/';memcpy(name+len1+1U,second,len2);
        name[len1+len2+1U]='\0';
    }else name[len1]='\0';
    {
        size_t old_capacity=as->debug_file_capacity;
        if(!debug_grow((void **)&as->debug_files,&as->debug_file_capacity,
                       (size_t)index+1U,sizeof(*as->debug_files))) {
            free(name);return false;
        }
        if(as->debug_file_capacity>old_capacity)
            memset(as->debug_files+old_capacity,0,
                (as->debug_file_capacity-old_capacity)*sizeof(*as->debug_files));
    }
    /* Newly allocated pointer slots must be initialized. */
    if(as->debug_file_max<(size_t)index) {
        size_t i;
        for(i=as->debug_file_max+1U;i<=(size_t)index;++i)
            as->debug_files[i]=NULL;
        as->debug_file_max=(size_t)index;
    }
    if(as->debug_files[index]!=NULL && strcmp(as->debug_files[index],name)) {
        free(name);goto invalid;
    }
    free(as->debug_files[index]);
    as->debug_files[index]=name;return true;
invalid:
    minias_set_error(as,"invalid-dwarf-file:%s:line=%zu",args,line);
    return false;
}
static bool parse_loc(MiniAs *as,const char *args,size_t source_line) {
    char *copy=minias_strdup(args);
    char *cursor=copy;
    char *part;
    MiniAsDebugLoc loc;
    size_t symbol;
    memset(&loc,0,sizeof(loc));
    if(!copy)return false;
    part=debug_next_token(&cursor);
    if(!debug_unsigned(part,&loc.file) || loc.file==0U ||
       loc.file>as->debug_file_max ||
       as->debug_files[loc.file]==NULL)goto bad;
    part=debug_next_token(&cursor);
    if(!debug_unsigned(part,&loc.line))goto bad;
    part=debug_next_token(&cursor);
    if(part && debug_unsigned(part,&loc.column))
        part=debug_next_token(&cursor);
    else loc.column=0U;
    loc.is_stmt=as->debug_is_stmt;
    while(part) {
        if(!strcmp(part,"is_stmt")) {
            unsigned val;
            part=debug_next_token(&cursor);
            if(!debug_unsigned(part,&val) || val>1U)goto bad;
            loc.is_stmt=val!=0U;
        }else if(!strcmp(part,"basic_block"))loc.flags|=1U;
        else if(!strcmp(part,"prologue_end"))loc.flags|=2U;
        else if(!strcmp(part,"epilogue_begin"))loc.flags|=4U;
        else if(!strcmp(part,"discriminator")) {
            part=debug_next_token(&cursor);
            if(!debug_unsigned(part,&loc.discriminator))goto bad;
        }else if(!strcmp(part,"isa")) {
            part=debug_next_token(&cursor);
            if(!debug_unsigned(part,&loc.isa))goto bad;
        }else goto bad;
        part=debug_next_token(&cursor);
    }
    if(as->current_section<0 ||
       (as->sections[(size_t)as->current_section].flags &
        MINIAS_SHF_EXECINSTR)==0U)goto bad;
    if(!debug_grow((void **)&as->debug_locs,&as->debug_loc_capacity,
                   as->debug_loc_count+1U,sizeof(*as->debug_locs)) ||
       !minias_anchor_here(as,&symbol))goto bad;
    loc.symbol_index=symbol;
    as->debug_locs[as->debug_loc_count++]=loc;
    as->debug_is_stmt=loc.is_stmt;
    free(copy);return true;
bad:
    minias_set_error(as,"unsupported-dwarf-loc:%s:line=%zu",args,source_line);
    free(copy);return false;
}
bool minias_debug_directive(MiniAs *as,const char *op,
                            const char *args,size_t line) {
    if(!strcmp(op,".file"))return parse_file(as,args,line);
    if(!strcmp(op,".loc"))return parse_loc(as,args,line);
    return false;
}
static bool append_byte(MiniAs *as,int sec,uint8_t value) {
    return minias_section_append(as,sec,&value,1U);
}
static bool append_u16(MiniAs *as,int sec,uint16_t value) {
    unsigned char b[2]={(unsigned char)value,(unsigned char)(value>>8U)};
    return minias_section_append(as,sec,b,2U);
}
static bool append_u32(MiniAs *as,int sec,uint32_t value) {
    unsigned char b[4];unsigned i;
    for(i=0U;i<4U;++i)b[i]=(unsigned char)(value>>(i*8U));
    return minias_section_append(as,sec,b,4U);
}
static bool append_leb(MiniAs *as,int sec,uint64_t value) {
    do {
        uint8_t b=(uint8_t)(value&0x7fU);
        value>>=7U;
        if(value)b|=0x80U;
        if(!append_byte(as,sec,b))return false;
    }while(value);
    return true;
}
static bool append_sleb(MiniAs *as,int sec,int64_t value) {
    for(;;) {
        uint8_t b=(uint8_t)((uint64_t)value&0x7fU);
        bool last;
        value>>=7U;
        last=(value==0 && (b&0x40U)==0U) ||
             (value==-1 && (b&0x40U)!=0U);
        if(!last)b|=0x80U;
        if(!append_byte(as,sec,b))return false;
        if(last)return true;
    }
}
static bool patch32(MiniAs *as,int sec,size_t at,uint64_t value) {
    size_t i;
    MiniAsSection *s=&as->sections[(size_t)sec];
    if(value>UINT32_MAX || at+4U>s->size)return false;
    for(i=0U;i<4U;++i)
        s->data[at+i]=(unsigned char)(value>>(i*8U));
    return true;
}
static bool dwarf_string(MiniAs *as,int sec,const char *str) {
    return minias_section_append(as,sec,str,strlen(str)+1U);
}
static bool end_sequence(MiniAs *as,int sec) {
    return append_byte(as,sec,0U) && append_leb(as,sec,1U) &&
           append_byte(as,sec,1U);
}
static bool advance_pc(MiniAs *as,int sec,uint64_t delta) {
    return delta==0U || (append_byte(as,sec,2U) && append_leb(as,sec,delta));
}
static bool set_address(MiniAs *as,int sec,size_t symbol_index,
                        unsigned address_width) {
    size_t at;
    size_t i;
    const MiniAsSymbol *symbol;
    if(symbol_index>=as->symbol_count)return false;
    symbol=&as->symbols[symbol_index];
    if(!append_byte(as,sec,0U) ||
       !append_leb(as,sec,(uint64_t)address_width+1U) ||
       !append_byte(as,sec,2U))return false;
    at=as->sections[(size_t)sec].size;
    for(i=0U;i<address_width;++i)
        if(!append_byte(as,sec,0U))return false;
    return minias_add_section_fixup(as,
               MINIAS_FIXUP_ORIGIN_SYNTHETIC_SECTION,sec,at,MINIAS_FIXUP_ABS,address_width,
                            symbol->name,0);
}
/* No file-scope pointer to the assembly being serialized. qsort callbacks
 * that consult a mutable global would race when two independent MiniAs
 * contexts emit DWARF concurrently. Snapshot final section coordinates in
 * an owned sort key, and preserve original event order at equal offsets. */
typedef struct MiniAsDebugLocOrder {
    int section;
    uint64_t offset;
    size_t ordinal;
} MiniAsDebugLocOrder;

static int compare_loc(const void *lhs,const void *rhs) {
    const MiniAsDebugLocOrder *a=lhs,*b=rhs;
    if(a->section!=b->section)return a->section<b->section?-1:1;
    if(a->offset!=b->offset)return a->offset<b->offset?-1:1;
    return a->ordinal<b->ordinal?-1:a->ordinal>b->ordinal?1:0;
}
/* GNU GAS records each directory once and refers to it by a 1-based index
 * from the file table. Keep the original file paths owned by MiniAs and
 * derive a transient DWARF table: debug line emission must not mutate
 * source-level file identities or depend on a particular Target's syntax. */
typedef struct MiniAsDwarfDirectory {
    const char *path;
    size_t length;
} MiniAsDwarfDirectory;
typedef struct MiniAsDwarfFile {
    const char *basename;
    size_t directory_index;
} MiniAsDwarfFile;

static void plan_dwarf_files(const MiniAs *as,MiniAsDwarfDirectory *directories,
                             size_t *directory_count,MiniAsDwarfFile *files,
                             size_t *slots,size_t slot_count) {
    size_t i;
    *directory_count=0U;
    for(i=1U;i<=as->debug_file_max;++i) {
        const char *path=as->debug_files[i];
        const char *slash=strrchr(path,'/');
        size_t length;
        size_t j,at;
        uint64_t hash=UINT64_C(14695981039346656037);
        if(!slash) {
            files[i].basename=path;
            files[i].directory_index=0U;
            continue;
        }
        files[i].basename=slash+1U;
        /* '/' is a directory in its own right, not the empty path. */
        length=slash==path?1U:(size_t)(slash-path);
        for(j=0U;j<length;++j) {
            hash^=(unsigned char)path[j];
            hash*=UINT64_C(1099511628211);
        }
        at=(size_t)hash&(slot_count-1U);
        while(slots[at]!=0U) {
            j=slots[at]-1U;
            if(directories[j].length==length &&
               !memcmp(directories[j].path,path,length))break;
            at=(at+1U)&(slot_count-1U);
        }
        if(slots[at]==0U) {
            j=*directory_count;
            directories[j].path=path;
            directories[j].length=length;
            ++*directory_count;
            slots[at]=j+1U;
        }
        files[i].directory_index=j+1U;
    }
}
bool minias_debug_emit(MiniAs *as) {
    static const unsigned char lengths[12]=
        {0U,1U,1U,1U,1U,0U,0U,0U,1U,0U,0U,1U};
    MiniAsDebugLocOrder *order=NULL;
    MiniAsDwarfDirectory *directories=NULL;
    MiniAsDwarfFile *files=NULL;
    size_t *directory_slots=NULL;
    size_t directory_count=0U;
    size_t directory_slot_count=16U;
    size_t i,j,begin,header_len_at,header_start;
    int sec;
    unsigned address_width=as->output_elf32?4U:8U;
    if(as->debug_loc_count==0U)return true;
    for(i=1U;i<=as->debug_file_max;++i)
        if(as->debug_files[i]==NULL) {
            minias_set_error(as,"missing-dwarf-file-index:%zu",i);
            return false;
        }
    if(as->debug_loc_count>SIZE_MAX/sizeof(*order)) {
        minias_set_error(as,"dwarf-line-sort-capacity-overflow");return false;
    }
    order=malloc(as->debug_loc_count*sizeof(*order));
    if(!order)return false;
    if(as->debug_file_max==SIZE_MAX ||
       as->debug_file_max+1U>SIZE_MAX/sizeof(*files) ||
       as->debug_file_max>SIZE_MAX/sizeof(*directories))goto failed;
    while(directory_slot_count<=as->debug_file_max*2U) {
        if(directory_slot_count>SIZE_MAX/2U)goto failed;
        directory_slot_count*=2U;
    }
    files=calloc(as->debug_file_max+1U,sizeof(*files));
    directories=malloc(as->debug_file_max*sizeof(*directories));
    directory_slots=calloc(directory_slot_count,sizeof(*directory_slots));
    if(!files || !directories || !directory_slots)goto failed;
    plan_dwarf_files(as,directories,&directory_count,files,
                     directory_slots,directory_slot_count);
    for(i=0U;i<as->debug_loc_count;++i) {
        const size_t symbol=as->debug_locs[i].symbol_index;
        if(symbol>=as->symbol_count || !as->symbols[symbol].defined ||
           as->symbols[symbol].section<0 ||
           (size_t)as->symbols[symbol].section>=as->section_count) {
            minias_set_error(as,"dwarf-line-invalid-anchor:%zu",i);
            goto failed;
        }
        order[i].section=as->symbols[symbol].section;
        order[i].offset=as->symbols[symbol].value;
        order[i].ordinal=i;
    }
    qsort(order,as->debug_loc_count,sizeof(*order),compare_loc);
    sec=minias_ensure_section(as,".debug_line",MINIAS_SHT_PROGBITS,0U,1U);
    if(sec<0)goto failed;
    if(!append_u32(as,sec,0U) || !append_u16(as,sec,4U))goto failed;
    header_len_at=as->sections[(size_t)sec].size;
    if(!append_u32(as,sec,0U))goto failed;
    header_start=as->sections[(size_t)sec].size;
    if(!append_byte(as,sec,1U) || !append_byte(as,sec,1U) ||
       !append_byte(as,sec,1U) || !append_byte(as,sec,(uint8_t)-5) ||
       !append_byte(as,sec,14U) || !append_byte(as,sec,13U) ||
       !minias_section_append(as,sec,lengths,sizeof(lengths)))goto failed;
    for(i=0U;i<directory_count;++i)
        if(!minias_section_append(as,sec,directories[i].path,
                                  directories[i].length) ||
           !append_byte(as,sec,0U))goto failed;
    if(!append_byte(as,sec,0U))goto failed;
    for(i=1U;i<=as->debug_file_max;++i)
        if(!dwarf_string(as,sec,files[i].basename) ||
           !append_leb(as,sec,files[i].directory_index) ||
           !append_leb(as,sec,0U) ||
           !append_leb(as,sec,0U))goto failed;
    if(!append_byte(as,sec,0U) ||
       !patch32(as,sec,header_len_at,
                as->sections[(size_t)sec].size-header_start))goto failed;
    for(begin=0U;begin<as->debug_loc_count;) {
        const MiniAsDebugLoc *first=&as->debug_locs[order[begin].ordinal];
        const MiniAsSymbol *first_sym=&as->symbols[first->symbol_index];
        unsigned cur_file=1U,cur_col=0U,cur_isa=0U;
        int64_t cur_line=1;
        bool cur_stmt=true;
        uint64_t position=first_sym->value;
        int source_section=first_sym->section;
        if(source_section<0 || (size_t)source_section>=as->section_count ||
           !set_address(as,sec,first->symbol_index,address_width))goto failed;
        for(j=begin;j<as->debug_loc_count;++j) {
            const MiniAsDebugLoc *loc=&as->debug_locs[order[j].ordinal];
            const MiniAsSymbol *sym=&as->symbols[loc->symbol_index];
            int64_t line_diff;
            if(sym->section!=source_section)break;
            if(sym->value<position ||
               !advance_pc(as,sec,sym->value-position))goto failed;
            position=sym->value;
            if(loc->file!=cur_file &&
               (!append_byte(as,sec,4U) ||
                !append_leb(as,sec,loc->file)))goto failed;
            cur_file=loc->file;
            if(loc->column!=cur_col &&
               (!append_byte(as,sec,5U) ||
                !append_leb(as,sec,loc->column)))goto failed;
            cur_col=loc->column;
            if(loc->is_stmt!=cur_stmt &&
               !append_byte(as,sec,6U))goto failed;
            cur_stmt=loc->is_stmt;
            line_diff=(int64_t)loc->line-cur_line;
            if(line_diff!=0 &&
               (!append_byte(as,sec,3U) ||
                !append_sleb(as,sec,line_diff)))goto failed;
            cur_line=(int64_t)loc->line;
            if((loc->flags&1U) && !append_byte(as,sec,7U))goto failed;
            if((loc->flags&2U) && !append_byte(as,sec,10U))goto failed;
            if((loc->flags&4U) && !append_byte(as,sec,11U))goto failed;
            if(loc->isa!=cur_isa &&
               (!append_byte(as,sec,12U) ||
                !append_leb(as,sec,loc->isa)))goto failed;
            cur_isa=loc->isa;
            if(loc->discriminator) {
                unsigned n=1U;
                uint32_t disc=loc->discriminator;
                while(disc>=128U){disc>>=7U;++n;}
                if(!append_byte(as,sec,0U) ||
                   !append_leb(as,sec,(uint64_t)n+1U) ||
                   !append_byte(as,sec,4U) ||
                   !append_leb(as,sec,loc->discriminator))goto failed;
            }
            if(!append_byte(as,sec,1U))goto failed;
        }
        if(position>as->sections[(size_t)source_section].layout_size ||
           !advance_pc(as,sec,
              as->sections[(size_t)source_section].layout_size-position) ||
           !end_sequence(as,sec))goto failed;
        begin=j;
    }
    if(!patch32(as,sec,0U,as->sections[(size_t)sec].size-4U))goto failed;
    free(directory_slots);free(files);free(directories);free(order);return true;
failed:
    if(as->error[0]=='\0')minias_set_error(as,"dwarf-line-emission-failed");
    free(directory_slots);free(files);free(directories);free(order);return false;
}
