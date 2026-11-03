#include "minias_internal.h"
#include "minias_common_context.h"
#include "minias_common_emission.h"

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
 * GNU view assertions/resets are retained until final fragment layout;
 * unsupported line-state extensions fail closed rather than vanishing.
 */
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
/* .file path spelling uses the same GNU quoted-string escapes as Common
 * data directives. In particular, \\040 is a space, not literal "040".
 * Consume precisely one lexical string, then delegate byte decoding to the
 * one shared decoder; a NUL byte is invalid in a file-system path. */
static bool debug_quoted(const char **current,char **out) {
    const char *begin=*current,*p;
    char *literal=NULL,*name=NULL;
    unsigned char *decoded=NULL;
    size_t literal_size,decoded_size=0U;
    *out=NULL;
    while(isspace((unsigned char)*begin))++begin;
    if(*begin!='"')return false;
    p=begin+1U;
    while(*p!='\0') {
        if(*p=='\\') {
            if(p[1U]=='\0')return false;
            p+=2U;
            continue;
        }
        if(*p=='"')break;
        ++p;
    }
    if(*p!='"')return false;
    literal_size=(size_t)(p-begin)+1U;
    if(literal_size==SIZE_MAX)return false;
    literal=malloc(literal_size+1U);
    if(literal==NULL)return false;
    memcpy(literal,begin,literal_size);
    literal[literal_size]='\0';
    if(!minias_decode_string_literals(literal,false,&decoded,&decoded_size) ||
       decoded_size==SIZE_MAX ||
       (decoded_size!=0U && memchr(decoded,'\0',decoded_size)!=NULL))goto failed;
    name=malloc(decoded_size+1U);
    if(name==NULL)goto failed;
    if(decoded_size!=0U)memcpy(name,decoded,decoded_size);
    name[decoded_size]='\0';
    *current=p+1U;
    *out=name;
    free(decoded);
    free(literal);
    return true;
failed:
    free(name);
    free(decoded);
    free(literal);
    return false;
}
static bool parse_file(MiniAs *as,const char *args,size_t line) {
    const char *p=args;
    char *end;
    unsigned long index;
    char *first=NULL,*second=NULL,*name=NULL;
    size_t len1,len2;
    if(*p=='"') {
        /* GNU .file "name.c" contributes an independent LOCAL ABS STT_FILE
         * entry in .symtab.  It is not a numbered .loc file, and may have
         * the same spelling as a user-defined symbol or another CU. */
        if(!debug_quoted(&p,&first))goto invalid;
        while(isspace((unsigned char)*p))++p;
        if(*p!='\0')goto invalid;
        if(!minias_grow_array((void **)&as->cu_files,&as->cu_file_capacity,
                              sizeof(*as->cu_files),as->cu_file_count+1U)) {
            minias_set_error(as,"out-of-memory:cu-file-symbol");
            goto failed;
        }
        as->cu_files[as->cu_file_count++]=first;
        return true;
    }
    errno=0;index=strtoul(p,&end,10);
    if(errno || end==p || index>8192UL || !isspace((unsigned char)*end))
        goto invalid;
    p=end;
    if(!debug_quoted(&p,&first))goto invalid;
    while(isspace((unsigned char)*p))++p;
    if(*p) {
        if(!debug_quoted(&p,&second))goto invalid;
        while(isspace((unsigned char)*p))++p;
        if(*p)goto invalid;
    }
    if(index==0UL)goto success; /* DWARF4's CU name is not file index 0. */
    len1=strlen(first);len2=second?strlen(second):0U;
    if(len1>SIZE_MAX-len2-2U)goto oom;
    name=malloc(len1+len2+2U);
    if(!name)goto oom;
    memcpy(name,first,len1);
    if(len2){
        name[len1]='/';memcpy(name+len1+1U,second,len2);
        name[len1+len2+1U]='\0';
    }else name[len1]='\0';
    {
        size_t old_capacity=as->debug_file_capacity;
        if(!minias_grow_array((void **)&as->debug_files,&as->debug_file_capacity,
                              sizeof(*as->debug_files),(size_t)index+1U)) {
            goto oom;
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
        goto invalid;
    }
    free(as->debug_files[index]);
    as->debug_files[index]=name;
    name=NULL;
success:
    free(first);
    free(second);
    free(name);
    return true;
invalid:
    minias_set_error(as,"invalid-dwarf-file:%s:line=%zu",args,line);
failed:
    free(first);
    free(second);
    free(name);
    return false;
oom:
    minias_set_error(as,"out-of-memory:dwarf-file-name");
    goto failed;
}
static bool parse_loc(MiniAs *as,const char *args,size_t source_line) {
    char *copy=minias_strdup(args);
    char *cursor=copy;
    char *part;
    MiniAsDebugLoc loc;
    size_t symbol;
    memset(&loc,0,sizeof(loc));
    loc.view_symbol_index=SIZE_MAX;
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
        }else if(!strcmp(part,"view")) {
            part=debug_next_token(&cursor);
            if(part==NULL || loc.view_mode!=0U)goto bad;
            if(!strcmp(part,"0"))loc.view_mode=1U;
            else if(!strcmp(part,"-0"))loc.view_mode=2U;
            else {
                size_t k;
                if(!minias_expr_symbol_start(part[0]))goto bad;
                for(k=1U;part[k];++k)
                    if(!minias_expr_symbol_continue(part[k]))goto bad;
                if(!minias_common_context_reserve_named_dwarf_view(
                       as,part,&loc.view_symbol_index))goto bad;
                loc.view_mode=3U;
            }
        }else goto bad;
        part=debug_next_token(&cursor);
    }
    if(as->current_section<0 ||
       (as->sections[(size_t)as->current_section].flags &
        MINIAS_SECTION_FLAG_EXECINSTR)==0U)goto bad;
    if(!minias_grow_array((void **)&as->debug_locs,&as->debug_loc_capacity,
                          sizeof(*as->debug_locs),as->debug_loc_count+1U) ||
       !minias_anchor_here(as,&symbol))goto bad;
    loc.symbol_index=symbol;
    {
        MiniAsSymbolView here;
        size_t i=as->debug_loc_count;
        if(!minias_common_context_symbol_view(as,symbol,&here))goto bad;
        /* The parse-time value is a provisional absolute symbol usable by
         * expressions in subsequent debug sections. Final MCLayout owns the
         * actual ordinal, as instruction lengths can relax. */
        while(i!=0U) {
            const MiniAsDebugLoc *previous=&as->debug_locs[--i];
            MiniAsSymbolView anchor;
            if(!minias_common_context_symbol_view(as,previous->symbol_index,
                                                  &anchor))goto bad;
            if(anchor.section==here.section &&
               anchor.subsection==here.subsection &&
               anchor.value==here.value) {
                loc.view_number=previous->view_number+1U;
                break;
            }
        }
        if(loc.view_mode==2U)loc.view_number=0U;
        if(loc.view_symbol_index!=SIZE_MAX &&
           !minias_common_context_publish_dwarf_view(
                 as,loc.view_symbol_index,loc.view_number))goto bad;
    }
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
    return minias_section_append_le32(as,sec,value);
}
static bool append_leb(MiniAs *as,int sec,uint64_t value) {
    return minias_section_append_uleb(as,sec,value);
}
static bool append_sleb(MiniAs *as,int sec,int64_t value) {
    return minias_section_append_sleb(as,sec,value);
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
    MiniAsSymbolView symbol;
    if(!minias_common_context_symbol_view(as,symbol_index,&symbol))return false;
    if(!append_byte(as,sec,0U) ||
       !append_leb(as,sec,(uint64_t)address_width+1U) ||
       !append_byte(as,sec,2U))return false;
    at=minias_emission_section_size(as,sec);
    for(i=0U;i<address_width;++i)
        if(!append_byte(as,sec,0U))return false;
    return minias_add_section_fixup(as,
               MINIAS_FIXUP_ORIGIN_SYNTHETIC_SECTION,sec,at,MINIAS_FIXUP_ABS,address_width,
                            symbol.name,0,false);
}

/* A variable-length DW_LNS_advance_pc cannot be patched after GNU ld removes
 * a relaxable instruction or NOP reservation. GNU's link-relaxable targets
 * instead use DW_LNS_fixed_advance_pc plus a pair of 16-bit symbol-difference
 * fixups. A large delta uses an absolute DW_LNE_set_address to avoid a
 * linker-side 16-bit overflow. The machine's same-section subtraction
 * policy stays in TargetOps, rather than assuming every code-span marker
 * implies the same rule on RISC-V and LoongArch. */
static bool advance_pc_between_symbols(MiniAs *as,int sec,
                                      size_t from_symbol,size_t to_symbol,
                                      uint64_t delta,unsigned address_width) {
    MiniAsSymbolView from,to;
    const MiniAsTargetOps *target=as->target;
    const bool relocate=target->dwarf_line_difference_relocations!=NULL &&
        target->dwarf_line_difference_relocations(minias_target_abi_view(as));
    size_t field;
    if(!target->dwarf_line_fixed_advance)return advance_pc(as,sec,delta);
    if(delta>50000U)return set_address(as,sec,to_symbol,address_width);
    if(delta>UINT16_MAX ||
       !minias_common_context_symbol_view(as,from_symbol,&from) ||
       !minias_common_context_symbol_view(as,to_symbol,&to) ||
       from.section<0 || from.section!=to.section)return false;
    if(!append_byte(as,sec,9U))return false; /* DW_LNS_fixed_advance_pc */
    field=minias_emission_section_size(as,sec);
    if(!append_byte(as,sec,relocate?0U:(uint8_t)delta) ||
       !append_byte(as,sec,relocate?0U:(uint8_t)(delta>>8U)))return false;
    return !relocate ||
           (minias_add_section_fixup(as,MINIAS_FIXUP_ORIGIN_SYNTHETIC_SECTION,
               sec,field,MINIAS_FIXUP_ADD,2U,to.name,0,false) &&
            minias_add_section_fixup(as,MINIAS_FIXUP_ORIGIN_SYNTHETIC_SECTION,
               sec,field,MINIAS_FIXUP_SUB,2U,from.name,0,false));
}

/* The end_sequence PC must move with the final code bytes too. Its symbol
 * is a synthetic *Section-relative* anchor, created only during final DWARF
 * emission after MCLayout has committed the one Section size. */
static bool debug_section_end_anchor(MiniAs *as,int section,size_t *anchor_id) {
    char name[96];
    if(section<0 || (size_t)section>=as->section_count || anchor_id==NULL ||
       !minias_make_private_symbol_name(name,sizeof(name),"debug_line_end",
                                        (size_t)section+1U))return false;
    return minias_common_context_define_output_anchor(as,name,section,0U,
        as->sections[(size_t)section].layout_size,true,anchor_id);
}
/* No file-scope pointer to the assembly being serialized. qsort callbacks
 * that consult a mutable global would race when two independent MiniAs
 * contexts emit DWARF concurrently. Snapshot final section coordinates in
 * an owned sort key, and preserve original event order at equal offsets. */
typedef struct MiniAsDebugLocOrder {
    int section;
    uint64_t offset;
    uint32_t subsection;
    size_t ordinal;
} MiniAsDebugLocOrder;

static int compare_loc(const void *lhs,const void *rhs) {
    const MiniAsDebugLocOrder *a=lhs,*b=rhs;
    if(a->section!=b->section)return a->section<b->section?-1:1;
    if(a->offset!=b->offset)return a->offset<b->offset?-1:1;
    /* GNU concatenates numbered subsections before emitting line rows;
     * two zero-width anchors at the boundary follow subsection ownership,
     * not the interleaved source order of `.subsection` directives. */
    if(a->subsection!=b->subsection)
        return a->subsection<b->subsection?-1:1;
    return a->ordinal<b->ordinal?-1:a->ordinal>b->ordinal?1:0;
}
/* GNU GAS view labels are real absolute expressions, often referenced by
 * `.uleb128 .LVU2-.LVU1` in GCC's DWARF location lists. Their values are
 * computed from finalized section positions, not from source line numbers.
 * Keeping the relationship to an anchored .loc event makes branch growth
 * and subsection placement observable in the resulting symbol difference. */
/* Both layout's view resolution and final .debug_line emission need exactly
 * this section/offset/subsection/ordinal order. Build it once per caller;
 * resolving view labels never mutates the location anchors in these keys. */
static bool debug_sorted_locs(MiniAs *as,MiniAsDebugLocOrder **result) {
    MiniAsDebugLocOrder *order;
    size_t i;
    if(as->debug_loc_count>SIZE_MAX/sizeof(*order)) {
        minias_set_error(as,"dwarf-view-order-overflow");
        return false;
    }
    order=malloc(as->debug_loc_count*sizeof(*order));
    if(order==NULL) {
        minias_set_error(as,"out-of-memory:dwarf-view-order");
        return false;
    }
    for(i=0U;i<as->debug_loc_count;++i) {
        const size_t symbol=as->debug_locs[i].symbol_index;
        MiniAsSymbolView anchor;
        if(!minias_common_context_symbol_view(as,symbol,&anchor) ||
           !anchor.defined || anchor.section<0 ||
           (size_t)anchor.section>=as->section_count) {
            minias_set_error(as,"dwarf-line-invalid-anchor:%zu",i);
            free(order);return false;
        }
        order[i].section=anchor.section;
        order[i].offset=anchor.value;
        order[i].subsection=anchor.subsection;
        order[i].ordinal=i;
    }
    qsort(order,as->debug_loc_count,sizeof(*order),compare_loc);
    *result=order;
    return true;
}

static bool resolve_views_sorted(MiniAs *as,bool strict,
                                 const MiniAsDebugLocOrder *order) {
    size_t i;
    uint64_t previous_view=0U;
    for(i=0U;i<as->debug_loc_count;++i) {
        MiniAsDebugLoc *loc=&as->debug_locs[order[i].ordinal];
        const bool shared_pc=i!=0U &&
            order[i].section==order[i-1U].section &&
            order[i].offset==order[i-1U].offset;
        const uint64_t view=(shared_pc && loc->view_mode!=2U)
            ? previous_view+1U : 0U;
        if(view<previous_view && shared_pc && loc->view_mode!=2U) {
            minias_set_error(as,"dwarf-view-overflow");
            return false;
        }
        if(strict && loc->view_mode==1U && view!=0U) {
            minias_set_error(as,"dwarf-view-number-mismatch");
            return false;
        }
        loc->view_number=view;
        if(loc->view_symbol_index!=SIZE_MAX) {
            MiniAsSymbolView ignored;
            if(!minias_common_context_symbol_view(as,loc->view_symbol_index,
                                                  &ignored)) {
                minias_set_error(as,"dwarf-view-invalid-symbol");
                return false;
            }
            /* Repeatable: provisional parse views are recomputed after
             * each layout/relaxation pass, then once more for DWARF emit. */
            if(!minias_common_context_publish_dwarf_view(
                   as,loc->view_symbol_index,view))return false;
        }
        previous_view=view;
    }
    return true;
}

bool minias_debug_resolve_views(MiniAs *as,bool strict) {
    MiniAsDebugLocOrder *order;
    bool ok;
    if(as->debug_loc_count==0U)return true;
    if(!debug_sorted_locs(as,&order))return false;
    ok=resolve_views_sorted(as,strict,order);
    free(order);
    return ok;
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
    unsigned address_width=as->target_spec->layout.address_size;
    if(as->debug_loc_count==0U)return true;
    if(!debug_sorted_locs(as,&order))return false;
    if(!resolve_views_sorted(as,true,order))goto failed;
    for(i=1U;i<=as->debug_file_max;++i)
        if(as->debug_files[i]==NULL) {
            minias_set_error(as,"missing-dwarf-file-index:%zu",i);
            goto failed;
        }
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
    sec=minias_ensure_section(as,".debug_line",MINIAS_SECTION_KIND_PROGBITS,0U,1U);
    if(sec<0)goto failed;
    /* GNU GAS refuses to concatenate a source-owned .debug_line payload
     * with the synthesized .file/.loc line program: their headers, row
     * ordering and relocation ownership are independent object producers.
     * An explicitly selected but empty section is not a conflicting owner. */
    if(minias_emission_section_size(as,sec)!=0U) {
        minias_set_error(as,"duplicate-dwarf-line-section");
        goto failed;
    }
    if(!append_u32(as,sec,0U) || !append_u16(as,sec,4U))goto failed;
    header_len_at=minias_emission_section_size(as,sec);
    if(!append_u32(as,sec,0U))goto failed;
    header_start=minias_emission_section_size(as,sec);
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
        if(!minias_section_append(as,sec,files[i].basename,
                                  strlen(files[i].basename)+1U) ||
           !append_leb(as,sec,files[i].directory_index) ||
           !append_leb(as,sec,0U) ||
           !append_leb(as,sec,0U))goto failed;
    if(!append_byte(as,sec,0U) ||
       !minias_section_patch_le32(as,sec,header_len_at,
                minias_emission_section_size(as,sec)-header_start))goto failed;
    for(begin=0U;begin<as->debug_loc_count;) {
        const MiniAsDebugLoc *first=&as->debug_locs[order[begin].ordinal];
        MiniAsSymbolView first_sym;
        unsigned cur_file=1U,cur_col=0U,cur_isa=0U;
        int64_t cur_line=1;
        bool cur_stmt=true;
        bool seen_row=false;
        uint64_t position;
        int source_section;
        size_t previous_symbol=first->symbol_index;
        if(!minias_common_context_symbol_view(as,first->symbol_index,
                                              &first_sym))goto failed;
        position=first_sym.value;
        source_section=first_sym.section;
        if(source_section<0 || (size_t)source_section>=as->section_count ||
           !set_address(as,sec,first->symbol_index,address_width))goto failed;
        for(j=begin;j<as->debug_loc_count;++j) {
            const MiniAsDebugLoc *loc=&as->debug_locs[order[j].ordinal];
            MiniAsSymbolView sym;
            int64_t line_diff;
            if(!minias_common_context_symbol_view(as,loc->symbol_index,
                                                  &sym))goto failed;
            if(sym.section!=source_section)break;
            /* GNU's location view is an address-identity constraint, not
             * a decorative .loc token: `view 0` asserts this row is view
             * zero, while `view -0` resets a repeated-PC view by emitting
             * a new DW_LNE_set_address at the same section SymbolID. */
            if(seen_row && sym.value==position && loc->view_mode==1U) {
                minias_set_error(as,"dwarf-view-number-mismatch");
                goto failed;
            }
            if(seen_row && sym.value==position && loc->view_mode==2U &&
               !set_address(as,sec,loc->symbol_index,address_width))goto failed;
            if(sym.value<position)goto failed;
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
            if(seen_row &&
               !advance_pc_between_symbols(as,sec,previous_symbol,
                   loc->symbol_index,sym.value-position,address_width))goto failed;
            position=sym.value;
            previous_symbol=loc->symbol_index;
            seen_row=true;
            if(!append_byte(as,sec,1U))goto failed;
        }
        if(position>as->sections[(size_t)source_section].layout_size)goto failed;
        if(as->target->dwarf_line_fixed_advance) {
            size_t end_symbol;
            if(!debug_section_end_anchor(as,source_section,&end_symbol) ||
               !advance_pc_between_symbols(as,sec,previous_symbol,end_symbol,
                   as->sections[(size_t)source_section].layout_size-position,
                   address_width))goto failed;
        } else if(!advance_pc(as,sec,
              as->sections[(size_t)source_section].layout_size-position))goto failed;
        if(!end_sequence(as,sec))goto failed;
        begin=j;
    }
    if(!minias_section_patch_le32(as,sec,0U,
                                 minias_emission_section_size(as,sec)-4U))goto failed;
    free(directory_slots);free(files);free(directories);free(order);return true;
failed:
    if(as->error[0]=='\0')minias_set_error(as,"dwarf-line-emission-failed");
    free(directory_slots);free(files);free(directories);free(order);return false;
}
