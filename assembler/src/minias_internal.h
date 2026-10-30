#ifndef MINIAS_INTERNAL_H
#define MINIAS_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
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

typedef struct MiniAsSection {
    char *name;
    /* GNU ELF SHT_GROUP signature; absent for ordinary Sections. */
    char *group_signature;
    /* SHT_GROUP flags are distinct from section flags. A named group may be
     * non-COMDAT (zero) or COMDAT (GRP_COMDAT), and `?` inherits the current
     * source group's policy. The ELF writer merges policy by signature. */
    uint32_t group_flags;
    /* SHF_LINK_ORDER identity is the original referenced *symbol*, not the
     * resolved SectionID: two distinct patchable-function anchors in one
     * .text still require two distinct, same-named output sections.  Resolve
     * this reference after all labels and section groups are finalized. */
    char *link_order_reference;
    /* Explicit GNU `unique,<u32>` is a Section identity component, not an
     * output flag. ID zero is valid and differs from no unique argument. */
    bool has_unique_id;
    uint32_t unique_id;
    uint32_t type;
    uint64_t flags;
    uint64_t align;
    uint64_t entry_size; /* ELF sh_entsize: GNU mergeable constants/strings */
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
    /* A data or placement fragment may change width during MC relaxation.
     * Insert one target-advertised code alignment at the next instruction,
     * not one alignment fragment per instruction in large Linux sources. */
    bool pending_instruction_alignment;
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
    /* A resolved section-relative alias still depends on its target's
     * Fragment anchor.  Preserve that edge across every MCLayout pass. */
    bool has_alias_dependency;
    int64_t alias_addend;
    size_t alias_line;
    /* GNU .set CONST + symbol - .section: deferred absolute offset
     * relative to the *named* target section, never a PC-relative RELA. */
    char *alias_section_base;
    /* Location anchors in the parsed fragment stream. Repeated labels and
     * macro expansions must not use source-line numbers as identities. */
    bool has_fragment_anchor;
    size_t fragment_anchor;
    /* GNU .set name, .+N is a location symbol, not a frozen integer. */
    int64_t fragment_anchor_addend;
    bool has_size_end_anchor;
    size_t size_end_anchor;
    /* A .size end may be in another numbered subsection of the SAME ELF
     * Section. Its Fragment owner is independent of the symbol's start. */
    uint32_t size_end_subsection;
    int64_t size_end_addend; /* .size sym, .-sym+N is a retained end MCExpr */
    /* GNU permits .size public_symbol, .-local_entry_label.  The source
     * SymbolID owns the start Fragment even when the ELF symbol whose size
     * we report is a distinct global alias. */
    bool has_size_start_symbol;
    size_t size_start_symbol_index;
    uint64_t size_end_value;
} MiniAsSymbol;

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
#define MINIAS_SYMBOL_NAME_ID_NONE SIZE_MAX
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

typedef struct MiniAsConditional {
    bool parent_active;
    bool condition_true;
    bool else_seen;
    MiniAsStableSiteId source_site;
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
    MiniAsSymbolRef expression;
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
    size_t *body_source_segments;
    size_t body_count;
    MiniAsStableSiteId definition_site;
} MiniAsMacro;

/* Common never interprets these bits: only the emitting Target knows their
 * meaning. Preserve the exact directive-selected mode at the parse site,
 * rather than reading mutable TargetContext during later relaxation. */
typedef struct MiniAsTargetSiteState {
    uint32_t mode_bits;
} MiniAsTargetSiteState;

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
    uint64_t align;
    /* GNU alignment fragment semantics, shared by all backends. */
    bool align_has_fill;
    unsigned char align_fill;
    bool align_has_max_skip;
    uint64_t align_max_skip;
    /* Common MCLayout owns every captured .org expression, never Target. */
    MiniAsOrgMode org_mode;
    MiniAsDeferredExpr *org_expr;
    uint64_t org_target;
    size_t org_symbol_index;
    int64_t org_addend;
    /* Immutable parse-site state: meaningful bits are defined by Target. */
    struct MiniAsTargetSiteState site;
    /* Common variable-size data fragment resolved after symbol layout. */
    bool deferred_data_size;
    /* Semantic payload for variable DATA fragments. Filled after the
     * fixed point; emission must never reparse the source string. */
    bool variable_data_resolved;
    uint64_t variable_repeat;
    uint64_t variable_value;
    unsigned variable_element_width;
    /* Typed machine-independent DATA byte payloads. String/incbin and
     * literal LEB bytes are parse-time snapshots; symbolic LEB bytes are
     * finalized by MCLayout, never re-decoded from source on emission. */
    unsigned char *static_data;
    size_t static_data_size;
    /* Typed LEB payload: 0=other bytes, 1=unsigned, 2=signed.
     * Deferred expressions change this payload's length at MCLayout. */
    unsigned leb_mode;
    MiniAsMCData *mcdata; /* fixed-width DATA: retained typed expressions */
    MiniAsDeferredExpr *variable_exprs[3]; /* .fill/.space size/value */
    unsigned variable_expr_count;
    MiniAsMCInst *mcinst; /* allocated by target parser, freed by core */
} MiniAsStmt;

/* A Target proposes one whole encoded fragment.  Common alone assigns its
 * final SymbolIDs, relocation ownership and Section bytes after validation. */
typedef struct MiniAsEncodeFixupRequest {
    int section;
    uint32_t fragment_offset;
    MiniAsFixupKind kind;
    unsigned width;
    MiniAsSymbolRef symbol_ref;
    /* Common MCData may refer to a final parse-site SymbolID without
     * recreating its textual symbol name. Targets continue using NameIds. */
    bool has_symbol_index;
    size_t symbol_index;
    bool absolute_zero_symbol; /* ELF STN_UNDEF=0 linker marker */
    bool prefer_section_symbol;
} MiniAsEncodeFixupRequest;

typedef struct MiniAsEncodeAnchorRequest {
    MiniAsSymbolNameId name_id;
    int section;
    uint32_t subsection;
    uint64_t offset;
} MiniAsEncodeAnchorRequest;

/* Ordered, fragment-owned output operations. One source statement can emit
 * several fixed DATA fields, a borrowed .incbin snapshot and compact fills
 * without copying a large pattern into an intermediate contiguous buffer. */
typedef enum MiniAsFragmentWriteKind {
    MINIAS_WRITE_COPIED_BYTES,
    MINIAS_WRITE_BORROWED_BYTES,
    MINIAS_WRITE_PATTERN
} MiniAsFragmentWriteKind;
typedef struct MiniAsFragmentWrite {
    MiniAsFragmentWriteKind kind;
    size_t size;
    size_t copied_offset;
    const unsigned char *borrowed_bytes;
    unsigned char pattern[8];
    size_t repeat_count;
    unsigned repeat_width;
    bool ignore_nobits_fill;
} MiniAsFragmentWrite;

typedef struct MiniAsEncodeResult {
    unsigned char *bytes;
    size_t byte_count; /* logical fragment size, including compact writes */
    size_t byte_storage_size; /* copied bytes only; never counts fills */
    size_t byte_capacity;
    MiniAsFragmentWrite *writes;
    size_t write_count;
    size_t write_capacity;
    MiniAsEncodeFixupRequest *fixups;
    size_t fixup_count;
    size_t fixup_capacity;
    MiniAsEncodeAnchorRequest *anchors;
    size_t anchor_count;
    size_t anchor_capacity;
    uint32_t elf_flags_or;
} MiniAsEncodeResult;

/* Common deferred DWARF .debug_frame CFI fragments. Target psABI owns register
 * identities and alignment; Common owns symbolic anchors and serialization. */
enum {
    CFI_DEF_CFA = 1, CFI_DEF_CFA_OFFSET, CFI_OFFSET, CFI_RESTORE,
    CFI_REMEMBER, CFI_RESTORE_STATE, CFI_NEGATE_RA,
    CFI_DEF_CFA_REGISTER, CFI_UNDEFINED, CFI_SAME_VALUE,
    CFI_VAL_OFFSET, CFI_REGISTER, CFI_ESCAPE
};

/* One owned CFI event at a stable MCFragment anchor. Raw escape payloads
 * are retained bytecode, never reparsed source strings. */
typedef struct MiniAsCfiOp {
    size_t symbol_index;
    unsigned kind;
    unsigned reg;
    int64_t value;
    uint8_t *bytes;
    size_t byte_count;
} MiniAsCfiOp;

typedef struct MiniAsCfiProc {
    size_t begin_symbol;
    size_t end_symbol;
    size_t first_op;
    size_t op_count;
    unsigned state_depth;
    int64_t cfa_offset;
    /* Per-procedure CIE return address column.  .cfi_return_column changes
     * this field, not the target ABI's initial saved-RA CFI rule. */
    unsigned return_column;
    bool cfa_known;
    bool simple;        /* GNU .cfi_startproc simple suppresses initial CFA */
    bool signal_frame;  /* GNU .cfi_signal_frame changes CIE augmentation */
} MiniAsCfiProc;

typedef struct MiniAsDebugLoc {
    size_t symbol_index;
    /* GNU `.loc ... view NAME` defines NAME as an absolute view ordinal.
     * The value is recomputed from final Fragment anchors after relaxation. */
    size_t view_symbol_index; /* SIZE_MAX unless a named view is requested */
    unsigned file;
    unsigned line;
    unsigned column;
    unsigned flags;
    unsigned discriminator;
    unsigned isa;
    /* GNU .loc view 0 asserts a fresh address view; view -0 resets the
     * view by re-establishing the address when two rows share a PC. */
    unsigned view_mode; /* 0=implicit, 1=assert-zero, 2=reset-zero, 3=named */
    uint64_t view_number;
    bool is_stmt;
} MiniAsDebugLoc;

typedef struct MiniAs {
    const struct MiniAsTargetOps *target;
    MiniAsSection *sections;
    size_t section_count;
    size_t section_capacity;
    /* Full SectionKey (name, COMDAT signature, LINK_ORDER anchor). */
    size_t *section_slots;
    size_t section_slot_capacity;
    MiniAsSymbol *symbols;
    size_t symbol_count;
    size_t symbol_capacity;
    size_t *symbol_slots;
    size_t symbol_slot_capacity;
    MiniAsInternedSymbolName *symbol_names;
    size_t symbol_name_count;
    size_t symbol_name_capacity;
    size_t *symbol_name_slots;
    size_t symbol_name_slot_capacity;
    MiniAsStmt *stmts;
    size_t stmt_count;
    size_t stmt_capacity;
    MiniAsPendingLocalCommon *pending_local_commons;
    size_t pending_local_common_count;
    size_t pending_local_common_capacity;
    MiniAsFixup *fixups;
    size_t fixup_count;
    size_t fixup_capacity;
    /* All source Fragments stage bytes/fills/fixups before Common commits.
     * CFI/.reloc/DWARF are deliberately separate, section-owned operations.
     * The commit guard prevents the final materializer from re-staging. */
    bool emitting_fragment;
    bool committing_fragment;
    size_t emitting_fragment_id;
    MiniAsEncodeResult *active_encode_result;
    MiniAsLiteral *literals;
    size_t literal_count;
    size_t literal_capacity;
    MiniAsSubsection *subsections;
    size_t subsection_count;
    size_t subsection_capacity;
    /* Parser subsection (SectionID, number) -> slot index. Rebuilt when
     * canonical layout sorts the owned subsection array. */
    size_t *subsection_slots;
    size_t subsection_slot_capacity;
    MiniAsSectionState *section_stack;
    size_t section_stack_count;
    size_t section_stack_capacity;
    /* Global private-name ordinal allocated at successful MC commit. */
    size_t synthetic_anchor_counter;
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
    /* GAS `.end` terminates the entire source stream, including a macro,
     * inline semicolon statement or repetition expansion. It is not a
     * discardable directive: all callers of the shared source dispatcher
     * observe this one state, independently of target instruction syntax. */
    bool source_ended;
    /* GNU GAS's quoted-string-aware, multi-line C comment lexical state. */
    bool c_comment_active;
    bool layout_sensitive; /* position-dependent parser-control fallback */
    bool has_deferred_data; /* forward-symbol data-size fragments */
    bool has_org_constraints; /* typed position-dependent .org fragments */
    bool has_cross_subsection_size_end; /* retained .size cross-subsection span */
    bool has_mutable_absolute_sets; /* snapshot mutable .set in data */
    /* Borrowed per-assemble-config source identities. Object SymbolIDs and
     * TargetState remain fresh on every conservative source replay. */
    MiniAsSourceGraph *source_graph;
    MiniAsStableSiteId current_source_site;
    MiniAsStableSiteId current_expansion_site;
    /* Opaque per-assembly backend parser state, owned and freed by Target. */
    void *target_state;
    bool cfi_active;
    bool cfi_debug_frame;
    bool cfi_eh_frame;
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
    /* Unnumbered GNU .file directives are independent STT_FILE symbols,
     * not indexed .loc file records nor symbols in the user-name hash. */
    char **cu_files;
    size_t cu_file_count;
    size_t cu_file_capacity;
    MiniAsDebugLoc *debug_locs;
    size_t debug_loc_count;
    size_t debug_loc_capacity;
    bool debug_is_stmt;
    uint32_t elf_flags;
    bool output_elf32; /* Common object layout, not target ISA XLEN. */
    /* Target-resolved object format overrides. Zero means the TargetOps
     * default machine and RELA; the emitter never infers an ABI from
     * instruction spellings or an ELF numeric class. */
    uint16_t object_machine;
    bool output_rel_relocations;
    /* Common-owned cache for stable symbol anchors and current-pass branch
     * membership. Cross-replay branch identities live in SourceGraph. */
    void *layout_anchor_graph;
    unsigned char *forced_long_branch_bitmap;
    size_t forced_long_branch_bitmap_size;
    int current_section;
    uint32_t current_subsection;
    int previous_section;
    uint32_t previous_subsection;
    char error[MINIAS_MAX_ERROR];
} MiniAs;

void minias_destroy(MiniAs *as);
bool minias_emit_sections(MiniAs *as);
bool minias_write_elf64(MiniAs *as, const char *path);
bool minias_cfi_directive(MiniAs *as, const char *op,
                          const char *args, size_t line);
bool minias_cfi_emit(MiniAs *as);
bool minias_anchor_here(MiniAs *as, size_t *symbol_index);
bool minias_debug_directive(MiniAs *as,const char *op,
                            const char *args,size_t line);
bool minias_debug_emit(MiniAs *as);
bool minias_debug_resolve_views(MiniAs *as, bool strict);
bool minias_write_elf32(MiniAs *as, const char *path);

int minias_find_section(const MiniAs *as, const char *name);
int minias_ensure_section(MiniAs *as, const char *name, uint32_t type, uint64_t flags, uint64_t align);
MiniAsSymbol *minias_get_symbol(MiniAs *as, const char *name, bool create);
/* A Target supplies the ISA mapping kind and final section position; Common
 * exclusively owns collision-safe Object Symbol creation and definition.
 * With always_suffix=false, ordinal zero uses the bare stem. */
bool minias_define_target_mapping_symbol(MiniAs *as, const char *stem,
                                         size_t first_ordinal,
                                         bool always_suffix,
                                         int section, uint64_t offset);
bool minias_symbol_name_intern(MiniAs *as, const char *name,
                               MiniAsSymbolNameId *name_id);
bool minias_bind_symbol_ref(MiniAs *as, const MiniAsSymbolExpr *expr,
                            MiniAsSymbolRef *ref);
bool minias_bind_symbol_difference_ref(MiniAs *as,
                                        const MiniAsSymbolDifference *expr,
                                        MiniAsSymbolDifferenceRef *ref);
bool minias_resolve_symbol_difference_ref(MiniAs *as,
                                          const MiniAsSymbolDifferenceRef *expr,
                                          int64_t *value);
/* A final-layout symbol difference is either absolute (two symbols in one
 * section), or a relocation against the positive symbol when subtracting
 * a defined anchor in the relocation field's own section. In the latter
 * case the returned addend is PC-relative to field_offset; the Target owns
 * its relocation kind and width, Common owns symbolic arithmetic. */
bool minias_resolve_symbol_difference_field(MiniAs *as,
                                            const MiniAsSymbolDifferenceRef *expr,
                                            int section,uint64_t field_offset,
                                            int64_t *absolute,bool *has_reloc,
                                            MiniAsSymbolRef *reloc_ref);
/* Borrowed; no actual Symbol is created by lookup. Pointer is invalidated
 * by any subsequent symbol-table allocation; retain NameId across passes. */
const MiniAsSymbol *minias_symbol_ref_lookup(const MiniAs *as,
                                             MiniAsSymbolRef ref);
/* Shared absolute expression evaluator for target-specific operand syntax.
 * Only already-resolved numeric/.set expressions succeed: no guessing
 * relocatable values or mirroring Core symbol algorithms per backend. */
/* Unique, target-independent GNU MCExpr grammar and symbolic object
 * evaluation. CFI, directives, and all four Targets use these APIs. */
/* Common persistent Fragment/Layout subsystem. */
bool minias_grow_array(void **, size_t *, size_t, size_t);
MiniAsSubsection *minias_find_subsection(MiniAs *, int, uint32_t);
MiniAsSubsection *minias_ensure_subsection(MiniAs *, int, uint32_t);
bool minias_finalize_subsection_layout(MiniAs *);
bool minias_refresh_symbol_aliases(MiniAs *);
void minias_layout_destroy(MiniAs *);
bool minias_solve_fragment_layout(MiniAs *, size_t **, size_t *, size_t *,
                                  bool *, FILE *);
bool minias_validate_fragment_semantics(MiniAs *);
bool minias_parse_common_space(MiniAs *, const char *, uint64_t *, uint8_t *);
bool minias_parse_common_fill(MiniAs *, const char *, uint64_t *, unsigned *,
                              uint64_t *);
bool minias_current_location(MiniAs *, uint64_t *);
bool minias_parse_u64(const char *, uint64_t *);
bool minias_checked_add_i64(int64_t, int64_t, int64_t *);
bool minias_expr_symbol_start(char);
bool minias_expr_symbol_continue(char);
char *minias_expr_strip_outer_parens(char *);
bool minias_expr_parse_symbol_absolute_tail(MiniAs *, const char *,
                                            MiniAsSymbolExpr *);
bool minias_evaluate_absolute_expression(MiniAs *, const char *, int64_t *);
bool minias_compile_data_mc(MiniAs *, MiniAsStmt *, unsigned);
MiniAsDeferredExpr *minias_compile_deferred_expr(MiniAs *, const char *);
bool minias_evaluate_deferred_expr(const MiniAs *, const MiniAsDeferredExpr *,
                                  uint64_t, int, bool, int64_t *);
void minias_deferred_expr_destroy(MiniAsDeferredExpr *);
bool minias_compile_variable_exprs(MiniAs *, MiniAsStmt *);
bool minias_resolve_variable_exprs(MiniAs *, const MiniAsStmt *,
                                  uint64_t *, unsigned *, uint64_t *);
bool minias_encode_leb_integer(int64_t, bool, unsigned char[10], size_t *);
/* Inspect the retained expression program, never source text. Only a
 * canonical symbolic difference (with optional absolute addend) qualifies
 * for a linker-visible paired ULEB relocation. */
bool minias_deferred_symbol_difference(const MiniAsDeferredExpr *,
                                       size_t *,size_t *,int64_t *);
/* Post-materialization relocation synthesis retains the original fragment
 * owner even after the ordinary encoder has committed its bytes. */
bool minias_add_late_fragment_fixup(MiniAs *,size_t,MiniAsFixupKind,
                                   size_t,int64_t);
bool minias_emit_deferred_leb_relocations(MiniAs *);
/* Shared post-encoding predicate for CFI and ULEB code-distance fields. */
bool minias_span_has_linker_relaxation(MiniAs *,size_t,size_t,bool *);
bool minias_emit_mc_data(MiniAs *, const MiniAsStmt *);
void minias_mcdata_destroy(MiniAsMCData *);
bool minias_evaluate_absolute_operand(MiniAs *,const char *,int64_t *);
/* Parsed syntax may name forward symbols; resolve only after final
 * same-section layout and fail closed on missing/cross-section anchors. */
bool minias_parse_symbol_difference(const char *,MiniAsSymbolDifference *);
/* Shared retained same-section symbol-difference grammar with absolute tail. */
bool minias_parse_symbol_difference_absolute_tail(
    MiniAs *,const char *,MiniAsSymbolDifference *);
bool minias_parse_size_end_addend(const char *,const char *,int64_t *);
bool minias_queue_literal(MiniAs *as,const MiniAsSymbolExpr *expression,
                          unsigned width,MiniAsSymbolExpr *pool_label);
/* Target and generic data fixups require an active fragment. */
bool minias_add_fixup(MiniAs *as,
                      int section,
                      uint64_t offset,
                      MiniAsFixupKind kind,
                      unsigned width,
                      const char *symbol_name,
                      int64_t addend);
/* Fixed-width retained MCData already has a real, stable SymbolID; do not
 * serialize it into a temporary name and ask the object table to find it. */
bool minias_add_fixup_symbol_id(MiniAs *as, int section, uint64_t offset,
                                MiniAsFixupKind kind, unsigned width,
                                size_t symbol_index, int64_t addend);
/* Target instructions and executable alignment produce a single staged
 * EncodeResult; no Target may directly insert into global Fixup/Symbol/ELF
 * arrays while its encoder can still fail. Offsets remain fragment-relative. */
bool minias_encode_add_fixup_ref(MiniAs *as, int section, uint64_t offset,
                                 MiniAsFixupKind kind, unsigned width,
                                 MiniAsSymbolRef ref,
                                 bool prefer_section_symbol);
bool minias_encode_add_fixup_abs0(MiniAs *as, int section, uint64_t offset,
                                  MiniAsFixupKind kind, unsigned width);
bool minias_encode_request_anchor_ref(MiniAs *as, const MiniAsStmt *stmt,
                                      const char *prefix,
                                      MiniAsSymbolRef *ref);
bool minias_encode_add_elf_flags(MiniAs *as, uint32_t flags);
void minias_encode_result_destroy(MiniAsEncodeResult *result);
/* Section-owned relocations are explicit .reloc directives or generated
 * DWARF records. Never silently fall back here from a Target encoder. */
bool minias_add_section_fixup(MiniAs *as, MiniAsFixupOrigin origin,
                             int section, uint64_t offset,
                             MiniAsFixupKind kind, unsigned width,
                             const char *symbol_name, int64_t addend);
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
/* All Target instruction and executable-padding bytes are staged into the
 * active fragment result; Common alone publishes its complete transaction. */
bool minias_emit_instruction_bytes(MiniAs *as, int section_index,
                                   const void *data, size_t size);
/* A target may choose padding bytes based on the preceding Fragment class.
 * Common owns source order, subsection identity and adjacency rules. */
bool minias_code_alignment_preceded_by_adjacent_data(const MiniAs *, int section);
bool minias_section_append(MiniAs *as, int section_index, const void *data, size_t size);
bool minias_section_append_zero(MiniAs *as, int section_index, size_t size);
void minias_set_error(MiniAs *as, const char *format, ...);
char *minias_strdup(const char *text);
/* Synthetic labels use a namespace unavailable to GNU source tokens.
 * ELF serialization gives them collision-free printable names without
 * changing their stable internal SymbolIDs. */
bool minias_make_private_symbol_name(char *out, size_t capacity,
                                     const char *kind, size_t ordinal);
bool minias_is_private_symbol_name(const char *name);
char *minias_trim(char *text);
/* Common balanced lexical grouping, Target-private operand grammar. */
bool minias_split_mc_operands(const char *,char *const [],const size_t [],
                              unsigned,unsigned *);

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

/* Common classifies accepted data-producing source directives exactly once.
 * Targets may reject an ISA escape in executable sections using this typed
 * semantic category, never by copying an evolving list of GNU aliases. */
typedef enum MiniAsCommonDirectiveKind {
    MINIAS_COMMON_DIRECTIVE_OTHER = 0,
    MINIAS_COMMON_DIRECTIVE_RAW_DATA,
    MINIAS_COMMON_DIRECTIVE_ORG
} MiniAsCommonDirectiveKind;

bool minias_query_target_relax_plan(MiniAs *,const MiniAsStmt *,
                                   MiniAsTargetRelaxPlan *);

MiniAsCfiABI minias_cfi_abi(const MiniAs *);

typedef struct MiniAsTargetOps {
    char comment_prefix;
    uint16_t elf_machine;
    /* Optional: NULL means all directives go to the common grammar. */
    bool (*parse_target_directive)(MiniAs *, const char *, const char *, size_t, bool *);
    /* Fully target-owned ISA/ABI state lifecycle and per-fragment snapshot. */
    bool (*init_target_state)(MiniAs *, const struct MiniAsAssemblyConfig *);
    bool (*configure_legacy_target)(struct MiniAsAssemblyConfig *, unsigned);
    void (*capture_site_state)(const MiniAs *, MiniAsTargetSiteState *);
    void (*destroy_target_state)(MiniAs *);
    bool (*validate_common_directive)(MiniAs *, MiniAsCommonDirectiveKind,
                                      const char *, size_t);
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
    /* Mandatory target instruction path: parse once, measure its retained
     * typed operands, then encode. Core owns fragment address and growth. */
    bool (*parse_mcinst)(MiniAs *, MiniAsStmt *);
    bool (*measure_mcinst)(MiniAs *, const MiniAsStmt *, uint32_t *);
    bool (*encode)(MiniAs *, const MiniAsStmt *);
    /* One typed TargetRelaxPlan supplies both the range requirement and
     * the longest legal encoding. No Common mnemonic/argument reparse. */
    bool (*plan_instruction_relaxation)(MiniAs *,const MiniAsStmt *,
                                        MiniAsTargetRelaxPlan *);
    /* Keep the lexical ISA/mode snapshot of the ALIGN site: a Target may
     * need different NOP encodings for different source-selected CPU modes. */
    bool (*append_code_alignment)(MiniAs *,const MiniAsStmt *,size_t);
    /* Some targets interpret an explicit alignment fill as the canonical
     * executable NOP sequence (e.g. GNU x86 `balign 16,0x90`). Common
     * must not choose machine-code bytes or hardcode a target's fill value. */
    bool (*explicit_alignment_fill_is_code_nop)(uint8_t);
    /* A semantic capability: never infer target support from an ELF
     * relocation number, where zero may be a valid numeric enumeration. */
    bool (*supports_fixup)(MiniAsFixupKind, unsigned);
    /* Optional object ABI-specific capability. For instance ELF32/i386
     * exposes ABS16/PC16 while ELF64/x86-64 intentionally rejects them. */
    bool (*supports_fixup_for_object)(const MiniAs *,MiniAsFixupKind,unsigned);
    uint32_t (*fixup_relocation_type)(MiniAsFixupKind, unsigned);
    /* Optional ABI-sensitive relocation mapping (e.g. x86-64 vs i386).
     * Both profiles reuse one instruction parser and machine encoder. */
    uint32_t (*fixup_relocation_type_for_object)(const MiniAs *,
                                                MiniAsFixupKind, unsigned);
    /* GNU named relocation spelling is ISA-owned; Common only places the
     * semantic fixup at the explicit section offset. */
    bool (*parse_named_relocation)(const char *, MiniAsFixupKind *, unsigned *);
    bool supports_literal_pools; /* .ltorg/.pool only for a checked target */
    MiniAsLiteralPoolOrder literal_pool_order;
    /* Optional post-emission ISA validation; NULL is a validated no-op.
     * Strict RISC-V needs this gate, ordinary Targets need not install a
     * synthetic always-true callback to satisfy Common. */
    bool (*validate_output)(MiniAs *);
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
                                            const MiniAsSymbol *);
    /* Section-symbol density is ABI/assembler policy, not an ELF constant. */
    bool emit_all_section_symbols;
    /* Target mapping symbols for data synthesized by the Common CFI writer.
     * Called once at the first FDE, after the CIE has been serialized. */
    bool (*record_cfi_data_mapping)(MiniAs *, int, uint64_t);
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
    unsigned cfi_eh_cie_version; /* zero: DWARF1 CIE version */
    bool cfi_initial_cfa_register_only; /* GNU initial CFA register opcode */
    bool cfi_eh_range_relocations; /* paired ADD32/SUB32 on FDE range */
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
    bool (*leb128_relocations_enabled)(const MiniAs *);
    /* Optional Target recognition of fixups that permit the linker to
     * change the encoded code span after assembly (e.g. LoongArch RELAX).
     * CFI events spanning such a fixup need linker-resolved code distances. */
    bool (*cfi_linker_relaxation_marker)(MiniAsFixupKind);
    bool cfi_anchors_hidden; /* target marks private CFI anchors STV_HIDDEN */
    unsigned cfi_negate_ra_opcode;
    unsigned cfi_initial_cfa_offset;
    bool cfi_initial_ra_saved;
    void (*resolve_cfi_abi)(const MiniAs *, MiniAsCfiABI *);
    /* The DWARF register-number namespace belongs to the target ABI.
     * Common parses numeric expressions first; Target translates GNU
     * symbolic register spellings such as i386 ecx / %ecx. */
    bool (*resolve_cfi_register)(const MiniAs *, const char *, unsigned *);
    /* GNU assembler directive dialect belongs to TargetInfo, not Common. */
    unsigned default_exec_alignment; /* zero: 4; x86 GNU .text has alignment 1 */
    /* GNU target parser automatically aligns an instruction after data in
     * executable sections. This is distinct from ELF sh_addralign and does
     * not apply to instructions emitted in ordinary .data sections. */
    unsigned implicit_instruction_alignment; /* 0: no implicit alignment */
    /* Optional live ISA-state default (e.g. RVC sections align to 2,
     * strict RV32I sections to 4). Evaluated after Target state init. */
    unsigned (*default_exec_alignment_for_site)(const MiniAs *);
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
int minias_assemble_config(const char *, const char *,
                           const MiniAsAssemblyConfig *, FILE *);

/* Internal multi-target driver (legacy public API remains RISC-V). */
int minias_assemble_with_target(const char *, const char *,
                                const MiniAsTargetOps *, bool, uint32_t,
                                unsigned, FILE *);

#endif
