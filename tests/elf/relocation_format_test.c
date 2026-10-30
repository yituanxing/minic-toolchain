#include "minielf.h"

#include <elf.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool write_file(const char *path, const unsigned char *bytes,
                       size_t count) {
    FILE *out = fopen(path, "wb");
    if (out == NULL) return false;
    bool ok = fwrite(bytes, 1U, count, out) == count;
    if (fclose(out) != 0) ok = false;
    return ok;
}

static uint32_t read_le32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) |
           ((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U);
}

static bool generate(const char *path, bool implicit_addend) {
    /* i386: call callee; mov eax,1; xor ebx,ebx; int 0x80.
     * Linux IA-32 sys_exit(0) means this linked image is directly runnable. */
    static const unsigned char i386_text[] = {
        0xe8U, 0U, 0U, 0U, 0U,
        0xb8U, 1U, 0U, 0U, 0U,
        0x31U, 0xdbU, 0xcdU, 0x80U
    };
    static const unsigned char rv32_data[] = {0U, 0U, 0U, 0U};
    const unsigned char *data=implicit_addend?i386_text:rv32_data;
    size_t size=implicit_addend?sizeof(i386_text):sizeof(rv32_data);
    MiniElfWriteSection section = {0};
    MiniElfWriteSymbol symbols[2] = {{0}, {0}};
    MiniElfWriteRela reloc = {0};
    MiniElfRelocatableSpec spec = {0};
    MiniElfWriteError error = MINIELF_WRITE_OK;
    unsigned char *image=NULL;
    size_t image_size=0U;
    bool ok;

    section.name=implicit_addend?".text":".data";
    section.type=SHT_PROGBITS;
    section.flags=implicit_addend?(SHF_ALLOC|SHF_EXECINSTR):
                                      (SHF_ALLOC|SHF_WRITE);
    section.alignment=4U;
    section.data=data;
    section.size=size;

    symbols[0].name=implicit_addend?"_start":"_rv_data";
    symbols[0].info=(unsigned char)((STB_GLOBAL<<4U)|
                                    (implicit_addend?STT_FUNC:STT_OBJECT));
    symbols[0].section_index=1U;
    symbols[0].size=size;
    symbols[1].name=implicit_addend?"callee":"external_rv32";
    symbols[1].info=(unsigned char)((STB_GLOBAL<<4U)|STT_NOTYPE);
    symbols[1].section_index=SHN_UNDEF;

    reloc.target_section=0U;
    reloc.offset=implicit_addend?1U:0U;
    reloc.symbol_index=1U;
    reloc.type=implicit_addend?R_386_PC32:R_RISCV_32;
    reloc.addend=implicit_addend?-4:5;
    reloc.field_width=4U;

    spec.elf_class=ELFCLASS32;
    spec.data_encoding=ELFDATA2LSB;
    spec.machine=(uint16_t)(implicit_addend?EM_386:EM_RISCV);
    spec.relocation_format=implicit_addend?MINIELF_RELOC_FORMAT_REL:
                                            MINIELF_RELOC_FORMAT_RELA;
    spec.sections=&section;
    spec.section_count=1U;
    spec.symbols=symbols;
    spec.symbol_count=2U;
    spec.relocations=&reloc;
    spec.relocation_count=1U;
    spec.emit_section_symbols=true;
    ok=minielf_build_relocatable(&spec,&image,&image_size,&error);
    if(!ok) {
        fprintf(stderr,"minielf: %s\n",minielf_write_error_string(error));
        free(image);
        return false;
    }
    /* A REL patch must not mutate the caller's retained Section bytes.
     * The outgoing ELF32 Section #1 does contain the implicit -4 addend,
     * while the RV32 RELA Section remains unchanged (addend is explicit). */
    if(image_size<52U || data[implicit_addend?1U:0U] != 0U) {
        ok=false;
    } else {
        size_t shoff=read_le32(image+32U);
        if(shoff>image_size || image_size-shoff<80U)ok=false;
        else {
            size_t section_offset=read_le32(image+shoff+40U+16U);
            if(section_offset>image_size || size>image_size-section_offset)
                ok=false;
            else if(implicit_addend) {
                static const unsigned char minus_four[] = {
                    0xfcU, 0xffU, 0xffU, 0xffU
                };
                if(memcmp(image+section_offset+1U,
                          minus_four,sizeof(minus_four))!=0)ok=false;
            } else if(memcmp(image+section_offset,rv32_data,
                             sizeof(rv32_data))!=0)ok=false;
        }
    }
    if(ok)ok=write_file(path,image,image_size);
    free(image);

    /* The writer also rejects an out-of-bounds implicit addend site. */
    if(ok && implicit_addend) {
        reloc.offset=size;
        image=NULL;
        image_size=0U;
        error=MINIELF_WRITE_OK;
        if(minielf_build_relocatable(&spec,&image,&image_size,&error) ||
           error!=MINIELF_WRITE_INVALID_RELOCATION)ok=false;
        free(image);
        reloc.offset=1U;
        reloc.field_width=0U;
        image=NULL;
        error=MINIELF_WRITE_OK;
        if(minielf_build_relocatable(&spec,&image,&image_size,&error) ||
           error!=MINIELF_WRITE_INVALID_RELOCATION)ok=false;
        free(image);
    }
    return ok;
}

int main(int argc, char **argv) {
    if(argc!=3) {
        fprintf(stderr,"usage: relocation_format_test I386.o RV32.o\n");
        return 2;
    }
    if(!generate(argv[1],true) || !generate(argv[2],false))return 1;
    puts("MINIELF_RELOCATION_FORMAT=PASS elf32_REL_i386=1 elf32_RELA_rv32=1");
    return 0;
}
