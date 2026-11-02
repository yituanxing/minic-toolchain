#ifndef MINIAS_TARGET_API_H
#define MINIAS_TARGET_API_H

/* The only Target-visible MC/ABI types and phase-limited services. Targets
 * cannot include or dereference the Common assembly-session owner. */
#include <stdbool.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

#define MINIAS_SHF_WRITE 1ULL
#define MINIAS_SHF_ALLOC 2ULL
#define MINIAS_SHF_EXECINSTR 4ULL
#define MINIAS_SHF_MERGE 16ULL
#define MINIAS_SHF_STRINGS 32ULL
#define MINIAS_SHF_LINK_ORDER 128ULL
#define MINIAS_SHF_TLS 1024ULL
#define MINIAS_SHF_GROUP 512ULL
#define MINIAS_SHF_GNU_RETAIN (1ULL << 21)
#define MINIAS_GRP_COMDAT 1U
#define MINIAS_SHT_PROGBITS 1U
#define MINIAS_SHT_NOTE 7U
#define MINIAS_SHT_INIT_ARRAY 14U
#define MINIAS_SHT_FINI_ARRAY 15U
#define MINIAS_SHT_PREINIT_ARRAY 16U
#define MINIAS_SHT_NOBITS 8U

#define MINIAS_STB_LOCAL 0U
#define MINIAS_STB_GLOBAL 1U
#define MINIAS_STB_WEAK 2U
#define MINIAS_STB_GNU_UNIQUE 10U
#define MINIAS_STT_NOTYPE 0U
#define MINIAS_STT_OBJECT 1U
#define MINIAS_STT_FUNC 2U
#define MINIAS_STT_FILE 4U
#define MINIAS_STT_TLS 6U
#define MINIAS_STT_GNU_IFUNC 10U
#define MINIAS_STV_DEFAULT 0U
#define MINIAS_STV_INTERNAL 1U
#define MINIAS_STV_HIDDEN 2U
#define MINIAS_STV_PROTECTED 3U

#define MINIAS_SECTION_UNDEF (-1)
#define MINIAS_SECTION_ABS (-2)
#define MINIAS_SECTION_COMMON (-3) /* semantic ELF SHN_COMMON */
#define MINIAS_MAX_ERROR 256U

/* Section and Symbol are Common-owned mutable records, defined only in
 * minias_internal.h. Targets receive phase-scoped value projections below,
 * never their allocation capacities, mutable names or backing buffers. */
typedef struct MiniAsSymbolView {
    const char *name;
    int section;
    uint32_t subsection;
    uint64_t value;
    uint8_t bind;
    uint8_t type;
    bool defined;
    bool alias_pending;
    bool mutable_absolute_set;
    bool has_alias_dependency;
} MiniAsSymbolView;

/* The sole persistent Fragment discriminator.  Both Target-facing emission
 * and Common layout consume this semantic kind, not directive spelling or
 * a second coarse statement class. */
typedef enum MiniAsFragmentKind {
    MINIAS_FRAGMENT_INSTRUCTION,
    MINIAS_FRAGMENT_ALIGNMENT,
    MINIAS_FRAGMENT_ORIGIN,
    MINIAS_FRAGMENT_FIXED_DATA,
    MINIAS_FRAGMENT_FILL,
    MINIAS_FRAGMENT_SPACE,
    MINIAS_FRAGMENT_BYTES
} MiniAsFragmentKind;

/* A typed position constraint, comparable to LLVM MCOrgFragment. */
typedef enum MiniAsOrgMode {
    MINIAS_ORG_SOURCE = 0,
    MINIAS_ORG_ABSOLUTE,
    MINIAS_ORG_DOT_ADDEND,
    MINIAS_ORG_SYMBOL_ADDEND,
    MINIAS_ORG_TYPED_EXPR
} MiniAsOrgMode;

/* Persistent, owned expression trees for layout-sensitive GNU directives.
 * Each tree binds symbols to stable SymbolIDs while the source is parsed;
 * layout evaluates it with a supplied Fragment address, never a mutable
 * parser's global current_section or a second copy of the source text. */
typedef struct MiniAsDeferredExpr MiniAsDeferredExpr;

/* Semantic statement identity is independent of its changing Fragment
 * ordinal. A SiteID is interned from the complete source/expansion path by
 * an assembly-session SourceGraph; zero is the absent/root sentinel. */
typedef size_t MiniAsStableSiteId;
#define MINIAS_SITE_NONE ((MiniAsStableSiteId)0U)
typedef struct MiniAsSourceGraph MiniAsSourceGraph;
typedef struct MiniAsSourceContext MiniAsSourceContext;

/* GNU local .comm reserves BSS storage at the end of subsection zero,
 * after all ordinary source fragments regardless of declaration order.
 * Keep the original SymbolID and source site while deferring its allocation
 * until the complete source Fragment graph has been parsed. */
typedef struct MiniAsPendingLocalCommon {
    size_t symbol_index;
    uint64_t size;
    uint64_t alignment;
    size_t line;
    MiniAsStableSiteId source_site;
} MiniAsPendingLocalCommon;

/* Common fixup kinds describe generic byte-addressed data semantics.
 * Machine-specific kinds are opaque uint32_t values owned by the Target.
 * ELF relocation ID selection happens only after fragment layout. */
typedef uint32_t MiniAsFixupKind;
enum {
    MINIAS_FIXUP_NONE = 0,
    MINIAS_FIXUP_ABS,
    MINIAS_FIXUP_PREL32,
    MINIAS_FIXUP_PREL64,
    MINIAS_FIXUP_ADD,
    MINIAS_FIXUP_SUB,
    MINIAS_FIXUP_SET,
    /* Compact DW_CFA_advance_loc: relocation changes only opcode low six
     * bits, preserving DWARF 0x40 major opcode bits. */
    MINIAS_FIXUP_CFI_ADD6,
    MINIAS_FIXUP_CFI_SUB6,
    MINIAS_FIXUP_CFI_SET6,
    /* Zero-width relocation records operate on the full ULEB128 encoded
     * field.  The 0 width is intentional: its extent is determined by the
     * continuation bits, and linker relaxation must not change that width. */
    MINIAS_FIXUP_LEB_ADD,
    MINIAS_FIXUP_LEB_SUB,
    MINIAS_FIXUP_LEB_SET
};

/* A symbolic machine field adjustment, still independent of ELF's numeric
 * relocation encoding until final object materialization. */
/* Every machine fixup has exactly one owner. Fragment fixups retain an
 * intra-fragment offset until final layout; .reloc and synthesized DWARF
 * relocations are intentionally section-relative, never guessed as an
 * instruction's fixup. */
typedef enum MiniAsFixupOrigin {
    MINIAS_FIXUP_ORIGIN_FRAGMENT = 0,
    MINIAS_FIXUP_ORIGIN_EXPLICIT_SECTION,
    MINIAS_FIXUP_ORIGIN_SYNTHETIC_SECTION
} MiniAsFixupOrigin;

typedef struct MiniAsFixup {
    int section;
    uint64_t offset; /* section-relative ONLY for section-owned fixups */
    MiniAsFixupOrigin origin;
    size_t fragment_id; /* stable parsed statement ordinal */
    uint32_t fragment_offset; /* relative to final fragment start */
    MiniAsFixupKind kind;
    unsigned width;
    size_t symbol_index;
    int64_t addend;
    bool prefer_section_symbol; /* target opts in to GNU section binding */
} MiniAsFixup;

typedef struct MiniAsSymbolExpr {
    char name[256];
    int64_t addend;
} MiniAsSymbolExpr;

/* Parse-time symbol identity is separate from an ELF Object SymbolID.
 * NameId intern never inserts a MiniAsSymbol or changes its encounter order.
 * NameId is stable for one MiniAs lifetime and invalid across source replay. */
typedef size_t MiniAsSymbolNameId;
typedef struct MiniAsSymbolRef {
    MiniAsSymbolNameId name_id;
    int64_t addend;
} MiniAsSymbolRef;

typedef struct MiniAsInternedSymbolName {
    char *name;
} MiniAsInternedSymbolName;

/* A symbol subtraction is only absolute after BOTH final section-local
 * fragment anchors have been laid out. One shared layout owner computes
 * this difference, never a target-specific constant-folding shortcut. */
typedef struct MiniAsSymbolDifference {
    MiniAsSymbolExpr positive;
    MiniAsSymbolExpr negative;
} MiniAsSymbolDifference;

typedef struct MiniAsSymbolDifferenceRef {
    MiniAsSymbolRef positive;
    MiniAsSymbolRef negative;
} MiniAsSymbolDifferenceRef;

/* MCInst stores target-parsed instruction operands, decoupled from source
 * text. Targets may migrate to it incrementally without changing the
 * common section/symbol/fragment lifecycle. */
typedef enum MiniAsMCOperandKind {
    MINIAS_MC_REGISTER,
    MINIAS_MC_IMMEDIATE,
    MINIAS_MC_MEMORY,
    MINIAS_MC_BOUND_SYMBOL,
    MINIAS_MC_BOUND_SYMBOL_DIFFERENCE
} MiniAsMCOperandKind;
typedef enum MiniAsMCRegisterClass {
    MINIAS_MC_REG_GPR = 0,
    MINIAS_MC_REG_FPR,
    MINIAS_MC_REG_VECTOR
} MiniAsMCRegisterClass;
/* Memory is a semantic operand category, but its addressing representation is
 * target-owned.  Common must not impose x86 base/index/scale, segment, or
 * displacement rules on other ISAs.  The owning Target installs one payload
 * destructor per instruction; the pointer is otherwise opaque to Common. */

typedef struct MiniAsMCOperand {
    MiniAsMCOperandKind kind;
    unsigned reg;
    MiniAsMCRegisterClass reg_class;
    /* Target-private register-bank identity; zero means unspecified.
     * Common owns storage/lifetime and never interprets its ISA meaning. */
    uint16_t target_reg_bank;
    bool register_64;
    uint64_t immediate;
    /* Retained symbols are short NameId-based references. Parse-only name
     * DTOs never survive MCInst construction or enter the ELF symbol list. */
    union {
        MiniAsSymbolRef *bound_symbol;
        MiniAsSymbolDifferenceRef *bound_difference;
        void *memory;
    };
} MiniAsMCOperand;
typedef struct MiniAsMCInst {
    unsigned opcode;
    uint32_t encoding; /* target-owned base machine-bit template */
    /* Opaque Target form and retained lowering/encoding plan.  Unlike a
     * machine word these identify semantic choices across layout passes. */
    uint32_t target_form;
    void *target_payload;
    void (*dispose_target_payload)(void *);
    unsigned count;
    /* Every Target-owned MC_MEMORY heap payload has a mandatory destructor;
     * Common never guesses whether a private operand has nested storage. */
    void (*dispose_memory)(void *);
    /* Lifetime capacity is independent from successfully parsed operands.
     * A partially parsed instruction still owns initialized slots. */
    unsigned capacity;
    /* Allocate exactly the number of operands required by the target.
     * RV64 vector ops may have six; future targets need not resize MCInst. */
    MiniAsMCOperand operands[];
} MiniAsMCInst;

MiniAsMCInst *minias_mcinst_alloc(unsigned capacity);
void minias_mcinst_destroy(MiniAsMCInst *mcinst);

/* Parse owns the writable MCInst above. Measure, MCLayout and Emission
 * receive only these by-value, phase-scoped operand snapshots. In particular
 * neither a const MCInst* (whose nested pointers are writable) nor the
 * lifetime/destructor fields escape into those three callbacks. Target-private
 * memory and lowering plans are borrowed as const void* and remain owned by
 * the single retained MCInst. No second persistent instruction graph exists. */
typedef struct MiniAsMCOperandView {
    MiniAsMCOperandKind kind;
    unsigned reg;
    MiniAsMCRegisterClass reg_class;
    uint16_t target_reg_bank;
    bool register_64;
    uint64_t immediate;
    union {
        const MiniAsSymbolRef *bound_symbol;
        const MiniAsSymbolDifferenceRef *bound_difference;
        const void *memory;
    };
} MiniAsMCOperandView;

typedef struct MiniAsMCInstView {
    unsigned opcode;
    uint32_t encoding;
    uint32_t target_form;
    const void *target_payload;
    unsigned count;
    const MiniAsMCOperandView *operands;
} MiniAsMCInstView;

/* Target-requested GNU literal pool entry. Common owns Section/Layout
 * fragments and ABS32/ABS64 ELF relocations; Target owns LDR encoding.
 * Queued references are deduplicated only inside the same pending pool. */
typedef struct MiniAsLiteral {
    MiniAsSymbolRef expression;
    char label[64];
    int section;
    uint32_t subsection;
    unsigned width;
    bool emitted;
} MiniAsLiteral;

/* Common never interprets these bits: only the emitting Target knows their
 * meaning. Preserve the exact directive-selected mode at the parse site,
 * rather than reading mutable TargetContext during later relaxation. */
typedef struct MiniAsTargetSiteState {
    uint32_t mode_bits;
    /* Target-private parsed directive provenance. Common snapshots it once
     * and never interprets the bits during layout or output finalization. */
    uint32_t provenance_bits;
} MiniAsTargetSiteState;

typedef struct MiniAsInstructionView {
    MiniAsStableSiteId source_site;
    const char *op;
    const char *args;
    size_t line;
    int section;
    uint32_t subsection;
    uint64_t offset;
    uint32_t size;
    MiniAsTargetSiteState site;
    const MiniAsMCInstView *inst;
} MiniAsInstructionView;

/* MCExpr owns semantic operands, not source spelling. Compile each fixed-
 * width data operand once at its lexical site; the output pass resolves
 * relocatable references using the final Fragment/Symbol layout. */
typedef enum MiniAsMCExprKind {
    MINIAS_MC_EXPR_CONSTANT,
    MINIAS_MC_EXPR_SYMBOL,
    MINIAS_MC_EXPR_SYMBOL_MINUS_DOT,
    MINIAS_MC_EXPR_DOT_MINUS_SYMBOL, /* stable current fragment site - label */
    MINIAS_MC_EXPR_SYMBOL_MINUS_DOT_DIV,
    MINIAS_MC_EXPR_DIFFERENCE,
    MINIAS_MC_EXPR_DIFFERENCE_DIV
} MiniAsMCExprKind;

typedef struct MiniAsMCExpr {
    MiniAsMCExprKind kind;
    uint64_t constant;
    uint64_t divisor;
    int64_t lhs_addend;
    int64_t rhs_addend;
    /* Stable symbol table indices, never heap copies of symbol names.
     * Symbols may move during parsing, but ordinals are immutable. '.'
     * is a Fragment anchor, not an interned ELF symbol. */
    bool has_lhs;
    bool has_rhs;
    size_t lhs_symbol_index;
    size_t rhs_symbol_index;
    bool logical_shift;
} MiniAsMCExpr;

typedef struct MiniAsMCData {
    unsigned width;
    size_t count;
    MiniAsMCExpr *fields;
} MiniAsMCData;

/* One discriminated source Fragment payload. The kind in MiniAsStmt selects
 * exactly one active member, including on a failed partial parse. Source
 * spelling, site identity and final placement remain shared metadata. */
typedef struct MiniAsInstructionFragment {
    MiniAsMCInst *inst;
} MiniAsInstructionFragment;

typedef struct MiniAsAlignmentFragment {
    uint64_t alignment;
    bool has_fill;
    unsigned char fill;
    bool has_max_skip;
    uint64_t max_skip;
} MiniAsAlignmentFragment;

typedef struct MiniAsOriginFragment {
    MiniAsOrgMode mode;
    MiniAsDeferredExpr *expr;
    uint64_t target;
    size_t symbol_index;
    int64_t addend;
    bool has_fill;
    unsigned char fill;
} MiniAsOriginFragment;

typedef struct MiniAsFixedDataFragment {
    MiniAsMCData *data;
} MiniAsFixedDataFragment;

/* FILL and SPACE share the same retained GNU expression program but the
 * FragmentKind determines which value/width combinations are legal. */
typedef struct MiniAsVariableDataFragment {
    bool deferred_size;
    bool resolved;
    uint64_t repeat;
    uint64_t value;
    unsigned element_width;
    MiniAsDeferredExpr *exprs[3];
    unsigned expr_count;
} MiniAsVariableDataFragment;

/* Raw strings/incbin and fixed LEB snapshots share immutable bytes. Only a
 * deferred symbolic LEB carries an expression and changes its byte length. */
typedef struct MiniAsBytesFragment {
    bool deferred_size;
    unsigned char *data;
    size_t data_size;
    unsigned leb_mode;
    MiniAsDeferredExpr *leb_expr;
} MiniAsBytesFragment;

typedef union MiniAsFragmentPayload {
    MiniAsInstructionFragment instruction;
    MiniAsAlignmentFragment alignment;
    MiniAsOriginFragment origin;
    MiniAsFixedDataFragment fixed_data;
    MiniAsVariableDataFragment variable;
    MiniAsBytesFragment bytes;
} MiniAsFragmentPayload;

typedef struct MiniAsStmt {
    MiniAsFragmentKind fragment_kind;
    MiniAsStableSiteId source_site;
    char *op;
    char *args;
    size_t line;
    int section;
    uint32_t subsection;
    uint64_t offset;
    uint32_t size;
    /* Immutable parse-site state: meaningful bits are defined by Target. */
    struct MiniAsTargetSiteState site;
    MiniAsFragmentPayload payload;
} MiniAsStmt;

/* Ephemeral value projections. No pointer to writable Common Fragments,
 * Sections or Symbol records crosses a Target stage boundary. A borrowed
 * source name and finalized bytes remain const for the duration of the
 * callback. Each query observes the current MCLayout/Emission generation. */
typedef struct MiniAsFragmentView {
    MiniAsFragmentKind kind;
    const char *op;
    size_t line;
    int section;
    uint64_t offset;
    uint32_t size;
    MiniAsTargetSiteState site;
    bool has_instruction;
    unsigned instruction_opcode;
    bool alignment_has_fill;
} MiniAsFragmentView;

typedef struct MiniAsSectionView {
    const char *name;
    uint32_t type;
    uint64_t flags;
    uint64_t align;
    const unsigned char *data;
    size_t size;
} MiniAsSectionView;


/* Target object/ABI policies only need the finalized object class and the
 * Target's private state. Passing this value instead of the whole MiniAs
 * session prevents read-only policy hooks from depending on Common Symbols,
 * Sections, Fragment arrays or SourceContext internals. The snapshot is
 * reconstructed at each call because Target state can change during parse. */
typedef struct MiniAsTargetABIView {
    const void *target_state;
    bool elf32;
} MiniAsTargetABIView;

/* Complete retained source Fragments after parsing/EOF pools, BEFORE any
 * MCLayout relaxation. A Target may derive its own section-wide policies
 * once, without receiving writable Common Section/Fragment storage. */
typedef struct MiniAsPreLayoutView MiniAsPreLayoutView;
size_t minias_pre_layout_fragment_count(const MiniAsPreLayoutView *);
size_t minias_pre_layout_section_count(const MiniAsPreLayoutView *);
void *minias_pre_layout_target_state(const MiniAsPreLayoutView *);
bool minias_pre_layout_fragment(const MiniAsPreLayoutView *,size_t,
                                MiniAsFragmentView *);

/* Instruction width is a Target-owned property of retained MCInst and its
 * parse-site ISA/mode. Measuring cannot mutate Common source, layout,
 * symbols or fixups. Later encoding still gets its transactional emitter. */
typedef struct MiniAsMeasureView {
    const void *target_state;
} MiniAsMeasureView;
typedef struct MiniAsMeasureErrorSink MiniAsMeasureErrorSink;
void minias_measure_error(MiniAsMeasureErrorSink *, const char *, ...);

/* The ISA parser constructs a retained MCInst, but symbol-name interning,
 * current-symbol lookups, absolute-expression evaluation and literal-pool
 * requests are Common capabilities. Keep its owner completely opaque to
 * Targets: there is exactly one parser callback per ISA, not a second grammar
 * or a target-accessible writable MiniAs graph. */
typedef struct MiniAsParseContext MiniAsParseContext;
const void *minias_parse_target_state(const MiniAsParseContext *);
bool minias_parse_is_elf32(const MiniAsParseContext *);
bool minias_parse_has_error(const MiniAsParseContext *);
void minias_parse_error(MiniAsParseContext *, const char *, ...);
bool minias_parse_bind_symbol_ref(MiniAsParseContext *,
                                  const MiniAsSymbolExpr *, MiniAsSymbolRef *);
bool minias_parse_bind_symbol_difference_ref(
    MiniAsParseContext *, const MiniAsSymbolDifference *,
    MiniAsSymbolDifferenceRef *);
bool minias_parse_symbol_pair_same_defined_section(
    MiniAsParseContext *, MiniAsSymbolRef, MiniAsSymbolRef);
bool minias_parse_eval_absolute_data_bits(MiniAsParseContext *, const char *,
                                          uint64_t *);
bool minias_parse_evaluate_absolute_operand(MiniAsParseContext *, const char *,
                                            int64_t *);
bool minias_parse_eval_symbol_difference_absolute_tail(
    MiniAsParseContext *, const char *, MiniAsSymbolDifference *);
bool minias_parse_queue_literal(MiniAsParseContext *,
                                const MiniAsSymbolExpr *, unsigned,
                                MiniAsSymbolExpr *);

/* A borrowed, read-only view of the *final* MCLayout. Target output checks
 * cannot reach the mutable Common section/symbol/fixup graph through this
 * interface. Only the object-finalization sink below can request the two
 * currently needed ELF changes; Common remains their sole owner. */
typedef struct MiniAsPostEmitView MiniAsPostEmitView;
size_t minias_post_emit_fragment_count(const MiniAsPostEmitView *);
size_t minias_post_emit_section_count(const MiniAsPostEmitView *);
const void *minias_post_emit_target_state(const MiniAsPostEmitView *);
bool minias_post_emit_fragment(const MiniAsPostEmitView *,size_t,
                               MiniAsFragmentView *);
bool minias_post_emit_section(const MiniAsPostEmitView *,size_t,
                              MiniAsSectionView *);

typedef struct MiniAsPostEmitErrorSink MiniAsPostEmitErrorSink;
typedef struct MiniAsPostEmitObjectSink MiniAsPostEmitObjectSink;

void minias_post_emit_error(MiniAsPostEmitErrorSink *, const char *, ...);
size_t minias_post_emit_symbol_count(const MiniAsPostEmitObjectSink *);
bool minias_post_emit_define_mapping_symbol(MiniAsPostEmitObjectSink *,
                                            const char *, size_t, bool,
                                            int, uint64_t);
bool minias_post_emit_raise_section_alignment(MiniAsPostEmitObjectSink *,
                                              int, uint64_t);

/* Encoding is a Common-owned Fragment transaction. A Target may inspect
 * stable Symbol snapshots and private ISA state, then request bytes, Fixups
 * and private anchors; it cannot dereference or mutate the MiniAs session.
 * No compatibility callback taking MiniAs* is part of TargetOps. */
typedef struct MiniAsEncodeContext MiniAsEncodeContext;
bool minias_encode_ctx_emit(MiniAsEncodeContext *, const void *, size_t);
bool minias_encode_ctx_fixup_ref(MiniAsEncodeContext *, uint64_t,
                                MiniAsFixupKind, unsigned, MiniAsSymbolRef, bool);
bool minias_encode_ctx_fixup_abs0(MiniAsEncodeContext *, uint64_t,
                                 MiniAsFixupKind, unsigned);
bool minias_encode_ctx_anchor_ref(MiniAsEncodeContext *, const char *,
                                 MiniAsSymbolRef *);
bool minias_encode_ctx_elf_flags(MiniAsEncodeContext *, uint32_t);
void minias_encode_ctx_error(MiniAsEncodeContext *, const char *, ...);
bool minias_encode_ctx_symbol_lookup(MiniAsEncodeContext *,MiniAsSymbolRef,
                                    MiniAsSymbolView *);
bool minias_encode_ctx_resolve_difference(MiniAsEncodeContext *,
    const MiniAsSymbolDifferenceRef *, int64_t *);
bool minias_encode_ctx_resolve_difference_field(MiniAsEncodeContext *,
    const MiniAsSymbolDifferenceRef *, int, uint64_t, int64_t *, bool *,
    MiniAsSymbolRef *);
bool minias_encode_ctx_symbol_is_dot(MiniAsEncodeContext *, MiniAsSymbolRef);
const void *minias_encode_ctx_target_state(const MiniAsEncodeContext *);
bool minias_encode_ctx_is_elf32(const MiniAsEncodeContext *);
/* Executable ALIGN is a Target byte policy, not a general-purpose session
 * capability. The Target sees no MiniAs or mutable Fragment/Section array. */
typedef struct MiniAsCodeAlignSink MiniAsCodeAlignSink;
bool minias_code_align_emit(MiniAsCodeAlignSink *, const void *, size_t);
bool minias_code_align_add_elf_flags(MiniAsCodeAlignSink *, uint32_t);
void minias_code_align_error(MiniAsCodeAlignSink *, const char *, ...);
const void *minias_code_align_target_state(const MiniAsCodeAlignSink *);
bool minias_code_align_is_elf32(const MiniAsCodeAlignSink *);
bool minias_code_align_has_active_fragment(const MiniAsCodeAlignSink *);
/* Common owns source order, subsection identity and adjacency rules. */
bool minias_code_align_preceded_by_adjacent_data(const MiniAsCodeAlignSink *);

/* Pure shared lexical/number helpers have no mutable assembly session. */
bool minias_checked_add_i64(int64_t, int64_t, int64_t *);
bool minias_eval_u64_expression(const char *text, uint64_t *value);
bool minias_eval_i64_expression(const char *text, int64_t *value);
bool minias_parse_symbol_addend(const char *text, MiniAsSymbolExpr *expr);
bool minias_parse_symbol_difference(const char *,MiniAsSymbolDifference *);
bool minias_split_mc_operands(const char *,char *const [],const size_t [],
                              unsigned,unsigned *);
char *minias_strdup(const char *text);
char *minias_trim(char *text);

typedef struct MiniAsObjectAttribute {
    const char *name;
    uint32_t type;
    const unsigned char *data;
    size_t size;
} MiniAsObjectAttribute;

struct MiniAsAssemblyConfig;
/* Effective CFI ABI of a TargetSpec. Target can override only the fields
 * affected by its ISA/ABI profile (e.g. RV32 vs RV64), while Common owns
 * the CFI stream, stable location anchors and ELF object fields. */
typedef struct MiniAsCfiABI {
    unsigned code_align;
    int data_align;
    unsigned return_reg;
    unsigned stack_reg;
    unsigned address_size;
    unsigned eh_fde_alignment; /* zero: align each FDE to address size */
    unsigned eh_final_alignment; /* zero: use ordinary FDE alignment */
    unsigned negate_ra_opcode;
    unsigned initial_cfa_offset;
    bool initial_ra_saved;
} MiniAsCfiABI;

/* A literal pool owns ordinary MCFragments, but entry scheduling is an ISA
 * policy: AArch64 GNU GAS groups 32-bit entries before 64-bit entries. */
typedef enum MiniAsLiteralPoolOrder {
    MINIAS_LITERAL_POOL_FIRST_USE = 0,
    MINIAS_LITERAL_POOL_WIDTH_ASC
} MiniAsLiteralPoolOrder;

/* A target-selected, typed encoding alternative for one retained MCInst.
 * required_size is the smallest currently legal encoding after symbol/range
 * checks; terminal_size is the monotonic long alternative available when
 * an earlier branch forces this Fragment to grow.  Both are measured from
 * the retained instruction, never recovered from its mnemonic spelling.
 * The final encoding is determined uniquely by retained MCInst and size. */
typedef struct MiniAsTargetRelaxPlan {
    uint32_t current_size;
    uint32_t required_size;
    uint32_t terminal_size;
} MiniAsTargetRelaxPlan;

/* MCLayout alone owns the current Symbol/Fragment graph. Range planning
 * borrows its exact generation through an opaque, stack-scoped Common
 * service. Targets can query symbols and checked differences or report a
 * diagnostic, but cannot obtain a writable MiniAs or mutate layout. */
typedef struct MiniAsRelaxContext MiniAsRelaxContext;
bool minias_relax_symbol_ref_lookup(MiniAsRelaxContext *,MiniAsSymbolRef,
                                   MiniAsSymbolView *);
bool minias_relax_resolve_symbol_difference_ref(
    MiniAsRelaxContext *, const MiniAsSymbolDifferenceRef *, int64_t *);
void minias_relax_error(MiniAsRelaxContext *, const char *, ...);

/* Common classifies accepted data-producing source directives exactly once.
 * Targets may reject an ISA escape in executable sections using this typed
 * semantic category, never by copying an evolving list of GNU aliases. */
typedef enum MiniAsCommonDirectiveKind {
    MINIAS_COMMON_DIRECTIVE_OTHER = 0,
    MINIAS_COMMON_DIRECTIVE_RAW_DATA,
    MINIAS_COMMON_DIRECTIVE_ORG
} MiniAsCommonDirectiveKind;



/* A Target directive may mutate only its own state and ask Common for
 * narrow, read-only source/section queries. The complete assembly session
 * is intentionally unavailable through this opaque phase capability. */
typedef struct MiniAsDirectiveContext MiniAsDirectiveContext;
void *minias_directive_target_state(MiniAsDirectiveContext *);
bool minias_directive_section_executable(const MiniAsDirectiveContext *);
bool minias_directive_has_prior_instruction(const MiniAsDirectiveContext *);
bool minias_directive_parse_absolute_data_bits(MiniAsDirectiveContext *,
                                               const char *,uint64_t *);
void minias_directive_set_error(MiniAsDirectiveContext *,const char *,...);

/* Source-pass target state is built privately and published by Common only
 * after success. A failed construction may return a partial state for Common
 * to destroy, but may never install it in the assembly session. */
typedef struct MiniAsCfiMappingSink MiniAsCfiMappingSink;
bool minias_cfi_mapping_define(MiniAsCfiMappingSink *,const char *stem,
                               bool always_suffix,int section,uint64_t offset);

typedef struct MiniAsTargetOps {
    char comment_prefix;
    uint16_t elf_machine;
    /* Optional: NULL means all directives go to the common grammar. */
    bool (*parse_target_directive)(MiniAsDirectiveContext *,
                                   const char *, const char *, size_t, bool *);
    /* Fully target-owned ISA/ABI state lifecycle and per-fragment snapshot.
     * Site capture only reads Target state and the immutable source site;
     * Common owns the Fragment array and its placement. */
    bool (*init_target_state)(const struct MiniAsAssemblyConfig *,
                              void **,char *,size_t);
    bool (*configure_legacy_target)(struct MiniAsAssemblyConfig *, unsigned);
    void (*capture_site_state)(const void *target_state, const MiniAsStmt *,
                               MiniAsTargetSiteState *);
    void (*destroy_target_state)(void *);
    /* Optional one-shot analysis over the complete parsed SourceGraph.
     * Errors are written to the bounded Common diagnostic buffer; the
     * Target must not alter the borrowed Fragment graph. */
    bool (*prepare_layout)(const MiniAsPreLayoutView *, char *, size_t);
    bool (*validate_common_directive)(MiniAsDirectiveContext *,
                                      MiniAsCommonDirectiveKind,
                                      const char *, size_t);
    /* Optional GNU .if target operand dialect. Common owns the
     * conditional stack and arithmetic, Target only interprets native
     * register-token comparisons that are not integer expressions. */
    bool (*evaluate_target_if)(const char *, bool *);
    /* Optional: NULL emits no target-specific attributes section. */
    void (*object_attributes)(MiniAsTargetABIView, unsigned char,
                              MiniAsObjectAttribute *);
    const char *name;
    /* Each target owns ISA/ABI/ELF profile validation; the CLI is target-neutral. */
    bool (*resolve_profile)(const char *, const char *,
                            struct MiniAsAssemblyConfig *, char *, size_t);
    /* Mandatory target instruction path: parse once, measure its retained
     * typed operands, then encode. Core owns fragment address and growth. */
    bool (*parse_mcinst)(MiniAsParseContext *, MiniAsStmt *);
    bool (*measure_mcinst)(const MiniAsMeasureView *,
                           MiniAsMeasureErrorSink *,
                           const MiniAsInstructionView *, uint32_t *);
    bool (*encode)(MiniAsEncodeContext *, const MiniAsInstructionView *);
    /* One typed TargetRelaxPlan supplies both the range requirement and
     * the longest legal encoding. No Common mnemonic/argument reparse. */
    bool (*plan_instruction_relaxation)(MiniAsRelaxContext *,
                                        const MiniAsInstructionView *,
                                        MiniAsTargetRelaxPlan *);
    /* Keep the lexical ISA/mode snapshot of the ALIGN site: a Target may
     * need different NOP encodings for different source-selected CPU modes. */
    bool (*append_code_alignment)(MiniAsCodeAlignSink *,const MiniAsStmt *,size_t);
    /* Some targets interpret an explicit alignment fill as the canonical
     * executable NOP sequence (e.g. GNU x86 `balign 16,0x90`). Common
     * must not choose machine-code bytes or hardcode a target's fill value. */
    bool (*explicit_alignment_fill_is_code_nop)(uint8_t);
    /* Single ABI-aware semantic capability and relocation mapping. ELF32
     * i386 and ELF64 AMD64 are different object profiles despite sharing
     * one target parser; no generic fallback may re-admit a rejected kind. */
    bool (*supports_fixup)(MiniAsTargetABIView,MiniAsFixupKind,unsigned);
    uint32_t (*fixup_relocation_type)(MiniAsTargetABIView,MiniAsFixupKind,unsigned);
    /* GNU named relocation spelling is ISA-owned; Common only places the
     * semantic fixup at the explicit section offset. */
    bool (*parse_named_relocation)(MiniAsTargetABIView,const char *,
                                   MiniAsFixupKind *,unsigned *);
    bool supports_literal_pools; /* .ltorg/.pool only for a checked target */
    MiniAsLiteralPoolOrder literal_pool_order;
    /* Post-emission validation checks finalized section bytes/placements.
     * It may report errors, but must not introduce Object symbols, change
     * section alignment or otherwise materialize object output. */
    bool (*validate_output)(const MiniAsPostEmitView *,
                            MiniAsPostEmitErrorSink *);
    /* Post-emission Target object finalization runs AFTER validation and
     * BEFORE the ObjectWriter observes symbols, sections and relocations.
     * ISA mapping symbols and target-driven section alignment belong here,
     * not in an apparently read-only validation callback. NULL is a no-op. */
    bool (*finalize_output)(const MiniAsPostEmitView *,
                            MiniAsPostEmitErrorSink *,
                            MiniAsPostEmitObjectSink *);
    /* Target-advertised GNU implicit .eh_frame: never infer ISA from
     * numeric ELF e_machine inside Common CFI/layout dispatch. */
    bool cfi_implicit_eh_frame;
    /* GNU x86 ld canonicalizes defined local ABS data relocations, including
     * same-section references, to STT_SECTION + addend. Target policy,
     * Common ELF ownership. */
    bool fold_local_abs_relocation_to_section;
    /* Some targets (including GNU i386/AMD64) canonicalize an ordinary
     * source Fragment's local S - P data relocation to STT_SECTION +
     * section-relative symbol offset.  Explicit .reloc and synthesized
     * DWARF fixups retain their own identity/ownership contract. */
    bool fold_local_pcrel_relocation_to_section;
    /* Some ABIs (AArch64 GAS) canonicalize eligible relocations to defined
     * local labels as STT_SECTION + offset, including PC-relative fixups. */
    bool fold_defined_local_relocation_to_section;
    bool fold_synthetic_local_relocation_to_section;
    /* Private expression-anchor visibility is an object ABI policy. The
     * generic symbol owner must not infer ELF visibility from the generated
     * spelling or assume all targets share GNU GAS's choice. */
    bool synthetic_expr_anchors_hidden;
    /* A target may veto otherwise-selected STT_SECTION folding for symbol-
     * sensitive relocation semantics (e.g. AArch64 function veneers and GOT
     * entry identity). A NULL callback imposes no additional restriction. */
    bool (*allow_local_section_symbol_fixup)(const MiniAsFixup *,
                                            const MiniAsSymbolView *);
    /* Section-symbol density is ABI/assembler policy, not an ELF constant. */
    bool emit_all_section_symbols;
    /* Target mapping symbols for data synthesized by the Common CFI writer.
     * Called once at the first FDE, after the CIE has been serialized. */
    bool (*record_cfi_data_mapping)(MiniAsCfiMappingSink *,int,uint64_t);
    /* Target-owned DWARF CFI ABI (zero => not yet available). */
    unsigned cfi_code_align;
    int cfi_data_align;
    unsigned cfi_return_reg;
    unsigned cfi_stack_reg;
    unsigned cfi_address_size;
    unsigned cfi_eh_fde_alignment; /* zero: use address size */
    /* Optional stronger alignment of the final .eh_frame FDE. GNU x86
     * pads intervening FDEs to 4 bytes but closes the section at 8. */
    unsigned cfi_eh_final_alignment;
    /* GNU target CIE encoding and linker-resolved function extent policy.
     * Kept in TargetInfo: Common emits one canonical CFI event stream. */
    unsigned cfi_cie_version; /* zero: DWARF1 CIE version, both frame outputs */
    bool cfi_initial_cfa_register_only; /* GNU initial CFA register opcode */
    bool cfi_fde_range_relocations; /* paired ADD/SUB on FDE range */
    bool cfi_debug_length_relocations; /* linker-relaxed debug CIE/FDE lengths */
    unsigned cfi_advance_reloc_mode; /* 0 none, 1 ADD/SUB, 2 SET/SUB */
    /* GNU ULEB relaxation policy: 0 none; 1 ADD/SUB only when the anchors
     * straddle a linker-relaxable instruction (newer LoongArch GNU GAS);
     * 2 SET/SUB on each valid symbolic difference (RISC-V GNU GAS);
     * 3 ADD/SUB on each valid difference (older LoongArch GNU GAS 2.42).
     * Toolchain dialect is selected by TargetInfo, not by Common inspecting
     * the assembler host or enumerating ELF machines. */
    unsigned leb128_reloc_mode;
    /* GNU GAS's ULEB fixup sweep uses the Target's final relaxation state
     * (which may be changed by .option push/norelax/pop). NULL is enabled. */
    bool (*leb128_relocations_enabled)(MiniAsTargetABIView);
    /* Optional Target recognition of fixups that permit the linker to
     * change the encoded code span after assembly (e.g. LoongArch RELAX).
     * CFI events spanning such a fixup need linker-resolved code distances. */
    bool (*cfi_linker_relaxation_marker)(MiniAsFixupKind);
    bool cfi_anchors_hidden; /* target marks private CFI anchors STV_HIDDEN */
    unsigned cfi_negate_ra_opcode;
    unsigned cfi_initial_cfa_offset;
    bool cfi_initial_ra_saved;
    void (*resolve_cfi_abi)(MiniAsTargetABIView, MiniAsCfiABI *);
    /* The DWARF register-number namespace belongs to the target ABI.
     * Common parses numeric expressions first; Target translates GNU
     * symbolic register spellings such as i386 ecx / %ecx. */
    bool (*resolve_cfi_register)(MiniAsTargetABIView, const char *, unsigned *);
    /* GNU assembler directive dialect belongs to TargetInfo, not Common. */
    unsigned default_exec_alignment; /* zero: 4; x86 GNU .text has alignment 1 */
    /* GNU target parser automatically aligns an instruction after data in
     * executable sections. This is distinct from ELF sh_addralign and does
     * not apply to instructions emitted in ordinary .data sections. */
    unsigned implicit_instruction_alignment; /* 0: no implicit alignment */
    /* Optional live Target-state default (e.g. RVC sections align to 2,
     * strict RV32I sections to 4). Evaluated after Target state init,
     * without exposing writable Common sections or symbols. */
    unsigned (*default_exec_alignment_for_site)(const void *target_state);
    unsigned word_directive_width; /* zero: 4, x86 GNU .word: 2 */
    /* GNU's target-specific macro lexer can keep `op (expr)` in one
     * comma-delimited argument when filling optional positional slots.
     * AArch64 GAS preserves this binding; x86 GAS whitespace-splits it. */
    bool macro_preserve_parenthesized_operand;
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
    uint16_t object_machine; /* 0: use Target's ELF machine. */
    bool output_rel_relocations; /* false: RELA; true: REL. */
    /* Serialized TargetSpec, interpreted only by the owning Target.
     * A fixed inline buffer avoids caller-owned borrowed pointers. */
    unsigned char target_profile[1024];
    size_t target_profile_size;
} MiniAsAssemblyConfig;

#endif
