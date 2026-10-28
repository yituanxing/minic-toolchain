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
#define MINIAS_SECTION_COMMON (-3) /* semantic ELF SHN_COMMON */
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
    bool mutable_absolute_set; /* GNU .set reassignment, unlike .equ */
    bool explicit_local_binding; /* .local before .comm denotes real BSS */
    size_t alias_target_index;
    int64_t alias_addend;
    size_t alias_line;
    /* Location anchors in the parsed fragment stream. Repeated labels and
     * macro expansions must not use source-line numbers as identities. */
    bool has_fragment_anchor;
    size_t fragment_anchor;
    bool has_size_end_anchor;
    size_t size_end_anchor;
    uint64_t size_end_value;
} MiniAsSymbol;

typedef enum MiniAsStmtKind {
    MINIAS_STMT_INSN,
    MINIAS_STMT_ALIGN,
    MINIAS_STMT_DATA
} MiniAsStmtKind;

typedef enum MiniAsFixupKind {
    MINIAS_FIXUP_NONE = 0,
    MINIAS_FIXUP_ABS,
    MINIAS_FIXUP_PREL32, /* common S+A-P .long data expression */
    MINIAS_FIXUP_PREL64, /* common S+A-P .quad ELF64 PC-relative expression */
    MINIAS_FIXUP_ADD,
    MINIAS_FIXUP_SUB,
    MINIAS_FIXUP_RV_CALL_PLT,
    MINIAS_FIXUP_RV_PCREL_HI20,
    MINIAS_FIXUP_RV_GOT_HI20,
    MINIAS_FIXUP_RV_PCREL_LO12_I,
    MINIAS_FIXUP_RV_PCREL_LO12_S,
    MINIAS_FIXUP_RV_JAL,
    MINIAS_FIXUP_A64_CALL26,
    MINIAS_FIXUP_A64_JUMP26,
    MINIAS_FIXUP_A64_CONDBR19,
    MINIAS_FIXUP_A64_TSTBR14,
    MINIAS_FIXUP_A64_ADR_LO21,
    MINIAS_FIXUP_A64_ADRP_HI21,
    MINIAS_FIXUP_A64_ADR_GOT_PAGE,
    MINIAS_FIXUP_A64_LD64_GOT_LO12,
    MINIAS_FIXUP_A64_ADD_LO12,
    MINIAS_FIXUP_A64_LDST8_LO12,
    MINIAS_FIXUP_A64_LDST16_LO12,
    MINIAS_FIXUP_A64_LDST32_LO12,
    MINIAS_FIXUP_A64_LDST64_LO12,
    /* Semantic MOVW UABS relocations. Target owns numeric ELF IDs. */
    MINIAS_FIXUP_A64_MOVW_G0,
    MINIAS_FIXUP_A64_MOVW_G0_NC,
    MINIAS_FIXUP_A64_MOVW_G1,
    MINIAS_FIXUP_A64_MOVW_G1_NC,
    MINIAS_FIXUP_A64_MOVW_G2,
    MINIAS_FIXUP_A64_MOVW_G2_NC,
    MINIAS_FIXUP_A64_MOVW_G3,
    MINIAS_FIXUP_A64_MOVW_SG0,
    MINIAS_FIXUP_A64_MOVW_SG1,
    MINIAS_FIXUP_A64_MOVW_SG2,
    MINIAS_FIXUP_X64_PC32,
    MINIAS_FIXUP_X64_PLT32,
    MINIAS_FIXUP_X64_ABS32S,
    MINIAS_FIXUP_X64_ABS64,
    /* ELF numeric relocation types are target-owned; Common stores only
     * semantic fixup identities and SymbolExpr. */
    MINIAS_FIXUP_LARCH_PCALA_HI20,
    MINIAS_FIXUP_LARCH_PCALA_LO12,
    MINIAS_FIXUP_LARCH_B16,
    MINIAS_FIXUP_LARCH_B21,
    MINIAS_FIXUP_LARCH_B26
} MiniAsFixupKind;

/* A symbolic machine field adjustment, still independent of ELF's numeric
 * relocation encoding until final object materialization. */
typedef struct MiniAsFixup {
    int section;
    uint64_t offset;
    MiniAsFixupKind kind;
    unsigned width;
    size_t symbol_index;
    int64_t addend;
} MiniAsFixup;

typedef struct MiniAsSymbolExpr {
    char name[256];
    int64_t addend;
} MiniAsSymbolExpr;

/* MCInst stores target-parsed instruction operands, decoupled from source
 * text. Targets may migrate to it incrementally without changing the
 * common section/symbol/fragment lifecycle. */
typedef enum MiniAsMCOperandKind {
    MINIAS_MC_REGISTER,
    MINIAS_MC_IMMEDIATE,
    MINIAS_MC_SYMBOL,
    MINIAS_MC_MEMORY
} MiniAsMCOperandKind;
typedef enum MiniAsMCRegisterClass {
    MINIAS_MC_REG_GPR = 0,
    MINIAS_MC_REG_FPR,
    MINIAS_MC_REG_VECTOR
} MiniAsMCRegisterClass;
/* Target-neutral typed base/index/scale/displacement addressing.
 * Symbol expression is optional; relocation details remain target-owned.
 * Store a pointer only for memory operands, not in every integer operand. */
typedef struct MiniAsMCMemory {
    bool has_base;
    bool pc_relative; /* target-specific PC-relative encoding, not implicit ABS */
    unsigned address_space; /* optional target-owned segment/space selector */
    unsigned base;
    bool has_index;
    unsigned index;
    unsigned scale;
    int64_t displacement;
    MiniAsSymbolExpr *symbol;
} MiniAsMCMemory;

typedef struct MiniAsMCOperand {
    MiniAsMCOperandKind kind;
    unsigned reg;
    MiniAsMCRegisterClass reg_class;
    bool register_64;
    uint64_t immediate;
    /* Symbol names are allocated only for symbol operands. Integer-heavy
     * source files must not pay 256 bytes per register/immediate operand. */
    union {
        MiniAsSymbolExpr *symbol;
        MiniAsMCMemory *memory;
    };
} MiniAsMCOperand;
typedef struct MiniAsMCInst {
    unsigned opcode;
    uint32_t encoding; /* target-owned base machine-bit template */
    unsigned count;
    /* Allocate exactly the number of operands required by the target.
     * RV64 vector ops may have six; future targets need not resize MCInst. */
    MiniAsMCOperand operands[];
} MiniAsMCInst;

typedef struct MiniAsConditional {
    bool parent_active;
    bool condition_true;
    bool else_seen;
} MiniAsConditional;

typedef struct MiniAsMacroParam {
    char *name;
    char *default_value;
    bool required;
    bool variadic; /* GNU final :vararg captures remaining arguments */
} MiniAsMacroParam;

/* Target-requested GNU literal pool entry. Common owns Section/Layout
 * fragments and ABS32/ABS64 ELF relocations; Target owns LDR encoding.
 * Queued references are deduplicated only inside the same pending pool. */
typedef struct MiniAsLiteral {
    MiniAsSymbolExpr expression;
    char label[64];
    int section;
    uint32_t subsection;
    unsigned width;
    bool emitted;
} MiniAsLiteral;

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
    /* GNU alignment fragment semantics, shared by all backends. */
    bool align_has_fill;
    unsigned char align_fill;
    bool align_has_max_skip;
    uint64_t align_max_skip;
    /* Effective .option pic/nopic at the precise instruction site. */
    /* Opaque target state captured at the exact instruction site. */
    uint32_t target_options;
    /* Common variable-size data fragment resolved after symbol layout. */
    bool deferred_data_size;
    MiniAsMCInst *mcinst; /* allocated by target parser, freed by core */
} MiniAsStmt;

/* Common deferred DWARF .debug_frame CFI fragments. Target psABI owns register
 * identities and alignment; Common owns symbolic anchors and serialization. */
typedef struct MiniAsCfiOp {
    size_t symbol_index;
    unsigned kind;
    unsigned reg;
    int64_t value;
} MiniAsCfiOp;

typedef struct MiniAsCfiProc {
    size_t begin_symbol;
    size_t end_symbol;
    size_t first_op;
    size_t op_count;
    unsigned state_depth;
} MiniAsCfiProc;

typedef struct MiniAsDebugLoc {
    size_t symbol_index;
    unsigned file;
    unsigned line;
    unsigned column;
    unsigned flags;
    unsigned discriminator;
    unsigned isa;
    bool is_stmt;
} MiniAsDebugLoc;

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
    MiniAsFixup *fixups;
    size_t fixup_count;
    size_t fixup_capacity;
    MiniAsLiteral *literals;
    size_t literal_count;
    size_t literal_capacity;
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
    bool altmacro_active; /* GNU %absolute-expression positional arguments */
    bool conditional_active;
    /* GNU GAS's quoted-string-aware, multi-line C comment lexical state. */
    bool c_comment_active;
    bool layout_sensitive; /* conservative full-reparse fallback */
    bool has_deferred_data; /* forward-symbol data-size fragments */
    bool has_mutable_absolute_sets; /* snapshot mutable .set in data */
    bool expr_dot_override; /* explicit fragment-local dot while evaluating */
    uint64_t expr_dot_value;
    uint32_t target_options;
    /* Opaque per-assembly backend parser state, owned and freed by Target. */
    void *target_state;
    uint32_t target_option_stack[64];
    size_t target_option_depth;
    bool cfi_active;
    bool cfi_signal_frame;
    bool cfi_debug_frame;
    MiniAsCfiProc *cfi_procs;
    size_t cfi_proc_count;
    size_t cfi_proc_capacity;
    MiniAsCfiOp *cfi_ops;
    size_t cfi_op_count;
    size_t cfi_op_capacity;
    size_t cfi_anchor_counter;
    char **debug_files;
    size_t debug_file_capacity;
    size_t debug_file_max;
    MiniAsDebugLoc *debug_locs;
    size_t debug_loc_count;
    size_t debug_loc_capacity;
    bool debug_is_stmt;
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
bool minias_cfi_directive(MiniAs *as, const char *op,
                          const char *args, size_t line);
bool minias_cfi_emit(MiniAs *as);
bool minias_anchor_here(MiniAs *as, size_t *symbol_index);
bool minias_debug_directive(MiniAs *as,const char *op,
                            const char *args,size_t line);
bool minias_debug_emit(MiniAs *as);
bool minias_write_elf32(MiniAs *as, const char *path);

int minias_find_section(const MiniAs *as, const char *name);
int minias_ensure_section(MiniAs *as, const char *name, uint32_t type, uint64_t flags, uint64_t align);
MiniAsSymbol *minias_get_symbol(MiniAs *as, const char *name, bool create);
bool minias_queue_literal(MiniAs *as,const MiniAsSymbolExpr *expression,
                          unsigned width,MiniAsSymbolExpr *pool_label);
bool minias_add_fixup(MiniAs *as,
                      int section,
                      uint64_t offset,
                      MiniAsFixupKind kind,
                      unsigned width,
                      const char *symbol_name,
                      int64_t addend);
/* GNU/C-style binary, decimal, octal, hex and U/L/ULL literal scanner.
 * Exposes the consumed end pointer to both Common and Target grammars. */
bool minias_parse_number_literal(const char *text, char **end, uint64_t *value);
bool minias_eval_u64_expression(const char *text, uint64_t *value);
bool minias_eval_i64_expression(const char *text, int64_t *value);
/* Shared absolute expression resolver with Common-defined .set/.equ and
 * location-counter semantics. Targets use it only for ISA field constraints;
 * section/layout/fixup emission stays in Common. */
bool minias_parse_absolute_data_bits(MiniAs *as,const char *text,
                                     uint64_t *value);
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

typedef struct MiniAsObjectAttribute {
    const char *name;
    uint32_t type;
    const unsigned char *data;
    size_t size;
} MiniAsObjectAttribute;

struct MiniAsAssemblyConfig;
typedef struct MiniAsTargetOps {
    char comment_prefix;
    uint16_t elf_machine;
    /* Optional: NULL means all directives go to the common grammar. */
    bool (*parse_target_directive)(MiniAs *, const char *, const char *, size_t, bool *);
    /* Per-target operand/alias registry lifecycle; Common never inspects it. */
    void (*destroy_target_state)(MiniAs *);
    bool (*validate_common_directive)(MiniAs *, const char *, size_t);
    /* Optional GNU .if target operand dialect. Common owns the
     * conditional stack and arithmetic, Target only interprets native
     * register-token comparisons that are not integer expressions. */
    bool (*evaluate_target_if)(const char *, bool *);
    /* Optional: NULL emits no target-specific attributes section. */
    void (*object_attributes)(const MiniAs *, unsigned char, MiniAsObjectAttribute *);
    const char *name;
    /* Each target owns ISA/ABI/ELF profile validation; the CLI is target-neutral. */
    bool (*resolve_profile)(const char *, const char *,
                            struct MiniAsAssemblyConfig *, char *, size_t);
    /* Optional ISA/ABI gate runs before operand parsing and does not
     * independently decode source instruction operands. */
    bool (*validate_instruction)(const MiniAs *, const char *, const char *,
                                 char *, size_t);
    /* Mandatory target instruction path: parse once, measure its retained
     * typed operands, then encode. Core owns fragment address and growth. */
    bool (*parse_mcinst)(MiniAs *, MiniAsStmt *);
    bool (*measure_mcinst)(MiniAs *, const MiniAsStmt *, uint32_t *);
    bool (*encode)(MiniAs *, const MiniAsStmt *);
    /* Layout owns iteration; each target owns encoding alternatives and
     * target-specific range checks. Return the next encoding size. */
    bool (*relax_instruction)(MiniAs *, const MiniAsStmt *, uint32_t *);
    bool (*expanded_instruction_size)(const char *, uint32_t, uint32_t *);
    bool (*append_code_alignment)(MiniAs *, int, size_t);
    uint32_t (*fixup_relocation_type)(MiniAsFixupKind, unsigned);
    /* GNU named relocation spelling is ISA-owned; Common only places the
     * semantic fixup at the explicit section offset. */
    bool (*parse_named_relocation)(const char *, MiniAsFixupKind *, unsigned *);
    bool supports_literal_pools; /* .ltorg/.pool only for a checked target */
    bool (*validate_output)(MiniAs *);
    /* Target-owned DWARF CFI ABI (zero => not yet available). */
    unsigned cfi_code_align;
    int cfi_data_align;
    unsigned cfi_return_reg;
    unsigned cfi_stack_reg;
    unsigned cfi_address_size;
    unsigned cfi_negate_ra_opcode;
    /* GNU assembler directive dialect belongs to TargetInfo, not Common. */
    unsigned word_directive_width; /* zero: 4, x86 GNU .word: 2 */
    unsigned inst_directive_width; /* optional raw target ISA .inst datum */
    bool align_directive_bytes;   /* x86 GNU .align: bytes, RISC-V/A64: power 2 */
    unsigned common_default_alignment_cap; /* 0: require explicit .comm alignment */
    MiniAsFixupKind dot_relative_fixup; /* zero or target PC-relative fixup */
} MiniAsTargetOps;

const MiniAsTargetOps *minias_target_riscv(void);
const MiniAsTargetOps *minias_target_aarch64(void);
const MiniAsTargetOps *minias_target_x86_64(void);
const MiniAsTargetOps *minias_target_loongarch64(void);
const MiniAsTargetOps *minias_find_target(const char *name);

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

bool minias_riscv_measure_mcinst(MiniAs *, const MiniAsStmt *, uint32_t *);
bool minias_riscv_parse_mcinst(MiniAs *as, MiniAsStmt *stmt);
bool minias_riscv_encode(MiniAs *as, const MiniAsStmt *stmt);

#endif
