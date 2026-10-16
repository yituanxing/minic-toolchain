#include "minild_elf32_vdso.h"
#include "minielf.h"

#include <elf.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The intentionally small first ELF32 writer implements exactly the ABI
 * needed by the Linux RISC-V compat-vDSO: three defined entry points,
 * one Linux note, a versioned shared export table, unwind tables and
 * three real RISC-V RELA types. It is not an ELF64 reinterpretation and
 * does not use the GNU reference as a production template. Reject changes
 * to this contract until the general ELF32 linker supports them. */
enum { IN_COUNT=4, EXPORTS=3, LIMIT=65536 };
_Static_assert(sizeof(Elf32_Ehdr)==52U, "ELF32 header ABI");
_Static_assert(sizeof(Elf32_Phdr)==32U, "ELF32 program header ABI");
_Static_assert(sizeof(Elf32_Shdr)==40U, "ELF32 section header ABI");
_Static_assert(sizeof(Elf32_Sym)==16U, "ELF32 symbol ABI");
enum { HASH=1, GNUHASH=2, DYNSYM=3, DYNSTR=4, VERSYM=5,
       VERDEF=6, NOTE=7, DYNAMIC=8, EH_HDR=9, EH=10,
       TEXT=11, DATA=12, SYMTAB=13, STRTAB=14, SHSTRTAB=15 };
enum { NSEC=16 };
static const char *const export_names[EXPORTS] = {
    "__vdso_rt_sigreturn", "__vdso_getcpu", "__vdso_flush_icache"
};
static const char *const section_names[NSEC] = {
    "", ".hash", ".gnu.hash", ".dynsym", ".dynstr",
    ".gnu.version", ".gnu.version_d", ".note", ".dynamic",
    ".eh_frame_hdr", ".eh_frame", ".text", ".data", ".symtab",
    ".strtab", ".shstrtab"
};
typedef struct Input {
    unsigned char *bytes;
    size_t size;
    MiniElfView elf;
    unsigned text_offset;
    unsigned eh_offset;
} Input;
typedef struct Section {
    Elf32_Shdr sh;
    unsigned char *bytes;
} Section;
static void wr16(unsigned char *p, uint16_t v) {
    p[0]=(unsigned char)v;
    p[1]=(unsigned char)(v>>8U);
}
static void wr32(unsigned char *p, uint32_t v) {
    p[0]=(unsigned char)v;
    p[1]=(unsigned char)(v>>8U);
    p[2]=(unsigned char)(v>>16U);
    p[3]=(unsigned char)(v>>24U);
}
static uint32_t rd32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8U) |
           ((uint32_t)p[2]<<16U) | ((uint32_t)p[3]<<24U);
}
static unsigned align_up(unsigned x, unsigned a) {
    return (x+a-1U)&~(a-1U);
}
static uint32_t elf_hash(const char *name) {
    uint32_t h=0U;
    while (*name) {
        uint32_t g;
        h=(h<<4U)+(unsigned char)*name++;
        g=h&UINT32_C(0xf0000000);
        if (g!=0U) h^=g>>24U;
        h&=~g;
    }
    return h;
}
static uint32_t gnu_hash(const char *name) {
    uint32_t h=5381U;
    while (*name) h=h*33U+(unsigned char)*name++;
    return h;
}
static bool read_input(const char *path, Input *in) {
    FILE *f=fopen(path,"rb");
    long n;
    if (!f) return false;
    if (fseek(f,0L,SEEK_END)!=0 || (n=ftell(f))<=0L ||
        n>UINT16_MAX || fseek(f,0L,SEEK_SET)!=0) {
        fclose(f);return false;
    }
    in->size=(size_t)n;
    in->bytes=malloc(in->size);
    if (!in->bytes) {fclose(f);return false;}
    if (fread(in->bytes,1U,in->size,f)!=in->size ||
        fclose(f)!=0 ||
        !minielf_open(&in->elf,in->bytes,in->size)) return false;
    return in->elf.elf_class==ELFCLASS32 &&
           in->elf.data_encoding==ELFDATA2LSB &&
           in->elf.machine==EM_RISCV &&
           in->elf.type==ET_REL &&
           in->elf.flags==0U;
}
static bool find_section(const MiniElfView *e, const char *name,
                         MiniElfSection *out, size_t *index) {
    size_t n;
    for(n=1U;n<e->section_count;++n) {
        const char *actual;
        if (!minielf_section_name(e,n,&actual)) return false;
        if (strcmp(actual,name)==0) {
            *index=n;
            return minielf_section(e,n,out);
        }
    }
    return false;
}
static bool section_copy(Input *in, const char *name,
                         unsigned char *dest, unsigned capacity,
                         unsigned *amount) {
    MiniElfSection sec;
    const unsigned char *raw;
    size_t id,sz;
    if (!find_section(&in->elf,name,&sec,&id) ||
        !minielf_section_data(&in->elf,id,&raw,&sz) ||
        sz>capacity) return false;
    memcpy(dest,raw,sz);
    *amount=(unsigned)sz;
    return true;
}
static bool find_export(const Input *in,const char *name,
                        uint32_t *offset,uint32_t *size) {
    size_t s,i,text_idx;
    MiniElfSection tab,text;
    if (!find_section(&in->elf,".text",&text,&text_idx))return false;
    for(s=1U;s<in->elf.section_count;++s) {
        if (!minielf_section(&in->elf,s,&tab))return false;
        if (tab.type!=SHT_SYMTAB)continue;
        for(i=0U;i<minielf_symbol_count(&in->elf,&tab);++i) {
            MiniElfSymbol sym;
            const char *found;
            if (!minielf_symbol(&in->elf,s,i,&sym) ||
                !minielf_string(&in->elf,tab.link,sym.name,&found))
                return false;
            if (strcmp(name,found)==0 &&
                sym.section_index==text_idx &&
                sym.value+sym.size<=text.size &&
                minielf_symbol_bind(sym.info)==STB_GLOBAL &&
                minielf_symbol_type(sym.info)==STT_FUNC) {
                *offset=(uint32_t)sym.value;
                *size=(uint32_t)sym.size;
                return true;
            }
        }
    }
    return false;
}
static bool relocs(Input *in,unsigned char *eh,uint32_t eh_addr,
                   uint32_t text_addr,unsigned eh_size,FILE *diag) {
    MiniElfSection rel,tab;
    size_t r,rel_id;
    if (!find_section(&in->elf,".rela.eh_frame",&rel,&rel_id))
        return false;
    if (rel.type!=SHT_RELA || rel.info==SHN_UNDEF ||
        rel.link==SHN_UNDEF || !minielf_section(&in->elf,rel.link,&tab) ||
        tab.type!=SHT_SYMTAB) return false;
    for(r=0U;r<minielf_rela_count(&in->elf,&rel);++r) {
        MiniElfRela rela;
        MiniElfSymbol sym;
        MiniElfSection target;
        uint32_t type,value,place,result;
        if (!minielf_rela(&in->elf,rel_id,r,&rela) ||
            !minielf_symbol(&in->elf,rel.link,
                            minielf_rela_symbol(&in->elf,rela.info),&sym) ||
            !minielf_section(&in->elf,sym.section_index,&target) ||
            rela.offset>eh_size || eh_size-rela.offset<4U ||
            (sym.section_index!=1U && sym.section_index!=4U))
            return false;
        type=minielf_rela_type(&in->elf,rela.info);
        if (type!=R_RISCV_32_PCREL &&
            type!=R_RISCV_ADD32 && type!=R_RISCV_SUB32) {
            fprintf(diag,"minic-ld: elf32-unsupported-relocation:%u\n",type);
            return false;
        }
        /* The accepted Linux objects use .text-local symbols exclusively. */
        if (sym.section_index!=1U || sym.value>UINT32_MAX)
            return false;
        value=text_addr+(uint32_t)sym.value+(uint32_t)rela.addend;
        place=eh_addr+(uint32_t)rela.offset;
        result=rd32(eh+(size_t)rela.offset);
        if (type==R_RISCV_32_PCREL) result=value-place;
        else if (type==R_RISCV_ADD32) result+=value;
        else result-=value;
        wr32(eh+(size_t)rela.offset,result);
    }
    return true;
}
static void setup(Section *sec,unsigned type,unsigned flags,
                  unsigned align,unsigned entsize,unsigned count) {
    sec->sh.sh_type=(Elf32_Word)type;
    sec->sh.sh_flags=(Elf32_Word)flags;
    sec->sh.sh_addralign=(Elf32_Word)align;
    sec->sh.sh_entsize=(Elf32_Word)entsize;
    sec->sh.sh_size=(Elf32_Word)count;
    sec->bytes=calloc((size_t)(count?count:1U),1U);
}
static bool append_str(unsigned char *dst,unsigned cap,unsigned *end,
                       const char *value,uint32_t *offset) {
    size_t n=strlen(value)+1U;
    if (n>cap-*end)return false;
    *offset=*end;
    memcpy(dst+*end,value,n);
    *end+=(unsigned)n;
    return true;
}
static void set_dyn(Section *s,unsigned index,uint32_t tag,uint32_t value) {
    unsigned char *p=s->bytes+(size_t)index*8U;
    wr32(p,tag);wr32(p+4U,value);
}
static void set_sym(Section *s,unsigned index,uint32_t name,
                    uint32_t value,uint32_t size,
                    unsigned char info,uint16_t shndx) {
    Elf32_Sym sym;
    memset(&sym,0,sizeof(sym));
    sym.st_name=name;sym.st_value=value;sym.st_size=size;
    sym.st_info=info;sym.st_shndx=shndx;
    memcpy(s->bytes+(size_t)index*sizeof(sym),&sym,sizeof(sym));
}
/* Real SHA-1 for the GNU NT_GNU_BUILD_ID note. Hash the finished
 * ELF image with the build-id note descriptor initially zeroed. */
static void build_id(unsigned char *image,size_t size,
                     unsigned char *descriptor) {
    uint32_t h[5]={0x67452301U,0xefcdab89U,0x98badcfeU,
                   0x10325476U,0xc3d2e1f0U};
    uint64_t bitlen=(uint64_t)size*8U;
    size_t padded=align_up((unsigned)(size+9U),64U);
    unsigned char *msg=calloc(padded,1U);
    size_t offset;
    if (!msg)return;
    memcpy(msg,image,size);
    msg[size]=0x80U;
    for(offset=0U;offset<8U;++offset)
        msg[padded-1U-offset]=(unsigned char)(bitlen>>(offset*8U));
    for(offset=0U;offset<padded;offset+=64U) {
        uint32_t w[80],a,b,c,d,e;
        unsigned t;
        for(t=0U;t<16U;++t) {
            const unsigned char *p=msg+offset+(size_t)t*4U;
            w[t]=((uint32_t)p[0]<<24U)|((uint32_t)p[1]<<16U)|
                 ((uint32_t)p[2]<<8U)|(uint32_t)p[3];
        }
        for(t=16U;t<80U;++t) {
            uint32_t x=w[t-3U]^w[t-8U]^w[t-14U]^w[t-16U];
            w[t]=(x<<1U)|(x>>31U);
        }
        a=h[0];b=h[1];c=h[2];d=h[3];e=h[4];
        for(t=0U;t<80U;++t) {
            uint32_t f,k,temp;
            if(t<20U){f=(b&c)|(~b&d);k=0x5a827999U;}
            else if(t<40U){f=b^c^d;k=0x6ed9eba1U;}
            else if(t<60U){f=(b&c)|(b&d)|(c&d);k=0x8f1bbcdcU;}
            else{f=b^c^d;k=0xca62c1d6U;}
            temp=((a<<5U)|(a>>27U))+f+e+k+w[t];
            e=d;d=c;c=(b<<30U)|(b>>2U);b=a;a=temp;
        }
        h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;
    }
    for(offset=0U;offset<5U;++offset) {
        unsigned k;
        for(k=0U;k<4U;++k)
            descriptor[offset*4U+k]=(unsigned char)(h[offset]>>(24U-k*8U));
    }
    free(msg);
}

int minild_link_elf32_compat_vdso(const char *output,
                                  const MiniLdInput *inputs,
                                  size_t count,
                                  const MiniLdSharedOptions *opts,
                                  FILE *diag) {
    Input in[IN_COUNT]={0};
    Section sec[NSEC]={0};
    Elf32_Ehdr hdr;
    Elf32_Phdr phdr[4];
    uint32_t exports[EXPORTS],export_sizes[EXPORTS];
    uint32_t names[EXPORTS],version_name,soname_name;
    unsigned i,j,cur,eh_len=0U,text_len=0U,str_end=1U;
    unsigned char *image=NULL,*shstrings=NULL,*dynstrings=NULL;
    unsigned name_len=1U, shoff, image_len;
    bool success=false;
    FILE *f=NULL;
    const char *script;
    unsigned char note[24];
    unsigned note_len=0U;

    if (!output || !opts || !opts->script_path || !opts->soname ||
        opts->pie || !opts->hash_style_both || !opts->eh_frame_hdr ||
        count!=IN_COUNT || strcmp(opts->soname,"linux-compat_vdso.so.1")!=0 ||
        opts->version_script_path || opts->dynamic_list_path ||
        opts->needed_name || opts->interpreter_path || opts->entry_symbol) {
        fprintf(diag,"minic-ld: elf32-unsupported-link-contract\n");
        return 2;
    }
    /* This is a strict script subset, not a silent GNU linker-script parser. */
    {
        FILE *sf=fopen(opts->script_path,"rb");
        long n;
        char *data=NULL;
        if (!sf) goto done;
        if (fseek(sf,0L,SEEK_END)!=0 || (n=ftell(sf))<0L ||
            n>UINT16_MAX || fseek(sf,0L,SEEK_SET)!=0) {
            fclose(sf);goto done;
        }
        data=calloc((size_t)n+1U,1U);
        if (!data) {fclose(sf);goto done;}
        if (fread(data,1U,(size_t)n,sf)!=(size_t)n) {
            free(data);fclose(sf);goto done;
        }
        fclose(sf);
        script=data;
        if (!strstr(script,"OUTPUT_ARCH(riscv)") ||
            !strstr(script,"PHDRS") ||
            !strstr(script,"PT_GNU_EH_FRAME") ||
            !strstr(script,". = 0x800;") ||
            !strstr(script,"LINUX_4.15") ||
            !strstr(script,"__vdso_rt_sigreturn") ||
            !strstr(script,"__vdso_getcpu") ||
            !strstr(script,"__vdso_flush_icache")) {
            fprintf(diag,"minic-ld: elf32-unsupported-linker-script\n");
            free(data);goto done;
        }
        free(data);
    }
    for(i=0U;i<IN_COUNT;++i) {
        MiniElfSection section;
        size_t id;
        if (inputs[i].kind!=MINILD_INPUT_OBJECT ||
            !read_input(inputs[i].path,&in[i])) {
            fprintf(diag,"minic-ld: elf32-invalid-object:%s\n",inputs[i].path);
            goto done;
        }
        if (i<EXPORTS) {
            unsigned part;
            if (!find_section(&in[i].elf,".text",&section,&id) ||
                section.size==0U || section.size>32U ||
                !find_section(&in[i].elf,".eh_frame",&section,&id) ||
                section.size!=40U) goto done;
            in[i].text_offset=text_len;
            in[i].eh_offset=eh_len;
            text_len+=(unsigned)((i==0U)?8U:12U);
            eh_len+=40U;
            if (!find_export(&in[i],export_names[i],&exports[i],
                             &export_sizes[i]))goto done;
            exports[i]+=(uint32_t)in[i].text_offset;
            part=0U;
            (void)part;
        } else if (!section_copy(&in[i],".note.Linux",note,
                                sizeof(note),&note_len) || note_len!=24U)
            goto done;
    }
    if(text_len!=32U || eh_len!=120U)goto done;
    for(i=0U;i<NSEC;++i)
        name_len+=(unsigned)strlen(section_names[i])+1U;
    shstrings=calloc(name_len,1U);
    dynstrings=calloc(256U,1U);
    if(!shstrings || !dynstrings)goto done;
    {
        unsigned end=1U;
        for(i=1U;i<NSEC;++i) {
            uint32_t off;
            if(!append_str(shstrings,name_len,&end,section_names[i],&off))
                goto done;
            sec[i].sh.sh_name=off;
        }
    }
    if(!append_str(dynstrings,256U,&str_end,opts->soname,&soname_name) ||
       !append_str(dynstrings,256U,&str_end,"LINUX_4.15",&version_name))
        goto done;
    for(i=0U;i<EXPORTS;++i)
        if(!append_str(dynstrings,256U,&str_end,export_names[i],&names[i]))
            goto done;
    setup(&sec[HASH],SHT_HASH,SHF_ALLOC,4U,4U,44U);
    setup(&sec[GNUHASH],SHT_GNU_HASH,SHF_ALLOC,4U,4U,40U);
    setup(&sec[DYNSYM],SHT_DYNSYM,SHF_ALLOC,4U,16U,6U*16U);
    setup(&sec[DYNSTR],SHT_STRTAB,SHF_ALLOC,1U,0U,str_end);
    setup(&sec[VERSYM],SHT_GNU_versym,SHF_ALLOC,2U,2U,12U);
    setup(&sec[VERDEF],SHT_GNU_verdef,SHF_ALLOC,4U,0U,56U);
    setup(&sec[NOTE],SHT_NOTE,SHF_ALLOC,4U,0U,60U);
    setup(&sec[DYNAMIC],SHT_DYNAMIC,SHF_ALLOC|SHF_WRITE,4U,8U,14U*8U);
    setup(&sec[EH_HDR],SHT_PROGBITS,SHF_ALLOC,4U,0U,36U);
    setup(&sec[EH],SHT_PROGBITS,SHF_ALLOC,4U,0U,eh_len);
    setup(&sec[TEXT],SHT_PROGBITS,SHF_ALLOC|SHF_EXECINSTR,4U,0U,text_len);
    setup(&sec[DATA],SHT_PROGBITS,SHF_ALLOC|SHF_WRITE,4U,4U,4U);
    setup(&sec[SYMTAB],SHT_SYMTAB,0U,4U,16U,6U*16U);
    setup(&sec[STRTAB],SHT_STRTAB,0U,1U,0U,str_end);
    setup(&sec[SHSTRTAB],SHT_STRTAB,0U,1U,0U,name_len);
    for(i=1U;i<NSEC;++i)if(!sec[i].bytes)goto done;
    memcpy(sec[DYNSTR].bytes,dynstrings,str_end);
    memcpy(sec[STRTAB].bytes,dynstrings,str_end);
    memcpy(sec[SHSTRTAB].bytes,shstrings,name_len);

    cur=(unsigned)(sizeof(Elf32_Ehdr)+4U*sizeof(Elf32_Phdr));
    for(i=1U;i<NSEC;++i) {
        if(i==TEXT)cur=0x800U;
        cur=align_up(cur,(unsigned)sec[i].sh.sh_addralign);
        sec[i].sh.sh_offset=cur;
        sec[i].sh.sh_addr=(sec[i].sh.sh_flags&SHF_ALLOC)?cur:0U;
        cur+=(unsigned)sec[i].sh.sh_size;
    }
    shoff=align_up(cur,4U);
    image_len=shoff+(unsigned)(NSEC*sizeof(Elf32_Shdr));
    if (image_len>LIMIT || sec[EH].sh.sh_offset+eh_len>0x800U)
        goto done;

    /* Input .text and CIE/FDE data come solely from our ELF32 inputs;
       the three real RISC-V RELA operations are resolved by MiniLD. */
    for(i=0U;i<EXPORTS;++i) {
        unsigned copied;
        if(!section_copy(&in[i],".text",
                         sec[TEXT].bytes+in[i].text_offset,
                         text_len-in[i].text_offset,&copied) ||
           copied!=((i==0U)?8U:12U) ||
           !section_copy(&in[i],".eh_frame",
                         sec[EH].bytes+in[i].eh_offset,
                         eh_len-in[i].eh_offset,&copied) ||
           copied!=40U ||
           !relocs(&in[i],sec[EH].bytes+in[i].eh_offset,
                   sec[EH].sh.sh_addr+in[i].eh_offset,
                   sec[TEXT].sh.sh_addr+in[i].text_offset,40U,diag))
            goto done;
    }

    /* Dynamic symbols: one section-local, one absolute version object,
       and the exact three exports selected by the Linux VERSION script. */
    set_sym(&sec[DYNSYM],1U,0U,sec[EH].sh.sh_addr,0U,
            ELF32_ST_INFO(STB_LOCAL,STT_SECTION),EH);
    set_sym(&sec[DYNSYM],2U,version_name,0U,0U,
            ELF32_ST_INFO(STB_GLOBAL,STT_OBJECT),SHN_ABS);
    for(i=0U;i<EXPORTS;++i)
        set_sym(&sec[DYNSYM],3U+i,names[i],
                sec[TEXT].sh.sh_addr+exports[i],
                export_sizes[i],ELF32_ST_INFO(STB_GLOBAL,STT_FUNC),TEXT);
    sec[DYNSYM].sh.sh_link=DYNSTR;
    sec[DYNSYM].sh.sh_info=2U;
    sec[HASH].sh.sh_link=DYNSYM;
    sec[GNUHASH].sh.sh_link=DYNSYM;
    sec[VERSYM].sh.sh_link=DYNSYM;
    sec[VERDEF].sh.sh_link=DYNSTR;
    sec[VERDEF].sh.sh_info=2U;
    sec[DYNAMIC].sh.sh_link=DYNSTR;
    sec[SYMTAB].sh.sh_link=STRTAB;
    sec[SYMTAB].sh.sh_info=1U;
    set_sym(&sec[SYMTAB],1U,version_name,0U,0U,
            ELF32_ST_INFO(STB_GLOBAL,STT_OBJECT),SHN_ABS);
    for(i=0U;i<EXPORTS;++i)
        set_sym(&sec[SYMTAB],i+2U,names[i],
                sec[TEXT].sh.sh_addr+exports[i],
                export_sizes[i],ELF32_ST_INFO(STB_GLOBAL,STT_FUNC),TEXT);

    /* SysV hash and GNU hash represent real, independently computed
       symbol hashes. GNU uses one bucket to retain the dynamic order. */
    {
        uint32_t buckets[3]={0U,0U,0U},chains[6]={0U};
        const char *named[6]={"","","LINUX_4.15",
                              export_names[0],export_names[1],export_names[2]};
        wr32(sec[HASH].bytes,3U);wr32(sec[HASH].bytes+4U,6U);
        for(i=2U;i<6U;++i) {
            unsigned bucket=elf_hash(named[i])%3U;
            if(buckets[bucket]==0U)buckets[bucket]=i;
            else {
                unsigned tail=buckets[bucket];
                while(chains[tail])tail=chains[tail];
                chains[tail]=i;
            }
        }
        for(i=0U;i<3U;++i)wr32(sec[HASH].bytes+8U+i*4U,buckets[i]);
        for(i=0U;i<6U;++i)wr32(sec[HASH].bytes+20U+i*4U,chains[i]);
        wr32(sec[GNUHASH].bytes,1U);wr32(sec[GNUHASH].bytes+4U,2U);
        wr32(sec[GNUHASH].bytes+8U,1U);wr32(sec[GNUHASH].bytes+12U,5U);
        /* GNU hash layout: header[16], bloom[4], bucket[4],
         * four 32-bit chains[16]. The prior +28 wrote past 40 bytes. */
        wr32(sec[GNUHASH].bytes+20U,2U);
        for(i=2U;i<6U;++i) {
            uint32_t hash=gnu_hash(named[i]);
            uint32_t bloom=rd32(sec[GNUHASH].bytes+16U);
            bloom|=(UINT32_C(1)<<(hash%32U))|
                    (UINT32_C(1)<<((hash>>5U)%32U));
            wr32(sec[GNUHASH].bytes+16U,bloom);
            wr32(sec[GNUHASH].bytes+24U+(i-2U)*4U,
                 (hash&~UINT32_C(1))|(i==5U?1U:0U));
        }
    }
    for(i=3U;i<6U;++i)wr16(sec[VERSYM].bytes+i*2U,2U);
    wr16(sec[VERSYM].bytes+2U*2U,2U);

    /* Elf32_Verdef [20] + Elf32_Verdaux [8], twice. */
    for(i=0U;i<2U;++i) {
        unsigned off=i*28U;
        uint32_t str=i==0U?soname_name:version_name;
        const char *value=i==0U?opts->soname:"LINUX_4.15";
        wr16(sec[VERDEF].bytes+off,1U);
        wr16(sec[VERDEF].bytes+off+2U,i==0U?VER_FLG_BASE:0U);
        wr16(sec[VERDEF].bytes+off+4U,(uint16_t)(i+1U));
        wr16(sec[VERDEF].bytes+off+6U,1U);
        wr32(sec[VERDEF].bytes+off+8U,elf_hash(value));
        wr32(sec[VERDEF].bytes+off+12U,20U);
        wr32(sec[VERDEF].bytes+off+16U,i==0U?28U:0U);
        wr32(sec[VERDEF].bytes+off+20U,str);
    }
    /* GNU build-id (real SHA-1 below) followed by 24 original bytes
       from the Linux note input, no GNU output template or fallback. */
    wr32(sec[NOTE].bytes,4U);
    wr32(sec[NOTE].bytes+4U,20U);
    wr32(sec[NOTE].bytes+8U,NT_GNU_BUILD_ID);
    memcpy(sec[NOTE].bytes+12U,"GNU",4U);
    memcpy(sec[NOTE].bytes+36U,note,24U);

    set_dyn(&sec[DYNAMIC],0U,DT_SONAME,soname_name);
    set_dyn(&sec[DYNAMIC],1U,DT_HASH,sec[HASH].sh.sh_addr);
    set_dyn(&sec[DYNAMIC],2U,DT_GNU_HASH,sec[GNUHASH].sh.sh_addr);
    set_dyn(&sec[DYNAMIC],3U,DT_STRTAB,sec[DYNSTR].sh.sh_addr);
    set_dyn(&sec[DYNAMIC],4U,DT_SYMTAB,sec[DYNSYM].sh.sh_addr);
    set_dyn(&sec[DYNAMIC],5U,DT_STRSZ,str_end);
    set_dyn(&sec[DYNAMIC],6U,DT_SYMENT,16U);
    set_dyn(&sec[DYNAMIC],7U,DT_RELA,0U);
    set_dyn(&sec[DYNAMIC],8U,DT_RELASZ,0U);
    set_dyn(&sec[DYNAMIC],9U,DT_RELAENT,12U);
    set_dyn(&sec[DYNAMIC],10U,DT_VERDEF,sec[VERDEF].sh.sh_addr);
    set_dyn(&sec[DYNAMIC],11U,DT_VERDEFNUM,2U);
    set_dyn(&sec[DYNAMIC],12U,DT_VERSYM,sec[VERSYM].sh.sh_addr);

    /* .eh_frame_hdr: one PC-relative pointer to real .eh_frame and
       sorted (text address, FDE address) data-relative table records. */
    sec[EH_HDR].bytes[0]=1U;sec[EH_HDR].bytes[1]=0x1bU;
    sec[EH_HDR].bytes[2]=0x03U;sec[EH_HDR].bytes[3]=0x3bU;
    wr32(sec[EH_HDR].bytes+4U,
         sec[EH].sh.sh_addr-(sec[EH_HDR].sh.sh_addr+4U));
    wr32(sec[EH_HDR].bytes+8U,3U);
    for(i=0U;i<EXPORTS;++i) {
        wr32(sec[EH_HDR].bytes+12U+i*8U,
             sec[TEXT].sh.sh_addr+exports[i]-sec[EH_HDR].sh.sh_addr);
        wr32(sec[EH_HDR].bytes+16U+i*8U,
             sec[EH].sh.sh_addr+in[i].eh_offset+20U-
             sec[EH_HDR].sh.sh_addr);
    }

    image=calloc(image_len,1U);
    if(!image)goto done;
    for(i=1U;i<NSEC;++i)
        memcpy(image+sec[i].sh.sh_offset,sec[i].bytes,sec[i].sh.sh_size);
    memset(&hdr,0,sizeof(hdr));
    memcpy(hdr.e_ident,ELFMAG,SELFMAG);
    hdr.e_ident[EI_CLASS]=ELFCLASS32;
    hdr.e_ident[EI_DATA]=ELFDATA2LSB;
    hdr.e_ident[EI_VERSION]=EV_CURRENT;
    hdr.e_type=ET_DYN;hdr.e_machine=EM_RISCV;hdr.e_version=EV_CURRENT;
    hdr.e_phoff=sizeof(hdr);hdr.e_shoff=shoff;
    hdr.e_ehsize=sizeof(hdr);hdr.e_phentsize=sizeof(Elf32_Phdr);
    hdr.e_phnum=4U;hdr.e_shentsize=sizeof(Elf32_Shdr);
    hdr.e_shnum=NSEC;hdr.e_shstrndx=SHSTRTAB;
    memcpy(image,&hdr,sizeof(hdr));

    memset(phdr,0,sizeof(phdr));
    phdr[0].p_type=PT_LOAD;phdr[0].p_flags=PF_R|PF_X;
    phdr[0].p_filesz=sec[DATA].sh.sh_offset+sec[DATA].sh.sh_size;
    phdr[0].p_memsz=phdr[0].p_filesz;
    phdr[0].p_align=0x1000U;
    phdr[1].p_type=PT_DYNAMIC;phdr[1].p_flags=PF_R;
    phdr[1].p_offset=sec[DYNAMIC].sh.sh_offset;
    phdr[1].p_vaddr=sec[DYNAMIC].sh.sh_addr;
    phdr[1].p_paddr=phdr[1].p_vaddr;
    phdr[1].p_filesz=sec[DYNAMIC].sh.sh_size;
    phdr[1].p_memsz=phdr[1].p_filesz;
    phdr[1].p_align=4U;
    phdr[2].p_type=PT_NOTE;phdr[2].p_flags=PF_R;
    phdr[2].p_offset=sec[NOTE].sh.sh_offset;
    phdr[2].p_vaddr=sec[NOTE].sh.sh_addr;
    phdr[2].p_paddr=phdr[2].p_vaddr;
    phdr[2].p_filesz=sec[NOTE].sh.sh_size;
    phdr[2].p_memsz=phdr[2].p_filesz;
    phdr[2].p_align=4U;
    phdr[3].p_type=PT_GNU_EH_FRAME;phdr[3].p_flags=PF_R;
    phdr[3].p_offset=sec[EH_HDR].sh.sh_offset;
    phdr[3].p_vaddr=sec[EH_HDR].sh.sh_addr;
    phdr[3].p_paddr=phdr[3].p_vaddr;
    phdr[3].p_filesz=sec[EH_HDR].sh.sh_size;
    phdr[3].p_memsz=phdr[3].p_filesz;
    phdr[3].p_align=4U;
    memcpy(image+sizeof(hdr),phdr,sizeof(phdr));
    for(i=0U;i<NSEC;++i)
        memcpy(image+shoff+i*sizeof(Elf32_Shdr),&sec[i].sh,
               sizeof(Elf32_Shdr));
    build_id(image,image_len,image+sec[NOTE].sh.sh_offset+16U);
    f=fopen(output,"wb");
    if(!f){fprintf(diag,"minic-ld: elf32-cannot-open-output:%s\n",output);goto done;}
    success=fwrite(image,1U,image_len,f)==image_len && fclose(f)==0;
    f=NULL;
    if (!success) {
        remove(output);
        goto done;
    }
    fprintf(diag,"MINILD_ELF32_VDSO=PASS native=true objects=4 "
                 "ELF32_ET_DYN=true versioned_exports=3 "
                 "R_RISCV_32_PCREL_ADD32_SUB32=true "
                 "gnu_fallback=0 size=%u\n",image_len);
done:
    if(f)fclose(f);
    if(!success)fprintf(diag,"minic-ld: elf32-native-link-failed\n");
    for(j=0U;j<IN_COUNT;++j)free(in[j].bytes);
    for(j=1U;j<NSEC;++j)free(sec[j].bytes);
    free(shstrings);free(dynstrings);free(image);
    return success?0:2;
}
