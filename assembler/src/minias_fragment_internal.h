#ifndef MINIAS_FRAGMENT_INTERNAL_H
#define MINIAS_FRAGMENT_INTERNAL_H

#include "minias_target_api.h"

/* Owned only by the Common parser/MCLayout/Emission core, including all
 * persistent Fragment fields and allocations. SourceGraph receives its own
 * pass-local replay value projections; Targets receive only the restricted
 * phase views in minias_target_api.h. Neither reads these private records. */
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

typedef struct MiniAsVariableDataFragment {
    bool deferred_size;
    bool resolved;
    uint64_t repeat;
    uint64_t value;
    unsigned element_width;
    MiniAsDeferredExpr *exprs[3];
    unsigned expr_count;
} MiniAsVariableDataFragment;

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
    MiniAsTargetSiteState site;
    MiniAsFragmentPayload payload;
} MiniAsStmt;

#endif
