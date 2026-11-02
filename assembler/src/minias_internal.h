#ifndef MINIAS_INTERNAL_H
#define MINIAS_INTERNAL_H

#include "minias_target_api.h"
#include "minias_source_replay_contract.h"

/* Common-only mutable Section/Symbol/Fragment state and ownership APIs. */
/* The SourceGraph/MCLayout replay worklist is declared in its own contract. */

/* A Target proposes one whole encoded fragment.  Common alone assigns its
 * final SymbolIDs, relocation ownership and Section bytes after validation. */
typedef struct MiniAsEncodeFixupRequest {
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
    /* SourceContext is opaque to the Target/ELF layers, which cannot mutate
     * its macro, conditional or SourceGraph implementation details. */
    MiniAsSourceContext *source;
    bool layout_sensitive; /* position-dependent parser-control fallback */
    bool has_deferred_data; /* forward-symbol data-size fragments */
    bool has_org_constraints; /* typed position-dependent .org fragments */
    bool has_cross_subsection_size_end; /* retained .size cross-subsection span */
    bool has_mutable_absolute_sets; /* snapshot mutable .set in data */
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
    /* Only the Target object-finalization callback may request late
     * section alignment changes. Common retains the actual Section owner. */
    bool finalizing_target_output;
    /* Opaque MCLayout-owned cache for stable symbol anchors and current-pass
     * branch membership. Cross-replay identities live in SourceGraph. */
    void *layout_anchor_graph;
    int current_section;
    uint32_t current_subsection;
    int previous_section;
    uint32_t previous_subsection;
    char error[MINIAS_MAX_ERROR];
} MiniAs;


static inline MiniAsTargetABIView minias_target_abi_view(const MiniAs *as) {
    MiniAsTargetABIView view={as->target_state,as->output_elf32};
    return view;
}


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
bool minias_resolve_symbol_difference_ref(const MiniAs *as,
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
MiniAsSubsection *minias_ensure_subsection(MiniAs *, int, uint32_t);
bool minias_finalize_subsection_layout(MiniAs *);
bool minias_refresh_symbol_aliases(MiniAs *);
void minias_layout_destroy(MiniAs *);
bool minias_solve_fragment_layout(MiniAs *, MiniAsFragmentGrowthSet *,
                                  bool *, FILE *);
bool minias_validate_fragment_semantics(MiniAs *);
bool minias_current_location(MiniAs *, uint64_t *);
bool minias_parse_u64(const char *, uint64_t *);
bool minias_expr_symbol_start(char);
bool minias_expr_symbol_continue(char);
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
/* One checked DWARF byte serializer for CFI and .debug_line. The unsigned
 * path accepts all uint64_t values, including those above INT64_MAX. */
bool minias_encode_uleb_integer(uint64_t, unsigned char[10], size_t *);
bool minias_section_append_le32(MiniAs *, int, uint32_t);
bool minias_section_patch_le32(MiniAs *, int, size_t, uint64_t);
bool minias_section_append_uleb(MiniAs *, int, uint64_t);
bool minias_section_append_sleb(MiniAs *, int, int64_t);
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
/* Parsed syntax may name forward symbols; resolve only after final
 * same-section layout and fail closed on missing/cross-section anchors. */
/* Shared retained same-section symbol-difference grammar with absolute tail. */
bool minias_parse_symbol_difference_absolute_tail(
    MiniAs *,const char *,MiniAsSymbolDifference *);
bool minias_parse_size_end_addend(const char *,const char *,int64_t *);
/* Target and generic data fixups require an active fragment. */
/* Common-only checked Fixup field-width contract, no Target state. */
bool minias_fixup_field_width(MiniAsFixupKind kind,unsigned width);
bool minias_add_fixup(MiniAs *as,
                      uint64_t offset,
                      MiniAsFixupKind kind,
                      unsigned width,
                      const char *symbol_name,
                      int64_t addend);
/* Fixed-width retained MCData already has a real, stable SymbolID; do not
 * serialize it into a temporary name and ask the object table to find it. */
bool minias_add_fixup_symbol_id(MiniAs *as, uint64_t offset,
                                MiniAsFixupKind kind, unsigned width,
                                size_t symbol_index, int64_t addend);
/* Target instructions and executable alignment produce a single staged
 * EncodeResult; no Target may directly insert into global Fixup/Symbol/ELF
 * arrays while its encoder can still fail. Offsets remain fragment-relative. */
bool minias_encode_add_fixup_ref(MiniAs *as, uint64_t offset,
                                 MiniAsFixupKind kind, unsigned width,
                                 MiniAsSymbolRef ref,
                                 bool prefer_section_symbol);
bool minias_encode_add_fixup_abs0(MiniAs *as, uint64_t offset,
                                  MiniAsFixupKind kind, unsigned width);
bool minias_encode_request_anchor_ref(MiniAs *as, const char *prefix,
                                      MiniAsSymbolRef *ref);
bool minias_encode_add_elf_flags(MiniAs *as, uint32_t flags);
void minias_encode_result_destroy(MiniAsEncodeResult *result);
/* Section-owned relocations are explicit .reloc directives or generated
 * DWARF records. Never silently fall back here from a Target encoder. */
bool minias_add_section_fixup(MiniAs *as, MiniAsFixupOrigin origin,
                             int section, uint64_t offset,
                             MiniAsFixupKind kind, unsigned width,
                             const char *symbol_name, int64_t addend,
                             bool prefer_section_symbol);
/* GNU/C-style binary, decimal, octal, hex and U/L/ULL literal scanner.
 * Exposes the consumed end pointer to both Common and Target grammars. */
bool minias_parse_number_literal(const char *text, char **end, uint64_t *value);
/* Shared absolute expression resolver with Common-defined .set/.equ and
 * location-counter semantics. Targets use it only for ISA field constraints;
 * section/layout/fixup emission stays in Common. */
bool minias_parse_absolute_data_bits(MiniAs *as,const char *text,
                                     uint64_t *value);
bool minias_decode_string_literals(const char *text,
                                   bool nul_terminate,
                                   unsigned char **data,
                                   size_t *size);

bool minias_section_append(MiniAs *as, int section_index, const void *data, size_t size);
bool minias_section_append_zero(MiniAs *as, int section_index, size_t size);
void minias_set_error(MiniAs *as, const char *format, ...);
/* Shared first-error-wins formatter for phase-limited Common-owned sinks. */
void minias_set_error_va(MiniAs *, const char *, va_list);
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


bool minias_query_target_relax_plan(MiniAs *,const MiniAsStmt *,
                                   MiniAsTargetRelaxPlan *);
MiniAsCfiABI minias_cfi_abi(const MiniAs *);
bool minias_cfi_record_target_mapping(MiniAs *,int section,uint64_t offset);
int minias_assemble_config(const char *, const char *,
                           const MiniAsAssemblyConfig *, FILE *);

#endif
