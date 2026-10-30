#include "minias_internal.h"
#include "expression_grammar.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* Common MCExpr ownership. GNU expression grammar, absolute operands,
 * symbolic differences, persistent Symbol IDs, and semantic Fixup creation
 * are independent of ISA and ELF e_machine. Target encoders never duplicate
 * these algorithms; the driver owns sections, macros, and fragment layout. */

static bool parse_i64_data(const char *text, int64_t *out) {
    char *copy = minias_strdup(text);
    char *normalized;
    char *end = NULL;
    long long value;
    if (copy == NULL) return false;
    normalized = minias_expr_strip_outer_parens(copy);
    errno = 0;
    value = strtoll(normalized, &end, 0);
    if (errno != 0 || end == normalized) {
        free(copy);
        return false;
    }
    while (*end == ' ' || *end == '\t') {
        ++end;
    }
    if (*end != '\0') {
        free(copy);
        return false;
    }
    *out = (int64_t)value;
    free(copy);
    return true;
}

static bool parse_data_integer_bits(const char *text, uint64_t *out) {
    int64_t signed_value;
    char *copy;
    char *normalized;
    char *end = NULL;
    unsigned long long value;

    if (parse_i64_data(text, &signed_value)) {
        *out = (uint64_t)signed_value;
        return true;
    }
    copy = minias_strdup(text);
    if (copy == NULL) {
        return false;
    }
    normalized = minias_expr_strip_outer_parens(copy);
    if (*normalized == '-') {
        free(copy);
        return false;
    }
    errno = 0;
    value = strtoull(normalized, &end, 0);
    if (errno != 0 || end == normalized) {
        free(copy);
        return false;
    }
    while (*end == ' ' || *end == '\t') {
        ++end;
    }
    if (*end != '\0') {
        free(copy);
        return false;
    }
    *out = (uint64_t)value;
    free(copy);
    return true;
}


typedef struct MiniAsAbsoluteExpressionParser {
    MiniAs *as;
    const char *cursor;
    int64_t dot;
    bool only_absolute_symbols;
} MiniAsAbsoluteExpressionParser;

bool minias_expr_symbol_start(char ch) {
    return (unsigned char)ch==1U || ch == '.' || ch == '$' ||
           ch == '_' || isalpha((unsigned char)ch);
}

bool minias_expr_symbol_continue(char ch) {
    return minias_expr_symbol_start(ch) || isdigit((unsigned char)ch);
}

bool minias_checked_add_i64(int64_t lhs, int64_t rhs, int64_t *out) {
    if ((rhs > 0 && lhs > INT64_MAX - rhs) ||
        (rhs < 0 && lhs < INT64_MIN - rhs)) {
        return false;
    }
    *out = lhs + rhs;
    return true;
}

static bool checked_sub_i64(int64_t lhs, int64_t rhs, int64_t *out) {
    if ((rhs < 0 && lhs > INT64_MAX + rhs) ||
        (rhs > 0 && lhs < INT64_MIN + rhs)) {
        return false;
    }
    *out = lhs - rhs;
    return true;
}

/* GNU's 64-bit integer literals and fixed-width DATA use two's-complement
 * bit patterns.  A uint64_t -> int64_t conversion outside INT64_MAX is
 * implementation-defined in C; spell out the intended signed value instead
 * of relying on the host compiler's conversion behavior. */
static int64_t signed_from_bits(uint64_t bits) {
    return bits <= (uint64_t)INT64_MAX ? (int64_t)bits :
           -1 - (int64_t)(UINT64_MAX - bits);
}

static bool parse_absolute_logical_or(MiniAsAbsoluteExpressionParser *parser,
                                      int64_t *value);

static bool parse_absolute_primary(MiniAsAbsoluteExpressionParser *parser,
                                   int64_t *value) {
    const char *start;
    char name[256];
    size_t length;
    MiniAsSymbol *symbol;
    char *end = NULL;
    uint64_t number;

    minias_expr_skip_space(&parser->cursor);
    if (*parser->cursor == '(') {
        ++parser->cursor;
        if (!parse_absolute_logical_or(parser, value)) {
            return false;
        }
        minias_expr_skip_space(&parser->cursor);
        if (*parser->cursor != ')') {
            return false;
        }
        ++parser->cursor;
        return true;
    }
    if (*parser->cursor == '.' &&
        !minias_expr_symbol_continue(parser->cursor[1])) {
        parser->only_absolute_symbols = false;
        *value = parser->dot;
        ++parser->cursor;
        return true;
    }
    if (minias_expr_symbol_start(*parser->cursor)) {
        start = parser->cursor++;
        while (minias_expr_symbol_continue(*parser->cursor)) {
            ++parser->cursor;
        }
        length = (size_t)(parser->cursor - start);
        if (length == 0U || length >= sizeof(name)) {
            return false;
        }
        memcpy(name, start, length);
        name[length] = '\0';
        symbol = minias_get_symbol(parser->as, name, false);
        if (symbol == NULL || !symbol->defined ||
            symbol->section == MINIAS_SECTION_COMMON ||
            (symbol->section != MINIAS_SECTION_ABS &&
             symbol->value > (uint64_t)INT64_MAX)) {
            /* ELF COMMON st_value is alignment, not an arithmetic address.
             * GNU rejects its use in a pre-layout absolute expression. */
            return false;
        }
        if (symbol->section != MINIAS_SECTION_ABS) {
            parser->only_absolute_symbols = false;
        }
        *value = signed_from_bits(symbol->value);
        return true;
    }

    if(!minias_parse_number_literal(parser->cursor,&end,&number))return false;
    parser->cursor=end;
    /* GAS interprets a 64-bit hex mask in arithmetic as a two's-complement
     * value (e.g. subtracting 0xffffffff80000000 adds 0x80000000).
     * Avoid implementation-defined uint64->int64 casts. */
    *value=signed_from_bits(number);
    return true;
}

static bool parse_absolute_unary(MiniAsAbsoluteExpressionParser *parser,
                                 int64_t *value) {
    minias_expr_skip_space(&parser->cursor);
    if (*parser->cursor == '+') {
        ++parser->cursor;
        return parse_absolute_unary(parser, value);
    }
    if (*parser->cursor == '-') {
        int64_t operand;
        ++parser->cursor;
        if (!parse_absolute_unary(parser, &operand) || operand == INT64_MIN) {
            return false;
        }
        *value = -operand;
        return true;
    }
    if (*parser->cursor == '~' || *parser->cursor == '!') {
        int64_t operand;
        bool logical=*parser->cursor=='!';
        ++parser->cursor;
        if (!parse_absolute_unary(parser, &operand)) {
            return false;
        }
        *value = logical ? (operand==0?1:0) : ~operand;
        return true;
    }
    return parse_absolute_primary(parser, value);
}

static bool checked_mul_absolute_i64(int64_t lhs,
                                     int64_t rhs,
                                     int64_t *out) {
    if (lhs == 0 || rhs == 0) {
        *out = 0;
        return true;
    }
    if ((lhs == -1 && rhs == INT64_MIN) ||
        (rhs == -1 && lhs == INT64_MIN)) {
        return false;
    }
    if (lhs > 0) {
        if ((rhs > 0 && lhs > INT64_MAX / rhs) ||
            (rhs < 0 && rhs < INT64_MIN / lhs)) {
            return false;
        }
    } else {
        if ((rhs > 0 && lhs < INT64_MIN / rhs) ||
            (rhs < 0 && lhs < INT64_MAX / rhs)) {
            return false;
        }
    }
    *out = lhs * rhs;
    return true;
}

static bool parse_absolute_mul(MiniAsAbsoluteExpressionParser *parser,
                               int64_t *value) {
    int64_t result;

    if (!parse_absolute_unary(parser, &result)) {
        return false;
    }
    for (;;) {
        MiniAsExprBinaryOp op;
        int64_t rhs;
        int64_t next;

        if (!minias_expr_take_binary(&parser->cursor,10U,&op)) {
            break;
        }
        if (!parse_absolute_unary(parser, &rhs)) {
            return false;
        }
        if (op == MINIAS_EXPR_OP_MUL) {
            if (!checked_mul_absolute_i64(result, rhs, &next)) {
                return false;
            }
        } else {
            if (rhs == 0 || (result == INT64_MIN && rhs == -1)) {
                return false;
            }
            next = op == MINIAS_EXPR_OP_DIV ? result / rhs : result % rhs;
        }
        result = next;
    }
    *value = result;
    return true;
}

static bool parse_absolute_sum(MiniAsAbsoluteExpressionParser *parser, int64_t *value) {
    int64_t result;

    if (!parse_absolute_mul(parser, &result)) {
        return false;
    }
    for (;;) {
        MiniAsExprBinaryOp op;
        int64_t rhs;
        int64_t next;

        if (!minias_expr_take_binary(&parser->cursor,9U,&op)) {
            break;
        }
        if (!parse_absolute_mul(parser, &rhs)) {
            return false;
        }
        if (op == MINIAS_EXPR_OP_ADD) {
            if (!minias_checked_add_i64(result, rhs, &next)) {
                return false;
            }
        } else if (!checked_sub_i64(result, rhs, &next)) {
            return false;
        }
        result = next;
    }
    *value = result;
    return true;
}

static bool parse_absolute_shift(MiniAsAbsoluteExpressionParser *parser,
                                 int64_t *value) {
    int64_t result;

    if (!parse_absolute_sum(parser, &result)) {
        return false;
    }
    for (;;) {
        MiniAsExprBinaryOp op;
        int64_t shift;

        if (!minias_expr_take_binary(&parser->cursor,8U,&op)) {
            break;
        }
        if (!parse_absolute_sum(parser, &shift) || shift < 0 || shift >= 64) {
            return false;
        }
        if (op == MINIAS_EXPR_OP_SHL) {
            /* GNU GAS evaluates signed negative shifts as two's-complement
             * bit operations. C signed << of a negative value is undefined,
             * and rejecting it breaks real Linux extable_type_reg words:
             *   (17 | ((-14) << 16)) + (.Lregnr << 8)
             * Use the canonical unsigned bit pattern, then interpret as
             * signed (same convention as the shared numeric evaluator). */
            uint64_t bits=(uint64_t)result << (unsigned int)shift;
            result=signed_from_bits(bits);
        } else {
            /* GNU GAS absolute expressions shift the 64-bit bit pattern
             * logically, even after unary minus or complement. This matters
             * in Linux's mov_q macro, which tests (~mask >> 47) in .if.
             * Signed C right shift would sign-extend and choose the wrong
             * machine-instruction sequence. */
            result=signed_from_bits((uint64_t)result>>(unsigned)shift);
        }
    }
    *value = result;
    return true;
}

/* GNU GAS absolute comparisons produce -1 for true and 0 for false.
 * These are arithmetic expression operators, not only .if directives.
 * In particular Linux hand-written .fill uses -((symA-symB)>0).
 * Build comparison precedence once in Common, leaving Target expression
 * syntax and deferred symbol fixups independent. */
static bool parse_absolute_relational(MiniAsAbsoluteExpressionParser *parser,
                                      int64_t *value) {
    int64_t lhs;
    if(!parse_absolute_shift(parser,&lhs))return false;
    for(;;) {
        MiniAsExprBinaryOp op;
        int64_t rhs;
        if(!minias_expr_take_binary(&parser->cursor,7U,&op))break;
        if(!parse_absolute_shift(parser,&rhs))return false;
        lhs=((op==MINIAS_EXPR_OP_LT && lhs<rhs) ||
             (op==MINIAS_EXPR_OP_LE && lhs<=rhs) ||
             (op==MINIAS_EXPR_OP_GT && lhs>rhs) ||
             (op==MINIAS_EXPR_OP_GE && lhs>=rhs))?-1:0;
    }
    *value=lhs;return true;
}

static bool parse_absolute_equality(MiniAsAbsoluteExpressionParser *parser,
                                    int64_t *value) {
    int64_t lhs;
    if(!parse_absolute_relational(parser,&lhs))return false;
    for(;;) {
        MiniAsExprBinaryOp op;
        int64_t rhs;
        if(!minias_expr_take_binary(&parser->cursor,6U,&op))break;
        if(!parse_absolute_relational(parser,&rhs))return false;
        lhs=((lhs!=rhs)==(op==MINIAS_EXPR_OP_NE))?-1:0;
    }
    *value=lhs;return true;
}

static bool parse_absolute_and(MiniAsAbsoluteExpressionParser *parser,
                               int64_t *value) {
    int64_t result;

    if (!parse_absolute_equality(parser, &result)) {
        return false;
    }
    for (;;) {
        MiniAsExprBinaryOp op;
        int64_t rhs;

        if (!minias_expr_take_binary(&parser->cursor,5U,&op)) {
            break;
        }
        if (!parse_absolute_equality(parser, &rhs)) {
            return false;
        }
        result &= rhs;
    }
    *value = result;
    return true;
}

static bool parse_absolute_xor(MiniAsAbsoluteExpressionParser *parser,
                               int64_t *value) {
    int64_t result;

    if (!parse_absolute_and(parser, &result)) {
        return false;
    }
    for (;;) {
        MiniAsExprBinaryOp op;
        int64_t rhs;

        if (!minias_expr_take_binary(&parser->cursor,4U,&op)) {
            break;
        }
        if (!parse_absolute_and(parser, &rhs)) {
            return false;
        }
        result ^= rhs;
    }
    *value = result;
    return true;
}

static bool parse_absolute_or(MiniAsAbsoluteExpressionParser *parser,
                              int64_t *value) {
    int64_t result;

    if (!parse_absolute_xor(parser, &result)) {
        return false;
    }
    for (;;) {
        MiniAsExprBinaryOp op;
        int64_t rhs;

        if (!minias_expr_take_binary(&parser->cursor,3U,&op)) {
            break;
        }
        if (!parse_absolute_xor(parser, &rhs)) {
            return false;
        }
        result |= rhs;
    }
    *value = result;
    return true;
}

/* GNU GAS boolean operators are a separate, lower-precedence layer
 * after bitwise |/^. Unlike comparisons (-1 for true), GNU '||',
 * '&&' and unary '!' produce 1/0. Parse both operands and retain the
 * existing checked absolute arithmetic; never silently accept an
 * unresolvable symbol just because the LHS is true. */
static bool parse_absolute_logical_and(MiniAsAbsoluteExpressionParser *parser,
                                      int64_t *value) {
    int64_t lhs;
    if(!parse_absolute_or(parser,&lhs))return false;
    for(;;) {
        MiniAsExprBinaryOp op;
        int64_t rhs;
        if(!minias_expr_take_binary(&parser->cursor,2U,&op))break;
        if(!parse_absolute_or(parser,&rhs))return false;
        lhs=(lhs!=0 && rhs!=0)?1:0;
    }
    *value=lhs;return true;
}

static bool parse_absolute_logical_or(MiniAsAbsoluteExpressionParser *parser,
                                     int64_t *value) {
    int64_t lhs;
    if(!parse_absolute_logical_and(parser,&lhs))return false;
    for(;;) {
        MiniAsExprBinaryOp op;
        int64_t rhs;
        if(!minias_expr_take_binary(&parser->cursor,1U,&op))break;
        if(!parse_absolute_logical_and(parser,&rhs))return false;
        lhs=(lhs!=0 || rhs!=0)?1:0;
    }
    *value=lhs;return true;
}

bool minias_evaluate_absolute_expression(MiniAs *as,
                                         const char *text,
                                         int64_t *value) {
    MiniAsAbsoluteExpressionParser parser;
    uint64_t dot;
    uint64_t unsigned_value;

    /* GNU supports full-precision unsigned constants in .set/.equ and
     * conditional expressions. Resolve purely numeric, final in-range
     * expressions before the signed symbolic path; never coerce
     * unresolved symbols or relocations into an absolute value. */
    if (minias_eval_u64_expression(text, &unsigned_value) &&
        unsigned_value <= (uint64_t)INT64_MAX) {
        *value = (int64_t)unsigned_value;
        return true;
    }
    if(!minias_current_location(as,&dot))return false;
    if(dot>(uint64_t)INT64_MAX)return false;
    parser.as = as;
    parser.cursor = text;
    parser.dot = (int64_t)dot;
    parser.only_absolute_symbols = true;
    if (!parse_absolute_logical_or(&parser, value)) {
        return false;
    }
    minias_expr_skip_space(&parser.cursor);
    return *parser.cursor == '\0';
}

bool minias_evaluate_absolute_operand(MiniAs *as,const char *text,
                                      int64_t *value) {
    return text && value && minias_evaluate_absolute_expression(as,text,value);
}

/* A GNU expression such as (identity_mapped - relocate_kernel) is
 * absolute only AFTER both label anchors exist and subsection layout is
 * finalized. Preserve the two typed symbols throughout parse and measure:
 * parsing a forward symbol as zero or inventing a relocation is wrong.
 * Common owns syntax and final same-section semantics for all targets. */
bool minias_parse_symbol_difference(const char *expression,
                                    MiniAsSymbolDifference *out) {
    char *copy,*value,*minus;
    bool ok=false;
    if(!expression || !out)return false;
    copy=minias_strdup(expression);
    if(!copy)return false;
    value=minias_expr_strip_outer_parens(copy);
    minus=strchr(value,'-');
    if(minus && minus!=value) {
        *minus++='\0';
        if(minias_parse_symbol_addend(minias_trim(value),&out->positive) &&
           minias_parse_symbol_addend(minias_trim(minus),&out->negative))
            ok=true;
    }
    free(copy);
    return ok;
}

/* One Common parser for a retained section-symbol difference followed by
 * a GNU absolute arithmetic suffix. A64 branch/memory operands share this
 * with any future target; neither Target reinterprets '-4' as an addend on
 * the negative symbol, which would flip its sign. */
bool minias_parse_symbol_difference_absolute_tail(
    MiniAs *as,const char *text,MiniAsSymbolDifference *result) {
    char *copy=minias_strdup(text),*body,*first_minus,*split=NULL;
    unsigned depth=0U;
    int64_t tail=0;
    bool ok=false;
    if(!copy)return false;
    body=minias_expr_strip_outer_parens(copy);
    first_minus=strchr(body,'-');
    for(char *p=body;*p;++p) {
        if(*p=='(')++depth;
        else if(*p==')') {
            if(depth==0U)goto done;
            --depth;
        }else if(depth==0U && first_minus && p>first_minus &&
                 (*p=='+' || *p=='-')) {
            split=p;
            break;
        }
    }
    if(split) {
        if(!minias_evaluate_absolute_expression(as,split,&tail))goto done;
        *split='\0';
    }
    if(!minias_parse_symbol_difference(body,result))goto done;
    ok=minias_checked_add_i64(result->positive.addend,tail,
                              &result->positive.addend);
done:
    free(copy);
    return ok;
}

/* Stable parser-name identities resolve against actual SymbolIDs only after
 * final layout.  No conversion back to 256-byte parse DTOs or name-based
 * second lookup is needed; this is the single checked difference resolver
 * for all Targets. */
bool minias_resolve_symbol_difference_ref(MiniAs *as,
                                          const MiniAsSymbolDifferenceRef *pair,
                                          int64_t *value) {
    const MiniAsSymbol *positive,*negative;
    int64_t first,second,delta;
    if(!as || !pair || !value)return false;
    positive=minias_symbol_ref_lookup(as,pair->positive);
    negative=minias_symbol_ref_lookup(as,pair->negative);
    if(!positive || !negative ||
       !positive->defined || !negative->defined ||
       positive->section<0 || positive->section!=negative->section ||
       positive->value>INT64_MAX || negative->value>INT64_MAX)
        return false;
    first=(int64_t)positive->value;
    second=(int64_t)negative->value;
    if(!minias_checked_add_i64(first,pair->positive.addend,&first) ||
       !minias_checked_add_i64(second,pair->negative.addend,&second) ||
       !checked_sub_i64(first,second,&delta))return false;
    *value=delta;
    return true;
}

bool minias_parse_absolute_data_bits(MiniAs *as,
                                     const char *text,
                                     uint64_t *value) {
    MiniAsAbsoluteExpressionParser parser;
    uint64_t dot;
    int64_t signed_value;

    if (parse_data_integer_bits(text, value)) {
        return true;
    }
    /* Common unsigned expression grammar supports GNU C-preprocessed
     * 64-bit constants including 0x9000...ULL, shifts, masks and brackets.
     * This numeric-only fast path cannot resolve unknown symbols and thus
     * cannot fabricate relocations or pre-layout symbol differences. */
    if(minias_eval_u64_expression(text,value))return true;
    if(!minias_current_location(as,&dot))return false;
    if(dot>(uint64_t)INT64_MAX)return false;
    parser.as = as;
    parser.cursor = text;
    parser.dot = (int64_t)dot;
    parser.only_absolute_symbols = true;
    if (!parse_absolute_logical_or(&parser, &signed_value)) {
        return false;
    }
    minias_expr_skip_space(&parser.cursor);
    if (*parser.cursor != '\0' || !parser.only_absolute_symbols) {
        return false;
    }
    *value = (uint64_t)signed_value;
    return true;
}

char *minias_expr_strip_outer_parens(char *text) {
    text = minias_trim(text);
    for (;;) {
        size_t len = strlen(text);
        size_t i;
        int depth = 0;
        bool encloses_all = true;

        if (len < 2U || text[0] != '(' || text[len - 1U] != ')') {
            return text;
        }
        for (i = 0U; i < len; ++i) {
            if (text[i] == '(') {
                ++depth;
            } else if (text[i] == ')') {
                --depth;
                if (depth < 0) {
                    return text;
                }
                if (depth == 0 && i + 1U != len) {
                    encloses_all = false;
                    break;
                }
            }
        }
        if (!encloses_all || depth != 0) {
            return text;
        }
        text[len - 1U] = '\0';
        text = minias_trim(text + 1);
    }
}

/* GAS permits parentheses around a data relocation symbol, including
 * nested (symbol+addend) groups. Normalize a private copy only for data
 * relocation parsing: never mutate the stored statement or bypass the
 * Common typed Fixup path. The dominant plain-symbol path stays alloc-free. */
/* One relocatable symbol plus an otherwise absolute GNU arithmetic tail.
 * Resolve .set counters at their lexical DATA statement, not at final
 * emit, and retain an ordinary typed SymbolExpr/ELF addend. */
bool minias_expr_parse_symbol_absolute_tail(MiniAs *as,const char *text,
                                             MiniAsSymbolExpr *expr) {
    const char *p=text,*begin;
    char name[256];
    size_t len;
    int64_t addend;
    while(isspace((unsigned char)*p))++p;
    if(!minias_expr_symbol_start(*p))return false;
    begin=p++;
    while(minias_expr_symbol_continue(*p))++p;
    len=(size_t)(p-begin);
    if(len==0U || len>=sizeof(name))return false;
    memcpy(name,begin,len);name[len]='\0';
    while(isspace((unsigned char)*p))++p;
    if((*p!='+' && *p!='-') ||
       !minias_parse_symbol_addend(name,expr) ||
       !minias_evaluate_absolute_expression(as,p,&addend))return false;
    expr->addend=addend;
    return true;
}

static bool parse_data_symbol_addend(const char *text,
                                     MiniAsSymbolExpr *expr) {
    char *copy;
    bool valid;
    if(*text!='(')return minias_parse_symbol_addend(text,expr);
    copy=minias_strdup(text);
    if(!copy)return false;
    valid=minias_parse_symbol_addend(minias_expr_strip_outer_parens(copy),expr);
    free(copy);
    return valid;
}

/* A relocation plus a constant is commutative in GNU DATA expressions.
 * Keep the same SymbolID/fixup regardless of operand order: the existing
 * symbol-first parser handles `symbol + constant`, while Linux real-mode
 * tables also emit `0x9b000000 + pa_real_mode_base`.  The prefix must be
 * entirely absolute (not another section symbol), and arithmetic must not
 * silently overflow a signed ELF relocation addend. */
static bool parse_data_absolute_prefix_plus_symbol(MiniAs *as,
                                                    const char *text,
                                                    MiniAsSymbolExpr *expr) {
    char *copy=minias_strdup(text),*body,*p;
    unsigned depth=0U;
    bool matched=false;
    if(copy==NULL)return false;
    body=minias_expr_strip_outer_parens(copy);
    for(p=body;*p;++p) {
        uint64_t bits;
        MiniAsSymbolExpr candidate={0};
        int64_t combined;
        if(*p=='('){++depth;continue;}
        if(*p==')'){if(depth==0U)break;--depth;continue;}
        if(*p!='+' || depth!=0U || p==body)continue;
        *p='\0';
        if(minias_parse_absolute_data_bits(as,minias_trim(body),&bits) &&
           minias_parse_symbol_addend(minias_trim(p+1U),&candidate) &&
           minias_checked_add_i64(candidate.addend,signed_from_bits(bits),
                                  &combined)) {
            candidate.addend=combined;
            *expr=candidate;
            matched=true;
        }
        *p='+';
        if(matched)break;
    }
    free(copy);
    return matched;
}

static bool parse_symbol_minus_dot(const char *text, MiniAsSymbolExpr *expr) {
    char *copy;
    char *normalized;
    char *minus;
    char *suffix;
    char *lhs;
    bool ok;
    uint64_t trailing_addend = 0U;

    if (text == NULL || expr == NULL) {
        return false;
    }
    copy = minias_strdup(text);
    if (copy == NULL) {
        return false;
    }
    normalized = minias_expr_strip_outer_parens(copy);
    minus = strrchr(normalized, '-');
    if (minus == NULL) {
        free(copy);
        return false;
    }
    *minus = '\0';
    suffix = minias_expr_strip_outer_parens(minias_trim(minus + 1));
    if (strcmp(suffix, ".") != 0) {
        /* GNU accepts `symbol - . + addend` as a single PC-relative
         * relocation. Keep the semantic symbol expression typed;
         * do not have ELF targets parse or rewrite source strings. */
        char *suffix_tail=suffix;
        if(*suffix_tail++!='.') {
            free(copy);return false;
        }
        while(isspace((unsigned char)*suffix_tail))++suffix_tail;
        if(*suffix_tail++!='+') {
            free(copy);return false;
        }
        suffix_tail=minias_trim(suffix_tail);
        if(!minias_parse_u64(suffix_tail,&trailing_addend) ||
           trailing_addend>INT64_MAX) {
            free(copy);return false;
        }
    }
    lhs = minias_expr_strip_outer_parens(minias_trim(normalized));
    ok = minias_parse_symbol_addend(lhs, expr);
    if(ok && trailing_addend!=0U) {
        if(expr->addend>INT64_MAX-(int64_t)trailing_addend)
            ok=false;
        else expr->addend+=(int64_t)trailing_addend;
    }
    free(copy);
    return ok;
}

static bool parse_symbol_minus_dot_divisor(const char *text,
                                           MiniAsSymbolExpr *expr,
                                           uint64_t *divisor) {
    char *copy;
    char *normalized;
    char *slash = NULL;
    int depth = 0;
    char quote = '\0';
    char *p;
    uint64_t value;
    bool ok;

    if (text == NULL || expr == NULL || divisor == NULL) {
        return false;
    }
    copy = minias_strdup(text);
    if (copy == NULL) {
        return false;
    }
    normalized = minias_trim(copy);
    for (p = normalized; *p != '\0'; ++p) {
        if (quote != '\0') {
            if (*p == '\\' && p[1] != '\0') {
                ++p;
            } else if (*p == quote) {
                quote = '\0';
            }
            continue;
        }
        if (*p == '\'' || *p == '"') {
            quote = *p;
        } else if (*p == '(') {
            ++depth;
        } else if (*p == ')') {
            if (depth > 0) {
                --depth;
            }
        } else if (*p == '/' && depth == 0) {
            slash = p;
            break;
        }
    }
    if (slash == NULL) {
        free(copy);
        return false;
    }
    *slash = '\0';
    if (!minias_parse_u64(minias_trim(slash + 1), &value) || value == 0U) {
        free(copy);
        return false;
    }
    normalized = minias_expr_strip_outer_parens(minias_trim(normalized));
    ok = parse_symbol_minus_dot(normalized, expr);
    if (ok) {
        *divisor = value;
    }
    free(copy);
    return ok;
}

static bool parse_symbol_difference(const char *text,
                                    MiniAsSymbolExpr *lhs_expr,
                                    MiniAsSymbolExpr *rhs_expr) {
    char *copy;
    char *normalized;
    char *minus;
    char *lhs;
    char *rhs;
    bool rhs_parenthesized;
    bool ok;

    if (text == NULL || lhs_expr == NULL || rhs_expr == NULL) {
        return false;
    }
    copy = minias_strdup(text);
    if (copy == NULL) {
        return false;
    }
    normalized = minias_expr_strip_outer_parens(copy);
    minus = strrchr(normalized, '-');
    if (minus == NULL) {
        free(copy);
        return false;
    }
    *minus = '\0';
    lhs = minias_expr_strip_outer_parens(minias_trim(normalized));
    rhs = minias_trim(minus + 1);
    /* GNU expression precedence: A-B+11 means (A-B)+11, whereas
     * A-(B+11) means A-B-11. Preserve the grouping *before* removing
     * enclosing parentheses from the right-hand symbol expression. */
    rhs_parenthesized=rhs[0]=='(' &&
                       rhs[strlen(rhs)-1U]==')';
    rhs = minias_expr_strip_outer_parens(rhs);
    if (strcmp(rhs, ".") == 0) {
        free(copy);
        return false;
    }
    ok = minias_parse_symbol_addend(lhs, lhs_expr) &&
         minias_parse_symbol_addend(rhs, rhs_expr);
    if(ok && !rhs_parenthesized && rhs_expr->addend!=0) {
        int64_t extra=rhs_expr->addend;
        if((extra>0 && lhs_expr->addend>INT64_MAX-extra) ||
           (extra<0 && lhs_expr->addend<INT64_MIN-extra))
            ok=false;
        else {
            lhs_expr->addend+=extra;
            rhs_expr->addend=0;
        }
    }
    free(copy);
    return ok;
}

/* GNU .size NAME, .-NAME+N expresses a moving end Fragment anchor plus a
 * lexical constant. The Common expression grammar handles right-hand
 * parentheses and `A-B+N` precedence; .size must retain the resulting
 * signed offset, not freeze the initial function byte count. */
bool minias_parse_size_end_addend(const char *text,const char *name,
                                  int64_t *addend) {
    MiniAsSymbolExpr lhs={0},rhs={0};
    if(!text || !name || !addend ||
       !parse_symbol_difference(text,&lhs,&rhs) ||
       strcmp(lhs.name,".")!=0 || strcmp(rhs.name,name)!=0)
        return false;
    return checked_sub_i64(lhs.addend,rhs.addend,addend);
}

/* GNU code-relative data tables use (S1-S2)/4 in .byte/.2byte,
 * including forward local labels from ARM64 exception tables. This is
 * Common section arithmetic, not an ISA-specific relocation opcode. */
static bool parse_symbol_difference_divisor(const char *text,
        MiniAsSymbolExpr *lhs,MiniAsSymbolExpr *rhs,uint64_t *divisor,
        bool *arithmetic_shift) {
    char *copy=minias_strdup(text),*p,*operator_at=NULL,*normalized;
    unsigned depth=0U;
    bool ok=false,shift=false;
    uint64_t n;
    if(!copy)return false;
    /* GNU parenthesizes the ENTIRE shifted expression:
     * ((end-start) >> 5). Remove only whole-expression wrapping
     * parentheses BEFORE locating top-level >> or division; otherwise
     * depth never reaches zero and the operator is incorrectly hidden. */
    normalized=minias_expr_strip_outer_parens(copy);
    for(p=normalized;*p;++p) {
        if(*p=='(')++depth;
        else if(*p==')') {
            if(!depth)goto done;
            --depth;
        }else if(depth==0U && *p=='/') {
            if(operator_at)goto done;
            operator_at=p;
        }else if(depth==0U && p[0]=='>' && p[1]=='>') {
            if(operator_at)goto done;
            operator_at=p;shift=true;++p;
        }
    }
    if(depth || !operator_at)goto done;
    *operator_at='\0';
    if(!minias_parse_u64(minias_trim(operator_at+(shift?2U:1U)),&n))
        goto done;
    /* GAS section-symbol differences use unsigned 64-bit logical >>5:
     * -33 >> 5 = 0x07fffffffffffffe, not signed -2 and not -1.
     * Reject values that cannot fit in the destination .word/.long. */
    if(shift) {
        if(n>=63U)goto done;
        *divisor=UINT64_C(1)<<n;
    }else {
        if(n==0U || n>(uint64_t)INT64_MAX)goto done;
        *divisor=n;
    }
    if(parse_symbol_difference(minias_expr_strip_outer_parens(minias_trim(normalized)),
                               lhs,rhs)) {
        *arithmetic_shift=shift;ok=true;
    }
done:
    free(copy);return ok;
}

/* A persistent GNU scalar-expression program shared by .org and variable
 * DATA fragments. Parse once at the source site, bind symbols to stable
 * ordinals, and evaluate against the final MCFragment dot.  The operators
 * and signed-overflow contracts intentionally match the absolute GNU
 * expression parser above; this is not another source-string fallback. */
typedef enum DeferredOp {
    DEF_LITERAL, DEF_SYMBOL, DEF_DOT,
    DEF_NEG, DEF_INV, DEF_LNOT,
    DEF_ADD, DEF_SUB, DEF_MUL, DEF_DIV, DEF_MOD,
    DEF_SHL, DEF_SHR, DEF_LT, DEF_LE, DEF_GT, DEF_GE,
    DEF_EQ, DEF_NE, DEF_AND, DEF_XOR, DEF_OR, DEF_LAND, DEF_LOR
} DeferredOp;

typedef struct DeferredNode {
    DeferredOp op;
    size_t left,right;
    size_t symbol_index;
    int64_t value;
} DeferredNode;

struct MiniAsDeferredExpr {
    DeferredNode *nodes;
    size_t count,capacity,root;
};

typedef struct DeferredParser {
    MiniAs *as;
    MiniAsDeferredExpr *tree;
    const char *cursor;
    unsigned depth;
} DeferredParser;

static bool deferred_add(DeferredParser *p, DeferredOp op,
                         size_t lhs,size_t rhs,size_t symbol,int64_t value,
                         size_t *id) {
    MiniAsDeferredExpr *t=p->tree;
    DeferredNode *node;
    if(t->count>=512U ||
       !minias_grow_array((void **)&t->nodes,&t->capacity,
                          sizeof(*t->nodes),t->count+1U))return false;
    *id=t->count++;
    node=&t->nodes[*id];
    node->op=op;node->left=lhs;node->right=rhs;
    node->symbol_index=symbol;node->value=value;
    return true;
}

/* Deferred AST retains its own operator tags and later section algebra,
 * while sharing the exact lexer and precedence with the eager evaluators. */
static DeferredOp deferred_operator(MiniAsExprBinaryOp op) {
    static const DeferredOp mapping[] = {
        [MINIAS_EXPR_OP_LOR]=DEF_LOR,
        [MINIAS_EXPR_OP_LAND]=DEF_LAND,
        [MINIAS_EXPR_OP_OR]=DEF_OR,
        [MINIAS_EXPR_OP_XOR]=DEF_XOR,
        [MINIAS_EXPR_OP_AND]=DEF_AND,
        [MINIAS_EXPR_OP_EQ]=DEF_EQ,
        [MINIAS_EXPR_OP_NE]=DEF_NE,
        [MINIAS_EXPR_OP_LT]=DEF_LT,
        [MINIAS_EXPR_OP_LE]=DEF_LE,
        [MINIAS_EXPR_OP_GT]=DEF_GT,
        [MINIAS_EXPR_OP_GE]=DEF_GE,
        [MINIAS_EXPR_OP_SHL]=DEF_SHL,
        [MINIAS_EXPR_OP_SHR]=DEF_SHR,
        [MINIAS_EXPR_OP_ADD]=DEF_ADD,
        [MINIAS_EXPR_OP_SUB]=DEF_SUB,
        [MINIAS_EXPR_OP_MUL]=DEF_MUL,
        [MINIAS_EXPR_OP_DIV]=DEF_DIV,
        [MINIAS_EXPR_OP_MOD]=DEF_MOD
    };
    return op>MINIAS_EXPR_OP_NONE &&
           (unsigned)op<sizeof(mapping)/sizeof(mapping[0])?
           mapping[op]:DEF_LITERAL;
}

static bool deferred_parse_binary(DeferredParser *p,unsigned minimum,
                                  size_t *result);

static bool deferred_parse_prefix(DeferredParser *p,size_t *result) {
    const char *start;
    char *end=NULL;
    char name[256];
    size_t length,operand;
    uint64_t number;
    MiniAsSymbol *symbol;
    DeferredOp op;
    if(++p->depth>96U)return false;
    minias_expr_skip_space(&p->cursor);
    if(*p->cursor=='(') {
        ++p->cursor;
        if(!deferred_parse_binary(p,1U,result))return false;
        minias_expr_skip_space(&p->cursor);
        if(*p->cursor!=')')return false;
        ++p->cursor;--p->depth;return true;
    }
    if(*p->cursor=='+' || *p->cursor=='-' ||
       *p->cursor=='~' || *p->cursor=='!') {
        char ch=*p->cursor++;
        if(!deferred_parse_prefix(p,&operand))return false;
        if(ch=='+') {*result=operand;--p->depth;return true;}
        op=ch=='-'?DEF_NEG:ch=='~'?DEF_INV:DEF_LNOT;
        --p->depth;
        return deferred_add(p,op,operand,0U,0U,0,result);
    }
    if(*p->cursor=='.' &&
       !minias_expr_symbol_continue(p->cursor[1])) {
        ++p->cursor;--p->depth;
        return deferred_add(p,DEF_DOT,0U,0U,0U,0,result);
    }
    if(minias_expr_symbol_start(*p->cursor)) {
        start=p->cursor++;
        while(minias_expr_symbol_continue(*p->cursor))++p->cursor;
        length=(size_t)(p->cursor-start);
        if(length==0U || length>=sizeof(name))return false;
        memcpy(name,start,length);name[length]='\0';
        symbol=minias_get_symbol(p->as,name,true);
        if(symbol==NULL)return false;
        --p->depth;
        return deferred_add(p,DEF_SYMBOL,0U,0U,
                             (size_t)(symbol-p->as->symbols),0,result);
    }
    if(!minias_parse_number_literal(p->cursor,&end,&number))return false;
    p->cursor=end;--p->depth;
    return deferred_add(p,DEF_LITERAL,0U,0U,0U,
               signed_from_bits(number),result);
}

static bool deferred_parse_binary(DeferredParser *p,unsigned minimum,
                                  size_t *result) {
    size_t lhs,rhs;
    if(!deferred_parse_prefix(p,&lhs))return false;
    for(;;) {
        MiniAsExprBinary found;
        minias_expr_skip_space(&p->cursor);
        if(!minias_expr_scan_binary(p->cursor,&found) ||
           found.precedence<minimum)break;
        p->cursor+=found.width;
        if(!deferred_parse_binary(p,found.precedence+1U,&rhs) ||
           !deferred_add(p,deferred_operator(found.op),lhs,rhs,
                         0U,0,&lhs))return false;
    }
    *result=lhs;
    return true;
}

void minias_deferred_expr_destroy(MiniAsDeferredExpr *tree) {
    if(tree!=NULL) {free(tree->nodes);free(tree);}
}

MiniAsDeferredExpr *minias_compile_deferred_expr(MiniAs *as,
                                                 const char *text) {
    MiniAsDeferredExpr *tree=calloc(1U,sizeof(*tree));
    DeferredParser parser;
    if(tree==NULL) {
        minias_set_error(as,"out-of-memory:deferred-expression");return NULL;
    }
    parser.as=as;parser.tree=tree;parser.cursor=text;parser.depth=0U;
    if(!deferred_parse_binary(&parser,1U,&tree->root))goto invalid;
    minias_expr_skip_space(&parser.cursor);
    if(*parser.cursor!='\0')goto invalid;
    return tree;
invalid:
    minias_deferred_expr_destroy(tree);
    if(as->error[0]=='\0')
        minias_set_error(as,"unsupported-typed-deferred-expression:%.90s",text);
    return NULL;
}

/* All children have smaller IDs than their parent. One iterative pass
 * avoids recursive evaluation on every layout iteration, and a bound on
 * expression nodes protects complex GNU macro-generated inputs. */
bool minias_evaluate_deferred_expr(const MiniAs *as,
                                  const MiniAsDeferredExpr *tree,
                                  uint64_t dot,int dot_section,
                                  bool allow_same_section,int64_t *result) {
    int64_t values[512];
    /* ELF section identities are algebraic units, not just current numeric
     * addresses: .space (end_in_A-start_in_B) must NOT turn unrelated
     * relocations into an apparently absolute size.  A single live section
     * coefficient is enough for GNU's legal S1-S2, .-S and (. + addend)
     * forms.  Non-linear operators only accept already-absolute operands. */
    int sections[512];
    int coefficients[512];
    size_t i;
    if(!tree || tree->count==0U || tree->count>512U ||
       dot>(uint64_t)INT64_MAX)return false;
    for(i=0U;i<tree->count;++i) {
        const DeferredNode *n=&tree->nodes[i];
        int64_t l=0,r=0,v=0;
        int lc=0,rc=0,c=0,section=MINIAS_SECTION_ABS;
        int ls=MINIAS_SECTION_ABS,rs=MINIAS_SECTION_ABS;
        uint64_t bits;
        if(n->left<i) {
            l=values[n->left];lc=coefficients[n->left];ls=sections[n->left];
        }
        if(n->right<i) {
            r=values[n->right];rc=coefficients[n->right];rs=sections[n->right];
        }
        switch(n->op) {
        case DEF_LITERAL:v=n->value;break;
        case DEF_DOT:
            if(dot_section<0)return false;
            v=(int64_t)dot;c=1;section=dot_section;
            break;
        case DEF_SYMBOL: {
            const MiniAsSymbol *sym;
            if(n->symbol_index>=as->symbol_count)return false;
            sym=&as->symbols[n->symbol_index];
            if(!sym->defined || sym->section==MINIAS_SECTION_COMMON ||
               (sym->section!=MINIAS_SECTION_ABS &&
                sym->value>(uint64_t)INT64_MAX))return false;
            v=signed_from_bits(sym->value);
            if(sym->section!=MINIAS_SECTION_ABS) {
                if(sym->section<0)return false;
                c=1;section=sym->section;
            }
            break;
        }
        case DEF_NEG:
            if(l==INT64_MIN)return false;
            v=-l;c=-lc;section=ls;break;
        case DEF_INV:
            if(lc!=0)return false;
            v=~l;break;
        case DEF_LNOT:
            if(lc!=0)return false;
            v=l==0?1:0;break;
        case DEF_ADD:case DEF_SUB:
            /* Cross-section subtraction is not an absolute number, even
             * when both section-relative st_values happen to be equal. */
            if(lc!=0 && rc!=0 && ls!=rs)return false;
            section=lc!=0?ls:rs;
            c=n->op==DEF_ADD?lc+rc:lc-rc;
            /* Two same-sign RELOC operands cannot be represented as one
             * ELF section-relative value: S1+S2 is invalid, S1-S2 is
             * absolute only when both symbols belong to one section. */
            if(c>1 || c< -1)return false;
            if(c==0)section=MINIAS_SECTION_ABS;
            if(!(n->op==DEF_ADD?
                 minias_checked_add_i64(l,r,&v):
                 checked_sub_i64(l,r,&v)))return false;
            break;
        case DEF_MUL:if(!checked_mul_absolute_i64(l,r,&v))return false;break;
        case DEF_DIV:case DEF_MOD:
            if(r==0 || (l==INT64_MIN && r==-1))return false;
            v=n->op==DEF_DIV?l/r:l%r;break;
        case DEF_SHL:case DEF_SHR:
            if(r<0 || r>=63)return false;
            bits=(uint64_t)l;
            if(n->op==DEF_SHL)bits<<=(unsigned)r;
            else if(l<0 && r>0)
                bits=(bits>>(unsigned)r)|(UINT64_MAX<<(64U-(unsigned)r));
            else bits>>=(unsigned)r;
            v=signed_from_bits(bits);break;
        case DEF_LT:v=l<r?-1:0;break;
        case DEF_LE:v=l<=r?-1:0;break;
        case DEF_GT:v=l>r?-1:0;break;
        case DEF_GE:v=l>=r?-1:0;break;
        case DEF_EQ:v=l==r?-1:0;break;
        case DEF_NE:v=l!=r?-1:0;break;
        case DEF_AND:v=l&r;break;
        case DEF_XOR:v=l^r;break;
        case DEF_OR:v=l|r;break;
        case DEF_LAND:v=(l!=0 && r!=0)?1:0;break;
        case DEF_LOR:v=(l!=0 || r!=0)?1:0;break;
        default:return false;
        }
        /* Only add/sub preserve relocatable section affinity. All other
         * arithmetic must operate on an already absolute expression. */
        if(n->op>=DEF_MUL && (lc!=0 || rc!=0))return false;
        values[i]=v;
        sections[i]=section;
        coefficients[i]=c;
    }
    if(tree->root>=tree->count)return false;
    if(coefficients[tree->root]!=0 &&
       !(allow_same_section && coefficients[tree->root]==1 &&
         sections[tree->root]==dot_section))return false;
    *result=values[tree->root];
    return true;
}

/* Extract only the relocation-safe linear subset from the already compiled
 * expression program. A + B - C is not a single GNU O_subtract; reject it
 * instead of inventing one of its operands from a source-string rescan.
 * The retained SymbolIDs survive numeric-label replacement, macros and
 * post-parse subsection relayout without needing parser state. */
typedef struct DeferredDifferenceParts {
    size_t positive,negative;
    bool has_positive,has_negative;
    int64_t addend;
} DeferredDifferenceParts;

static bool deferred_collect_difference(const MiniAsDeferredExpr *tree,
                                        size_t index,int sign,
                                        DeferredDifferenceParts *parts) {
    const DeferredNode *node;
    int64_t value;
    if(index>=tree->count || (sign!=1 && sign!=-1))return false;
    node=&tree->nodes[index];
    switch(node->op) {
    case DEF_LITERAL:
        if(sign<0) {
            if(node->value==INT64_MIN)return false;
            value=-node->value;
        }else value=node->value;
        return minias_checked_add_i64(parts->addend,value,&parts->addend);
    case DEF_SYMBOL:
        if(sign>0) {
            if(parts->has_positive)return false;
            parts->positive=node->symbol_index;
            parts->has_positive=true;
        }else {
            if(parts->has_negative)return false;
            parts->negative=node->symbol_index;
            parts->has_negative=true;
        }
        return true;
    case DEF_ADD:
        return deferred_collect_difference(tree,node->left,sign,parts) &&
               deferred_collect_difference(tree,node->right,sign,parts);
    case DEF_SUB:
        return deferred_collect_difference(tree,node->left,sign,parts) &&
               deferred_collect_difference(tree,node->right,-sign,parts);
    case DEF_NEG:
        return deferred_collect_difference(tree,node->left,-sign,parts);
    default:
        return false;
    }
}

bool minias_deferred_symbol_difference(const MiniAsDeferredExpr *tree,
                                       size_t *positive,size_t *negative,
                                       int64_t *addend) {
    DeferredDifferenceParts parts={0};
    if(tree==NULL || positive==NULL || negative==NULL || addend==NULL ||
       tree->count==0U || tree->root>=tree->count ||
       !deferred_collect_difference(tree,tree->root,1,&parts) ||
       !parts.has_positive || !parts.has_negative)return false;
    *positive=parts.positive;
    *negative=parts.negative;
    *addend=parts.addend;
    return true;
}

bool minias_compile_variable_exprs(MiniAs *as,MiniAsStmt *stmt) {
    char *copy=minias_strdup(stmt->args),*cursor,*start;
    unsigned count=0U,depth=0U;
    bool fill=stmt->fragment_kind==MINIAS_FRAGMENT_FILL;
    if(!copy) {
        minias_set_error(as,"out-of-memory:variable-expression");
        return false;
    }
    start=copy;
    for(cursor=copy;;++cursor) {
        char ch=*cursor;
        if(ch=='(')++depth;
        else if(ch==')') {
            if(depth==0U)goto invalid;
            --depth;
        }
        if(ch==',' && depth==0U) {
            *cursor='\0';
            if(count>=3U || (!fill && count>=2U))goto invalid;
            stmt->variable_exprs[count]=
                minias_compile_deferred_expr(as,minias_trim(start));
            if(stmt->variable_exprs[count]==NULL)goto invalid;
            ++count;start=cursor+1U;
            continue;
        }
        if(ch=='\0')break;
    }
    if(depth!=0U || count>=3U || (!fill && count>=2U))goto invalid;
    stmt->variable_exprs[count]=
        minias_compile_deferred_expr(as,minias_trim(start));
    if(stmt->variable_exprs[count]==NULL)goto invalid;
    stmt->variable_expr_count=count+1U;
    free(copy);
    return true;
invalid:
    free(copy);
    if(as->error[0]=='\0')
        minias_set_error(as,"bad-variable-data-expression:line=%zu",stmt->line);
    return false;
}

bool minias_resolve_variable_exprs(MiniAs *as,const MiniAsStmt *stmt,
                                  uint64_t *repeat,unsigned *width,
                                  uint64_t *value) {
    int64_t fields[3]={0,1,0};
    unsigned i;
    bool fill=stmt->fragment_kind==MINIAS_FRAGMENT_FILL;
    if(stmt->variable_expr_count==0U ||
       stmt->variable_expr_count>(fill?3U:2U))return false;
    if(!fill)fields[1]=0;
    for(i=0U;i<stmt->variable_expr_count;++i)
        if(!minias_evaluate_deferred_expr(as,stmt->variable_exprs[i],
                                          stmt->offset,stmt->section,false,
                                          &fields[i]))
            return false;
    if(fill) {
        if(fields[1]<0 || fields[1]>8)return false;
        *repeat=fields[0]<0?0U:(uint64_t)fields[0];
        *width=(unsigned)fields[1];
        *value=(uint64_t)fields[2];
    }else {
        if(fields[0]<0 || fields[1]<-128 || fields[1]>255)return false;
        *repeat=(uint64_t)fields[0];
        *width=1U;
        *value=(uint8_t)fields[1];
    }
    return true;
}
bool minias_compile_data_mc(MiniAs *as, MiniAsStmt *stmt,
                            unsigned width) {
    MiniAsMCData *data;
    char *copy,*walk;
    size_t count=0U,i=0U;
    if(width==0U)return true;
    copy=minias_strdup(stmt->args);
    if(!copy){minias_set_error(as,"out-of-memory:mc-data-copy");return false;}
    count=1U;
    for(walk=copy;*walk;++walk)if(*walk==',')++count;
    if(count>SIZE_MAX/sizeof(MiniAsMCExpr) || count>UINT32_MAX/width) {
        minias_set_error(as,"mc-data-count-overflow:line=%zu",stmt->line);
        free(copy);return false;
    }
    data=calloc(1U,sizeof(*data));
    if(!data) {
        free(copy);minias_set_error(as,"out-of-memory:mc-data");return false;
    }
    stmt->mcdata=data; /* destructor owns partial parse on failure */
    data->fields=calloc(count,sizeof(*data->fields));
    if(!data->fields) {
        free(copy);minias_set_error(as,"out-of-memory:mc-data-fields");
        return false;
    }
    data->width=width;
    data->count=count;
    walk=copy;
    while(walk && i<count) {
        char *comma=strchr(walk,',');
        char *token;
        uint64_t value=0U,divisor=0U;
        bool shift=false,matched=false;
        MiniAsSymbolExpr lhs={0},rhs={0};
        MiniAsMCExpr *out=&data->fields[i];
        if(comma)*comma='\0';
        token=minias_trim(walk);
        /* Constant evaluation is a parse-site operation. A resolved ABS
         * .equ/.set has its lexical value now; reparsing it after
         * relaxation would accidentally observe a later .set mutation.
         * Only relocatable SymbolIDs and Fragment anchors survive. */
        if(minias_eval_u64_expression(token,&value)) {
            out->kind=MINIAS_MC_EXPR_CONSTANT;
            out->constant=value;
            matched=true;
        } else if(minias_parse_absolute_data_bits(as,token,&value)) {
            out->kind=MINIAS_MC_EXPR_CONSTANT;
            out->constant=value;
            matched=true;
        } else if((width==2U || width==4U || width==8U) &&
                  parse_symbol_minus_dot(token,&lhs)) {
            out->kind=MINIAS_MC_EXPR_SYMBOL_MINUS_DOT;
            matched=true;
        } else if((width==2U || width==4U || width==8U) &&
                  parse_symbol_minus_dot_divisor(token,&lhs,&divisor)) {
            out->kind=MINIAS_MC_EXPR_SYMBOL_MINUS_DOT_DIV;
            out->divisor=divisor;
            matched=true;
        } else if((width==1U || width==2U || width==4U || width==8U) &&
                  parse_symbol_difference_divisor(token,&lhs,&rhs,&divisor,&shift)) {
            out->kind=MINIAS_MC_EXPR_DIFFERENCE_DIV;
            out->divisor=divisor;
            out->logical_shift=shift;
            matched=true;
        } else if((width==1U || width==2U || width==4U || width==8U) &&
                  parse_symbol_difference(token,&lhs,&rhs)) {
            /* '.' is an MCFragment location anchor, NOT an ELF symbol.
             * Handle P-S from the final fragment-relative location rather
             * than serializing a fabricated undefined symbol named ".". */
            out->kind=(strcmp(lhs.name,".")==0) ?
                MINIAS_MC_EXPR_DOT_MINUS_SYMBOL :
                MINIAS_MC_EXPR_DIFFERENCE;
            matched=true;
        } else if((width==2U || width==4U || width==8U) &&
                  (parse_data_symbol_addend(token,&lhs) ||
                   minias_expr_parse_symbol_absolute_tail(as,token,&lhs) ||
                   parse_data_absolute_prefix_plus_symbol(as,token,&lhs))) {
            out->kind=MINIAS_MC_EXPR_SYMBOL;
            matched=true;
        } else {
            minias_set_error(as,"unsupported-mc-expression:%s:line=%zu",
                             token,stmt->line);
        }
        if(matched && out->kind!=MINIAS_MC_EXPR_CONSTANT) {
            MiniAsSymbol *symbol;
            out->lhs_addend=lhs.addend;
            if(out->kind!=MINIAS_MC_EXPR_DOT_MINUS_SYMBOL) {
                symbol=minias_get_symbol(as,lhs.name,true);
                if(symbol==NULL)matched=false;
                else {
                    out->has_lhs=true;
                    out->lhs_symbol_index=(size_t)(symbol-as->symbols);
                }
            }
            if(matched &&
               (out->kind==MINIAS_MC_EXPR_DIFFERENCE ||
                out->kind==MINIAS_MC_EXPR_DIFFERENCE_DIV ||
                out->kind==MINIAS_MC_EXPR_DOT_MINUS_SYMBOL)) {
                symbol=minias_get_symbol(as,rhs.name,true);
                if(symbol==NULL)matched=false;
                else {
                    out->has_rhs=true;
                    out->rhs_symbol_index=(size_t)(symbol-as->symbols);
                    out->rhs_addend=rhs.addend;
                }
            }
        }
        if(!matched) {
            if(as->error[0]=='\0')
                minias_set_error(as,"out-of-memory:mc-expression:line=%zu",
                                 stmt->line);
            free(copy);return false;
        }
        ++i;
        walk=comma?comma+1U:NULL;
    }
    free(copy);
    if(i!=count || data->count*(size_t)width!=stmt->size) {
        minias_set_error(as,"mc-expression-layout-mismatch:line=%zu",
                         stmt->line);
        return false;
    }
    return true;
}

/* A relocation's wire-format integer is not a capability bit. In ELF the
 * relocation number zero can have assigned semantics on an architecture;
 * Common must ask Target policy before choosing a fixup strategy. */
static bool target_supports_fixup(const MiniAs *as,
                                  MiniAsFixupKind kind,unsigned width) {
    if(as->target->supports_fixup_for_object!=NULL)
        return as->target->supports_fixup_for_object(as,kind,width);
    return as->target->supports_fixup!=NULL &&
           as->target->supports_fixup(kind,width);
}

static bool emit_symbol_minus_dot(MiniAs *as,
                                  const MiniAsStmt *stmt,
                                  unsigned int width,
                                  uint64_t relocation_offset,
                                  size_t symbol_index,
                                  int64_t addend) {
    const MiniAsSymbol *target=&as->symbols[symbol_index];
    bool can_add=target_supports_fixup(as,MINIAS_FIXUP_ADD,width);
    bool can_sub=target_supports_fixup(as,MINIAS_FIXUP_SUB,width);

    if (target->defined && target->section == stmt->section) {
        /* Fixed-width DATA is bit-modular, unlike a signed relocation
         * addend.  This is S+A-P modulo 2^64, with no signed overflow. */
        uint64_t value = target->value + (uint64_t)addend - relocation_offset;
        unsigned char bytes[8];
        unsigned int i;

        for (i = 0U; i < width; ++i) {
            bytes[i] = (unsigned char)((value >> (i * 8U)) & 0xffU);
        }
        return minias_section_append(as, stmt->section, bytes, width);
    }

    /* ELF64 x86 has a direct signed PC32 relocation for S + A - P.
     * Unlike LoongArch ADD/SUB pairs, one semantic fixup is sufficient.
     * A target describes this choice, Common never tests its name. */
    {
        /* Both supported ELF64 targets directly represent S+A-P at
         * either 32 or 64 bits. The Target reports whether it supports
         * the semantic relocation; no architecture-name branching here. */
        MiniAsFixupKind kind=(width==2U || width==4U)?
            as->target->dot_relative_fixup:MINIAS_FIXUP_PREL64;
        if((width==2U || width==4U || width==8U) &&
           kind!=MINIAS_FIXUP_NONE &&
           target_supports_fixup(as,kind,width))
            return minias_section_append_zero(as,stmt->section,width) &&
                   minias_add_fixup_symbol_id(as,stmt->section,relocation_offset,
                                              kind,width,symbol_index,addend);
    }

    if (!can_add || !can_sub) {
        minias_set_error(as,
                         "unsupported-symbol-difference-width:%u:line=%zu",
                         width,
                         stmt->line);
        return false;
    }

    {
        char anchor_name[96];
        MiniAsSymbol *anchor;

        if (!minias_make_private_symbol_name(anchor_name,
                                              sizeof(anchor_name),
                                              "expr",++as->expr_anchor_counter)) {
            minias_set_error(as, "expr-anchor-too-long:line=%zu", stmt->line);
            return false;
        }
        anchor = minias_get_symbol(as, anchor_name, true);
        if (anchor == NULL) {
            return false;
        }
        anchor->defined = true;
        anchor->section = stmt->section;
        anchor->value = relocation_offset;
        anchor->bind = MINIAS_STB_LOCAL;
        /* GNU GAS emits the local relocation anchor with default ELF
         * visibility (not STV_HIDDEN), even though it is a private label.
         * Visibility is independent of binding and source-level naming:
         * keep this explicit in the semantic SymbolID so the ELF writer
         * never infers it from the generated spelling. */
        anchor->visibility = MINIAS_STV_DEFAULT;

        return minias_section_append_zero(as, stmt->section, width) &&
               minias_add_fixup_symbol_id(as,
                                     stmt->section,
                                     relocation_offset,
                                     MINIAS_FIXUP_ADD,
                                     width,
                                     symbol_index,
                                     addend) &&
               minias_add_fixup(as,
                                     stmt->section,
                                     relocation_offset,
                                     MINIAS_FIXUP_SUB,
                                     width,
                                     anchor_name,
                                     0);
    }
}

static bool emit_symbol_minus_dot_divided(
    MiniAs *as,
    const MiniAsStmt *stmt,
    unsigned int width,
    uint64_t relocation_offset,
    size_t symbol_index,
    int64_t addend,
    uint64_t divisor) {
    const MiniAsSymbol *target = &as->symbols[symbol_index];
    int64_t difference;
    int64_t quotient;
    uint64_t bits;
    unsigned char bytes[8];
    unsigned int i;

    if (divisor == 0U || divisor > (uint64_t)INT64_MAX ||
        !target->defined ||
        target->section != stmt->section ||
        target->subsection != stmt->subsection) {
        minias_set_error(as,
                         "unsupported-divided-symbol-difference:%s/%llu:line=%zu",
                         target->name,
                         (unsigned long long)divisor,
                         stmt->line);
        return false;
    }
    /* GNU GAS performs section-difference arithmetic in a 64-bit modular
     * value domain BEFORE interpreting a division as signed.  In particular
     * S+INT64_MAX-P may wrap while the source still assembles.  Computing
     * this as signed C arithmetic invokes undefined behavior; computing it
     * with checked int64_t arithmetic would reject legal GNU sources. */
    difference=signed_from_bits(target->value+(uint64_t)addend-
                                relocation_offset);
    quotient = difference / (int64_t)divisor;
    bits = (uint64_t)quotient;
    for (i = 0U; i < width; ++i) {
        bytes[i] = (unsigned char)((bits >> (i * 8U)) & 0xffU);
    }
    return minias_section_append(as, stmt->section, bytes, width);
}

static bool emit_symbol_difference(MiniAs *as,
                                   const MiniAsStmt *stmt,
                                   unsigned int width,
                                   uint64_t relocation_offset,
                                   size_t lhs_index, int64_t lhs_addend,
                                   size_t rhs_index, int64_t rhs_addend) {
    const MiniAsSymbol *lhs = &as->symbols[lhs_index];
    const MiniAsSymbol *rhs = &as->symbols[rhs_index];
    bool can_add=target_supports_fixup(as,MINIAS_FIXUP_ADD,width);
    bool can_sub=target_supports_fixup(as,MINIAS_FIXUP_SUB,width);

    if (lhs->defined && rhs->defined &&
        /* If both names resolve to the same real section, its load
         * address cancels. GNU GAS resolves GLOBAL, WEAK and local names
         * alike, including when the data value belongs to a different
         * output section. Never fold SHN_COMMON/UNDEF or cross-section
         * operands: they are not absolute without linker semantics.
         * Keep target ADD/SUB pairs on RISC-V/LoongArch, where relocation
         * contracts are required by kernel .alternative patching. */
        lhs->section == rhs->section &&
        (lhs->section >= 0 || lhs->section == MINIAS_SECTION_ABS) &&
        (!can_add || !can_sub)) {
        /* Modular arithmetic gives correct low 8/16/32/64 bits and avoids
         * undefined signed overflow for large absolute/forward symbols. */
        uint64_t value=(lhs->value+(uint64_t)lhs_addend)-
                       (rhs->value+(uint64_t)rhs_addend);
        unsigned char bytes[8];
        unsigned int i;

        for (i = 0U; i < width; ++i) {
            bytes[i] = (unsigned char)((value >> (i * 8U)) & 0xffU);
        }
        return minias_section_append(as, stmt->section, bytes, width);
    }

    /*
     * GNU GAS ELF64 canonicalizes S1-S2 when S2 is a local anchor in
     * the same section as the relocation field:
     *   S1 - S2 + A = S1 - P + (P - S2 + A)
     * The right side is a single ELF PC32/PC64 fixup, even if S1 is
     * external or belongs to a different section. This avoids inventing
     * unsupported ADD/SUB relocation pairs on x86/AArch64. Targets with
     * genuine paired relocations continue to use their existing path.
     */
    if((width==2U || width==4U || width==8U) &&
       (!can_add || !can_sub) &&
       rhs->defined &&
       /* GNU GAS resolves defined GLOBAL/WEAK anchors to their section
        * positions for PC64 as well as LOCAL anchors. This binding
        * behavior is the same on x86-64 and AArch64. */
       rhs->section==stmt->section) {
        MiniAsFixupKind kind=(width==2U || width==4U)?
            as->target->dot_relative_fixup:MINIAS_FIXUP_PREL64;
        if(kind!=MINIAS_FIXUP_NONE &&
           target_supports_fixup(as,kind,width)) {
            /* GNU RELA addends are signed presentations of modular 64-bit
             * expression bits.  For example MAX-(-MAX) becomes -2; this
             * exact value is present in GNU's R_X86_64_PC64 record. */
            int64_t delta=signed_from_bits(relocation_offset-rhs->value+
                               (uint64_t)lhs_addend-(uint64_t)rhs_addend);
            return minias_section_append_zero(as,stmt->section,width) &&
                   minias_add_fixup_symbol_id(as,stmt->section,relocation_offset,
                                              kind,width,lhs_index,delta);
        }
    }
    if (!can_add || !can_sub) {
        minias_set_error(as,
                         "unsupported-symbol-difference-width:%u:line=%zu",
                         width,
                         stmt->line);
        return false;
    }
    return minias_section_append_zero(as, stmt->section, width) &&
           minias_add_fixup_symbol_id(as,
                                 stmt->section,
                                 relocation_offset,
                                 MINIAS_FIXUP_ADD,
                                 width,
                                 lhs_index,
                                 lhs_addend) &&
           minias_add_fixup_symbol_id(as,
                                 stmt->section,
                                 relocation_offset,
                                 MINIAS_FIXUP_SUB,
                                 width,
                                 rhs_index,
                                 rhs_addend);
}

static bool emit_symbol_difference_divided(MiniAs *as,
        const MiniAsStmt *stmt,unsigned int width,
        size_t lhs_index,int64_t lhs_addend,
        size_t rhs_index,int64_t rhs_addend,uint64_t divisor,
        bool arithmetic_shift) {
    const MiniAsSymbol *lhs=&as->symbols[lhs_index];
    const MiniAsSymbol *rhs=&as->symbols[rhs_index];
    int64_t diff;
    uint64_t result_bits;
    unsigned char bytes[8];
    unsigned i;
    /* A cross-section difference cannot be divided using an ELF fixup.
     * Keep the operation fail-closed rather than fabricating a value. */
    if(!lhs->defined || !rhs->defined ||
       lhs->section!=rhs->section ||
       (lhs->section<0 && lhs->section!=MINIAS_SECTION_ABS) ||
       divisor==0U || divisor>(uint64_t)INT64_MAX) {
        minias_set_error(as,"unsupported-divided-symbol-pair:line=%zu",
                         stmt->line);
        return false;
    }
    /* Preserve GNU's explicit 64-bit two's-complement difference before
     * either logical shift or signed division; no host signed overflow. */
    diff=signed_from_bits(lhs->value+(uint64_t)lhs_addend-
                          rhs->value-(uint64_t)rhs_addend);
    if(arithmetic_shift) {
        uint64_t shifted=(uint64_t)diff/divisor;
        if(width<8U &&
           shifted>((UINT64_C(1)<<(width*8U))-1U)) {
            minias_set_error(as,
                             "shifted-symbol-pair-field-overflow:line=%zu",
                             stmt->line);
            return false;
        }
        result_bits=shifted;
    }else result_bits=(uint64_t)(diff/(int64_t)divisor);
    for(i=0U;i<width;++i)
        bytes[i]=(unsigned char)(result_bits>>(8U*i));
    return minias_section_append(as,stmt->section,bytes,width);
}

/* Shared MCExpr evaluator/materializer: parse-site symbol identities and
 * addends are immutable, while section-dependent values are resolved from
 * the final layout. No Target owns this section/data semantics. */
bool minias_emit_mc_data(MiniAs *as,const MiniAsStmt *stmt) {
    const MiniAsMCData *data=stmt->mcdata;
    size_t i;
    if(!data || data->width==0U ||
       data->count>(size_t)UINT32_MAX/data->width ||
       data->count*data->width!=stmt->size) {
        minias_set_error(as,"invalid-mc-data-layout:line=%zu",stmt->line);
        return false;
    }
    if(!as->emitting_fragment || as->active_encode_result==NULL ||
       as->emitting_fragment_id>=as->stmt_count ||
       &as->stmts[as->emitting_fragment_id]!=stmt) {
        minias_set_error(as,"mcdata-emission-without-fragment");
        return false;
    }
    for(i=0U;i<data->count;++i) {
        const MiniAsMCExpr *x=&data->fields[i];
        const MiniAsSymbol *lhs=NULL,*rhs=NULL;
        const bool needs_lhs=x->kind==MINIAS_MC_EXPR_SYMBOL ||
                             x->kind==MINIAS_MC_EXPR_SYMBOL_MINUS_DOT ||
                             x->kind==MINIAS_MC_EXPR_SYMBOL_MINUS_DOT_DIV ||
                             x->kind==MINIAS_MC_EXPR_DIFFERENCE ||
                             x->kind==MINIAS_MC_EXPR_DIFFERENCE_DIV;
        const bool needs_rhs=x->kind==MINIAS_MC_EXPR_DOT_MINUS_SYMBOL ||
                             x->kind==MINIAS_MC_EXPR_DIFFERENCE ||
                             x->kind==MINIAS_MC_EXPR_DIFFERENCE_DIV;
        uint64_t value=x->constant;
        /* The Section cursor stays unchanged until this DATA fragment's
         * transaction commits. Locate '.' from the owned fragment offset
         * plus bytes already staged in the same result. */
        uint64_t at=stmt->offset+as->active_encode_result->byte_count;
        bool literal=x->kind==MINIAS_MC_EXPR_CONSTANT;
        unsigned j;
        unsigned char bytes[8];
        if((needs_lhs && !x->has_lhs) || (needs_rhs && !x->has_rhs)) {
            minias_set_error(as,"invalid-mc-expression-symbols:line=%zu",
                             stmt->line);
            return false;
        }
        if(x->has_lhs) {
            if(x->lhs_symbol_index>=as->symbol_count) {
                minias_set_error(as,"mc-lhs-symbol-out-of-range:line=%zu",
                                 stmt->line);
                return false;
            }
            lhs=&as->symbols[x->lhs_symbol_index];
        }
        if(x->has_rhs) {
            if(x->rhs_symbol_index>=as->symbol_count) {
                minias_set_error(as,"mc-rhs-symbol-out-of-range:line=%zu",
                                 stmt->line);
                return false;
            }
            rhs=&as->symbols[x->rhs_symbol_index];
        }
        if(x->kind==MINIAS_MC_EXPR_SYMBOL) {
            if(lhs && lhs->defined && lhs->section==MINIAS_SECTION_ABS) {
                literal=true;
                value=lhs->value+(uint64_t)x->lhs_addend;
            } else {
                /* GNU permits a forward .short absolute .set/.equ symbol,
                 * but no target-independent ABS16 unresolved reference. */
                if(!target_supports_fixup(as,MINIAS_FIXUP_ABS,data->width)) {
                    minias_set_error(as,"unsupported-symbol-width:%u:line=%zu",
                                     data->width,stmt->line);
                    return false;
                }
                if(!minias_section_append_zero(as,stmt->section,data->width) ||
                   !minias_add_fixup_symbol_id(as,stmt->section,at,
                          MINIAS_FIXUP_ABS,data->width,x->lhs_symbol_index,
                          x->lhs_addend))
                    return false;
                continue;
            }
        } else if(x->kind==MINIAS_MC_EXPR_SYMBOL_MINUS_DOT) {
            if(!emit_symbol_minus_dot(as,stmt,data->width,at,
                                      x->lhs_symbol_index,
                                      x->lhs_addend))return false;
            continue;
        } else if(x->kind==MINIAS_MC_EXPR_SYMBOL_MINUS_DOT_DIV) {
            if(!emit_symbol_minus_dot_divided(as,stmt,data->width,at,
                                              x->lhs_symbol_index,
                                              x->lhs_addend,x->divisor))return false;
            continue;
        } else if(x->kind==MINIAS_MC_EXPR_DOT_MINUS_SYMBOL) {
            if(rhs==NULL || !rhs->defined || rhs->section!=stmt->section) {
                minias_set_error(as,"unresolved-mc-dot-difference:line=%zu",
                                 stmt->line);
                return false;
            }
            literal=true;
            /* '.' has no SymbolID: its lexical addend stays in MCExpr,
             * while the other term is resolved directly by SymbolID. */
            value=at+(uint64_t)x->lhs_addend-
                  (rhs->value+(uint64_t)x->rhs_addend);
        } else if(x->kind==MINIAS_MC_EXPR_DIFFERENCE ||
                  x->kind==MINIAS_MC_EXPR_DIFFERENCE_DIV) {
            if(x->kind==MINIAS_MC_EXPR_DIFFERENCE &&
               (!target_supports_fixup(as,MINIAS_FIXUP_ADD,data->width) ||
                !target_supports_fixup(as,MINIAS_FIXUP_SUB,data->width)) &&
               lhs && rhs && lhs->defined && rhs->defined &&
               lhs->section==rhs->section &&
               (lhs->section==MINIAS_SECTION_ABS ||
                (lhs->section>=0 && lhs->subsection==rhs->subsection))) {
                literal=true;
                value=(lhs->value+(uint64_t)x->lhs_addend)-
                      (rhs->value+(uint64_t)x->rhs_addend);
            }else if(x->kind==MINIAS_MC_EXPR_DIFFERENCE_DIV) {
                if(!emit_symbol_difference_divided(as,stmt,data->width,
                           x->lhs_symbol_index,x->lhs_addend,
                           x->rhs_symbol_index,x->rhs_addend,
                           x->divisor,x->logical_shift))return false;
                continue;
            }else {
                if(!emit_symbol_difference(as,stmt,data->width,at,
                                           x->lhs_symbol_index,x->lhs_addend,
                                           x->rhs_symbol_index,x->rhs_addend))
                    return false;
                continue;
            }
        }
        if(!literal) {
            minias_set_error(as,"unknown-mc-expression-kind:%u:line=%zu",
                             (unsigned)x->kind,stmt->line);
            return false;
        }
        for(j=0U;j<data->width;++j)
            bytes[j]=(unsigned char)(value>>(j*8U));
        if(!minias_section_append(as,stmt->section,bytes,data->width))
            return false;
    }
    return true;
}

bool minias_parse_common_space(MiniAs *as,const char *args,uint64_t *count,
                               uint8_t *fill) {
    char *copy=minias_strdup(args),*comma;
    int64_t amount,byte=0;
    bool ok;
    if(!copy)return false;
    comma=strchr(copy,',');
    if(comma){
        *comma++='\0';
        if(strchr(comma,',') ||
           !minias_evaluate_absolute_expression(as,minias_trim(comma),&byte) ||
           byte<-128 || byte>255){free(copy);return false;}
    }
    ok=minias_evaluate_absolute_expression(as,minias_trim(copy),&amount) &&
       amount>=0;
    free(copy);
    if(!ok)return false;
    *count=(uint64_t)amount;
    *fill=(uint8_t)byte;
    return true;
}

bool minias_parse_common_fill(MiniAs *as,const char *args,
                              uint64_t *repeat,unsigned *size,
                              uint64_t *bits) {
    char *copy=minias_strdup(args),*cursor,*parts[3]={NULL,NULL,NULL};
    unsigned i=0U;
    int64_t n,step=1,value=0;
    bool ok=false;
    if(!copy)return false;
    cursor=copy;
    for(;;) {
        char *comma=strchr(cursor,',');
        if(i>=3U)goto done;
        if(comma)*comma='\0';
        parts[i++]=minias_trim(cursor);
        if(!*parts[i-1U])goto done;
        if(!comma)break;
        cursor=comma+1U;
    }
    if(!minias_evaluate_absolute_expression(as,parts[0],&n))goto done;
    /* GNU as treats a negative .fill repetition as zero (it prints a
     * warning rather than emitting bytes). In relaxed Linux Kbuild x86
     * forward IDT entries, an already-long jump may exceed its nominal
     * slot and produces this signed difference. Never cast negative n to
     * uint64_t or reject an otherwise GNU-valid source statement. */
    if(n<0)n=0;
    if(i>1U && (!minias_evaluate_absolute_expression(as,parts[1],&step) ||
                step<0 || step>8))goto done;
    if(i>2U && !minias_evaluate_absolute_expression(as,parts[2],&value))goto done;
    *repeat=(uint64_t)n;
    *size=(unsigned)step;
    *bits=(uint64_t)value;
    ok=true;
done:
    free(copy);return ok;
}

/* Complete, strictly typed DATA operands have no borrowed source text. */
void minias_mcdata_destroy(MiniAsMCData *data) {
    if(data==NULL)return;
    free(data->fields);
    free(data);
}
