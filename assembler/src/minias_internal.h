#ifndef MINIAS_INTERNAL_H
#define MINIAS_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define MINIAS_SHF_WRITE 1ULL
#define MINIAS_SHF_ALLOC 2ULL
#define MINIAS_SHF_EXECINSTR 4ULL
#define MINIAS_SHT_PROGBITS 1U
#define MINIAS_SHT_NOBITS 8U

#define MINIAS_STB_LOCAL 0U
#define MINIAS_STB_GLOBAL 1U
#define MINIAS_STB_WEAK 2U
#define MINIAS_STT_NOTYPE 0U
#define MINIAS_STT_OBJECT 1U
#define MINIAS_STT_FUNC 2U
#define MINIAS_STV_DEFAULT 0U
#define MINIAS_STV_INTERNAL 1U
#define MINIAS_STV_HIDDEN 2U
#define MINIAS_STV_PROTECTED 3U

#define MINIAS_R_RISCV_64 2U
#define MINIAS_R_RISCV_PCREL_HI20 23U
#define MINIAS_R_RISCV_PCREL_LO12_I 24U
#define MINIAS_R_RISCV_RELAX 51U

#define MINIAS_SECTION_UNDEF (-1)
#define MINIAS_MAX_ERROR 256U

typedef struct MiniAsSection {
    char *name;
    uint32_t type;
    uint64_t flags;
    uint64_t align;
    unsigned char *data;
    size_t size;
    size_t capacity;
} MiniAsSection;

typedef struct MiniAsSymbol {
    char *name;
    int section;
    uint64_t value;
    uint64_t size;
    uint8_t bind;
    uint8_t type;
    uint8_t visibility;
    bool defined;
} MiniAsSymbol;

typedef enum MiniAsStmtKind {
    MINIAS_STMT_INSN,
    MINIAS_STMT_ALIGN,
    MINIAS_STMT_DATA
} MiniAsStmtKind;

typedef struct MiniAsReloc {
    int section;
    uint64_t offset;
    uint32_t type;
    size_t symbol_index;
    int64_t addend;
} MiniAsReloc;

typedef struct MiniAsSymbolExpr {
    char name[256];
    int64_t addend;
} MiniAsSymbolExpr;

typedef struct MiniAsStmt {
    MiniAsStmtKind kind;
    char *op;
    char *args;
    size_t line;
    int section;
    uint64_t offset;
    uint32_t size;
    uint64_t align;
} MiniAsStmt;

typedef struct MiniAs {
    MiniAsSection *sections;
    size_t section_count;
    size_t section_capacity;
    MiniAsSymbol *symbols;
    size_t symbol_count;
    size_t symbol_capacity;
    MiniAsStmt *stmts;
    size_t stmt_count;
    size_t stmt_capacity;
    MiniAsReloc *relocs;
    size_t reloc_count;
    size_t reloc_capacity;
    size_t pcrel_anchor_counter;
    int current_section;
    char error[MINIAS_MAX_ERROR];
} MiniAs;

void minias_init(MiniAs *as);
void minias_destroy(MiniAs *as);
bool minias_parse_file(MiniAs *as, const char *path);
bool minias_emit_sections(MiniAs *as);
bool minias_write_elf64(MiniAs *as, const char *path);

int minias_find_section(const MiniAs *as, const char *name);
int minias_ensure_section(MiniAs *as, const char *name, uint32_t type, uint64_t flags, uint64_t align);
MiniAsSymbol *minias_get_symbol(MiniAs *as, const char *name, bool create);
bool minias_add_relocation(MiniAs *as,
                           int section,
                           uint64_t offset,
                           uint32_t type,
                           const char *symbol_name,
                           int64_t addend);
bool minias_parse_symbol_addend(const char *text, MiniAsSymbolExpr *expr);
bool minias_section_append(MiniAs *as, int section_index, const void *data, size_t size);
bool minias_section_append_zero(MiniAs *as, int section_index, size_t size);
void minias_set_error(MiniAs *as, const char *format, ...);
char *minias_strdup(const char *text);
char *minias_trim(char *text);

bool minias_riscv_measure(const char *op,
                          const char *args,
                          uint32_t *size,
                          char *reason,
                          size_t reason_size);
bool minias_riscv_encode(MiniAs *as, const MiniAsStmt *stmt);

#endif
