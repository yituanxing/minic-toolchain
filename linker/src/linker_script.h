#ifndef MINILD_LINKER_SCRIPT_H
#define MINILD_LINKER_SCRIPT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef size_t MiniLdScriptExprId;
#define MINILD_SCRIPT_EXPR_NONE ((MiniLdScriptExprId)SIZE_MAX)

typedef enum MiniLdScriptExprKind {
    MINILD_SCRIPT_EXPR_INTEGER = 0,
    MINILD_SCRIPT_EXPR_DOT,
    MINILD_SCRIPT_EXPR_SYMBOL,
    MINILD_SCRIPT_EXPR_ADD,
    MINILD_SCRIPT_EXPR_SUBTRACT,
    MINILD_SCRIPT_EXPR_MULTIPLY,
    MINILD_SCRIPT_EXPR_DIVIDE,
    MINILD_SCRIPT_EXPR_SHIFT_LEFT,
    MINILD_SCRIPT_EXPR_NEGATE,
    MINILD_SCRIPT_EXPR_ALIGN,
    MINILD_SCRIPT_EXPR_ADDR,
    MINILD_SCRIPT_EXPR_ABSOLUTE
} MiniLdScriptExprKind;

typedef struct MiniLdScriptExpr {
    MiniLdScriptExprKind kind;
    uint64_t integer;
    char *name;
    MiniLdScriptExprId left;
    MiniLdScriptExprId right;
} MiniLdScriptExpr;

typedef struct MiniLdScriptPattern {
    char *text;
    bool keep;
    bool sort;
    bool common;
} MiniLdScriptPattern;

typedef enum MiniLdScriptSectionItemKind {
    MINILD_SCRIPT_SECTION_PATTERN = 0,
    MINILD_SCRIPT_SECTION_DEFINE_SYMBOL,
    MINILD_SCRIPT_SECTION_SET_DOT,
    MINILD_SCRIPT_SECTION_BYTE,
    MINILD_SCRIPT_SECTION_CONSTRUCTORS
} MiniLdScriptSectionItemKind;

typedef struct MiniLdScriptSectionItem {
    MiniLdScriptSectionItemKind kind;
    union {
        MiniLdScriptPattern pattern;
        struct {
            char *name;
            MiniLdScriptExprId expression;
        } symbol;
        MiniLdScriptExprId expression;
    } value;
} MiniLdScriptSectionItem;

typedef struct MiniLdScriptOutputSection {
    char *name;
    MiniLdScriptExprId address;
    MiniLdScriptExprId at;
    MiniLdScriptExprId align;
    MiniLdScriptSectionItem *items;
    size_t item_count;
    size_t item_capacity;
    bool discard;
} MiniLdScriptOutputSection;

typedef enum MiniLdScriptCommandKind {
    MINILD_SCRIPT_DEFINE_SYMBOL = 0,
    MINILD_SCRIPT_SET_DOT,
    MINILD_SCRIPT_OUTPUT_SECTION
} MiniLdScriptCommandKind;

typedef struct MiniLdScriptCommand {
    MiniLdScriptCommandKind kind;
    union {
        struct {
            char *name;
            MiniLdScriptExprId expression;
        } symbol;
        MiniLdScriptExprId expression;
        MiniLdScriptOutputSection section;
    } value;
} MiniLdScriptCommand;

typedef struct MiniLdScript {
    char *entry_symbol;
    char *output_arch;
    MiniLdScriptExpr *expressions;
    size_t expression_count;
    size_t expression_capacity;
    MiniLdScriptCommand *commands;
    size_t command_count;
    size_t command_capacity;
} MiniLdScript;

typedef bool (*MiniLdScriptResolveSymbol)(void *context,
                                         const char *name,
                                         uint64_t *value_out);
typedef bool (*MiniLdScriptResolveSection)(void *context,
                                          const char *name,
                                          uint64_t *value_out);

typedef struct MiniLdScriptEvalContext {
    uint64_t dot;
    MiniLdScriptResolveSymbol resolve_symbol;
    MiniLdScriptResolveSection resolve_section;
    void *user;
} MiniLdScriptEvalContext;

void minild_script_initialize(MiniLdScript *script);
void minild_script_destroy(MiniLdScript *script);
bool minild_script_parse_file(const char *path,
                              MiniLdScript *script,
                              FILE *diagnostics);
bool minild_script_evaluate(const MiniLdScript *script,
                            MiniLdScriptExprId expression,
                            const MiniLdScriptEvalContext *context,
                            uint64_t *value_out,
                            FILE *diagnostics);
bool minild_script_pattern_matches(const MiniLdScriptPattern *pattern,
                                   const char *section_name);

#endif
