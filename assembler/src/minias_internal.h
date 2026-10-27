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
#define MINIAS_SHT_NOTE 7U
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

#define MINIAS_SECTION_UNDEF (-1)
#define MINIAS_SECTION_ABS (-2)
#define MINIAS_MAX_ERROR 256U

typedef struct MiniAsSection {
    char *name;
    uint32_t type;
    uint64_t flags;
    uint64_t align;
    unsigned char *data;
    size_t size;
    size_t capacity;
    uint64_t layout_size;
} MiniAsSection;

typedef struct MiniAsSubsection {
    int section;
    uint32_t number;
    uint64_t size;
    uint64_t base;
} MiniAsSubsection;

typedef struct MiniAsSectionState {
    int section;
    uint32_t subsection;
} MiniAsSectionState;

typedef struct MiniAsSymbol {
    char *name;
    int section;
    uint64_t value;
    uint64_t size;
    uint8_t bind;
    uint8_t type;
    uint8_t visibility;
    uint32_t subsection;
    bool defined;
    bool alias_pending;
    size_t alias_target_index;
    int64_t alias_addend;
    size_t alias_line;
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

typedef struct MiniAsConditional {
    bool parent_active;
    bool condition_true;
    bool else_seen;
} MiniAsConditional;

typedef struct MiniAsMacroParam {
    char *name;
    char *default_value;
    bool required;
} MiniAsMacroParam;

typedef struct MiniAsMacro {
    char *name;
    MiniAsMacroParam *params;
    size_t param_count;
    char **body;
    size_t *body_lines;
    size_t body_count;
} MiniAsMacro;

typedef struct MiniAsStmt {
    MiniAsStmtKind kind;
    char *op;
    char *args;
    size_t line;
    int section;
    uint32_t subsection;
    uint64_t offset;
    uint32_t size;
    uint64_t align;
    /* Effective .option pic/nopic at the precise instruction site. */
    /* Opaque target state captured at the exact instruction site. */
    uint32_t target_options;
} MiniAsStmt;

typedef struct MiniAs {
    const struct MiniAsTargetOps *target;
    MiniAsSection *sections;
    size_t section_count;
    size_t section_capacity;
    MiniAsSymbol *symbols;
    size_t symbol_count;
    size_t symbol_capacity;
    size_t *symbol_slots;
    size_t symbol_slot_capacity;
    MiniAsStmt *stmts;
    size_t stmt_count;
    size_t stmt_capacity;
    MiniAsReloc *relocs;
    size_t reloc_count;
    size_t reloc_capacity;
    MiniAsSubsection *subsections;
    size_t subsection_count;
    size_t subsection_capacity;
    MiniAsSectionState *section_stack;
    size_t section_stack_count;
    size_t section_stack_capacity;
    size_t pcrel_anchor_counter;
    size_t expr_anchor_counter;
    size_t *numeric_label_counts;
    size_t numeric_label_capacity;
    MiniAsConditional *conditionals;
    size_t conditional_count;
    size_t conditional_capacity;
    MiniAsMacro *macros;
    size_t macro_count;
    size_t macro_capacity;
    size_t macro_expansion_counter;
    size_t macro_expansion_depth;
    bool conditional_active;
    uint32_t target_options;
    uint32_t target_option_stack[64];
    size_t target_option_depth;
    bool cfi_active;
    bool cfi_signal_frame;
    uint32_t elf_flags;
    bool isa_explicit;
    unsigned target_xlen;
    uint64_t isa_features;
    unsigned char target_attributes[512];
    size_t target_attributes_size;
    bool strict_rv64i; /* legacy strict directive gating, both XLENs */
    unsigned strict_i_xlen; /* 0=legacy, 32=RV32I, 64=RV64I */
    const size_t *forced_long_branch_ids;
    size_t forced_long_branch_count;
    int current_section;
    uint32_t current_subsection;
    int previous_section;
    uint32_t previous_subsection;
    char error[MINIAS_MAX_ERROR];
} MiniAs;

void minias_init(MiniAs *as);
void minias_destroy(MiniAs *as);
bool minias_parse_file(MiniAs *as, const char *path);
bool minias_emit_sections(MiniAs *as);
bool minias_write_elf64(MiniAs *as, const char *path);
bool minias_write_elf32(MiniAs *as, const char *path);

int minias_find_section(const MiniAs *as, const char *name);
int minias_ensure_section(MiniAs *as, const char *name, uint32_t type, uint64_t flags, uint64_t align);
MiniAsSymbol *minias_get_symbol(MiniAs *as, const char *name, bool create);
bool minias_add_relocation(MiniAs *as,
                           int section,
                           uint64_t offset,
                           uint32_t type,
                           const char *symbol_name,
                           int64_t addend);
bool minias_eval_u64_expression(const char *text, uint64_t *value);
bool minias_parse_symbol_addend(const char *text, MiniAsSymbolExpr *expr);
bool minias_decode_string_literals(const char *text,
                                   bool nul_terminate,
                                   unsigned char **data,
                                   size_t *size);
bool minias_section_append(MiniAs *as, int section_index, const void *data, size_t size);
bool minias_section_append_zero(MiniAs *as, int section_index, size_t size);
void minias_set_error(MiniAs *as, const char *format, ...);
char *minias_strdup(const char *text);
char *minias_trim(char *text);

typedef enum MiniAsRelocKind {
    MINIAS_RELOC_ABS, MINIAS_RELOC_ADD, MINIAS_RELOC_SUB
} MiniAsRelocKind;

typedef struct MiniAsObjectAttribute {
    const char *name;
    uint32_t type;
    const unsigned char *data;
    size_t size;
} MiniAsObjectAttribute;

typedef struct MiniAsTargetOps {
    char comment_prefix;
    uint16_t elf_machine;
    bool (*parse_target_directive)(MiniAs *, const char *, const char *, size_t, bool *);
    void (*object_attributes)(const MiniAs *, unsigned char, MiniAsObjectAttribute *);
    const char *name;
    bool (*measure)(const MiniAs *, const char *, const char *, uint32_t *, char *, size_t);
    bool (*encode)(MiniAs *, const MiniAsStmt *);
    /* Layout owns iteration; each target owns encoding alternatives and
     * target-specific range checks. Return the next encoding size. */
    bool (*relax_instruction)(MiniAs *, const MiniAsStmt *, uint32_t *);
    bool (*expanded_instruction_size)(const char *, uint32_t, uint32_t *);
    bool (*append_code_alignment)(MiniAs *, int, size_t);
    uint32_t (*relocation_type)(unsigned, MiniAsRelocKind);
    bool (*validate_output)(MiniAs *);
} MiniAsTargetOps;

const MiniAsTargetOps *minias_target_riscv(void);
const MiniAsTargetOps *minias_target_aarch64(void);

typedef struct MiniAsAssemblyConfig {
    const MiniAsTargetOps *target;
    bool elf32;
    uint32_t elf_flags;
    unsigned strict_i_xlen;
    bool isa_explicit;
    uint64_t isa_features;
    unsigned char target_attributes[512];
    size_t target_attributes_size;
} MiniAsAssemblyConfig;
int minias_assemble_config(const char *, const char *,
                           const MiniAsAssemblyConfig *, FILE *);

/* Internal multi-target driver (legacy public API remains RISC-V). */
int minias_assemble_with_target(const char *, const char *,
                                const MiniAsTargetOps *, bool, uint32_t,
                                unsigned, FILE *);

bool minias_riscv_measure(unsigned strict_i_xlen,
                          const char *op,
                          const char *args,
                          uint32_t *size,
                          char *reason,
                          size_t reason_size);
bool minias_riscv_encode(MiniAs *as, const MiniAsStmt *stmt);

#endif
