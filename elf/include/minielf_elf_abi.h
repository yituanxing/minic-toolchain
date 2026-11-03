#ifndef MINIELF_ELF_ABI_H
#define MINIELF_ELF_ABI_H

/* Portable ELF wire constants for MiniELF's reader/relocatable writer and
 * MiniAS's ELF projection.  MinGW does not provide the Linux <elf.h> header.
 * Keep the Windows definitions limited to the ABI values actually consumed
 * by these object paths; on other hosts use the system ELF ABI definitions. */
#if !defined(_WIN32)
#include <elf.h>
#else

#include <stdint.h>

/* e_ident and ELF identification. */
#define EI_CLASS         4U
#define EI_DATA          5U
#define EI_VERSION       6U
#define EI_OSABI         7U
#define EI_NIDENT       16U
#define ELFMAG          "\177ELF"
#define SELFMAG          4U
#define ELFCLASS32       1U
#define ELFCLASS64       2U
#define ELFDATA2LSB      1U
#define ELFDATA2MSB      2U
#define ELFOSABI_NONE    0U
#define ELFOSABI_GNU     3U
#define EV_CURRENT       1U

/* File, program and section identity. */
#define ET_REL           1U
#define PT_LOAD          1U
#define SHN_UNDEF        0U
#define SHN_ABS          0xfff1U
#define SHN_COMMON       0xfff2U
#define SHT_PROGBITS     1U
#define SHT_SYMTAB       2U
#define SHT_STRTAB       3U
#define SHT_RELA         4U
#define SHT_NOTE         7U
#define SHT_NOBITS       8U
#define SHT_REL          9U
#define SHT_DYNSYM      11U
#define SHT_INIT_ARRAY  14U
#define SHT_FINI_ARRAY  15U
#define SHT_PREINIT_ARRAY 16U
#define SHT_GROUP       17U

/* Section-header flags and section-group semantics. */
#define SHF_WRITE        (UINT64_C(1) << 0)
#define SHF_ALLOC        (UINT64_C(1) << 1)
#define SHF_EXECINSTR    (UINT64_C(1) << 2)
#define SHF_MERGE        (UINT64_C(1) << 4)
#define SHF_STRINGS      (UINT64_C(1) << 5)
#define SHF_INFO_LINK    (UINT64_C(1) << 6)
#define SHF_LINK_ORDER   (UINT64_C(1) << 7)
#define SHF_GROUP        (UINT64_C(1) << 9)
#define SHF_TLS          (UINT64_C(1) << 10)
#define SHF_GNU_RETAIN   (UINT64_C(1) << 21)
#define GRP_COMDAT       1U

/* Symbol binding, symbol type and visibility. */
#define STB_LOCAL        0U
#define STB_GLOBAL       1U
#define STB_WEAK         2U
#define STB_GNU_UNIQUE  10U
#define STT_NOTYPE       0U
#define STT_OBJECT       1U
#define STT_FUNC         2U
#define STT_SECTION      3U
#define STT_FILE         4U
#define STT_TLS          6U
#define STT_GNU_IFUNC   10U
#define STV_DEFAULT      0U
#define STV_INTERNAL     1U
#define STV_HIDDEN       2U
#define STV_PROTECTED    3U

#endif /* _WIN32 */
#endif /* MINIELF_ELF_ABI_H */
