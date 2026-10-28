#include "minias_internal.h"

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

    if (copy == NULL) {
        return false;
    }
    normalized = minias_trim(copy);
    for (;;) {
        size_t len = strlen(normalized);
        size_t i;
        int depth = 0;
        bool encloses_all = true;

        if (len < 2U || normalized[0] != '(' ||
            normalized[len - 1U] != ')') {
            break;
        }
        for (i = 0U; i < len; ++i) {
            if (normalized[i] == '(') {
                ++depth;
            } else if (normalized[i] == ')') {
                --depth;
                if (depth < 0) {
                    encloses_all = false;
                    break;
                }
                if (depth == 0 && i + 1U != len) {
                    encloses_all = false;
                    break;
                }
            }
        }
        if (!encloses_all || depth != 0) {
            break;
        }
        normalized[len - 1U] = '\0';
        normalized = minias_trim(normalized + 1);
    }

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
    normalized = minias_trim(copy);
    for (;;) {
        size_t len = strlen(normalized);
        size_t i;
        int depth = 0;
        bool encloses_all = true;

        if (len < 2U || normalized[0] != '(' ||
            normalized[len - 1U] != ')') {
            break;
        }
        for (i = 0U; i < len; ++i) {
            if (normalized[i] == '(') {
                ++depth;
            } else if (normalized[i] == ')') {
                --depth;
                if (depth < 0) {
                    encloses_all = false;
                    break;
                }
                if (depth == 0 && i + 1U != len) {
                    encloses_all = false;
                    break;
                }
            }
        }
        if (!encloses_all || depth != 0) {
            break;
        }
        normalized[len - 1U] = '\0';
        normalized = minias_trim(normalized + 1);
    }
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

static void skip_absolute_expression_space(MiniAsAbsoluteExpressionParser *parser) {
    while (*parser->cursor == ' ' || *parser->cursor == '\t') {
        ++parser->cursor;
    }
}

bool minias_expr_symbol_start(char ch) {
    return ch == '.' || ch == '$' || ch == '_' || isalpha((unsigned char)ch);
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

    skip_absolute_expression_space(parser);
    if (*parser->cursor == '(') {
        ++parser->cursor;
        if (!parse_absolute_logical_or(parser, value)) {
            return false;
        }
        skip_absolute_expression_space(parser);
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
        *value = (int64_t)symbol->value;
        return true;
    }

    if(!minias_parse_number_literal(parser->cursor,&end,&number))return false;
    parser->cursor=end;
    /* GAS interprets a 64-bit hex mask in arithmetic as a two's-complement
     * value (e.g. subtracting 0xffffffff80000000 adds 0x80000000).
     * Avoid implementation-defined uint64->int64 casts. */
    *value=number>(uint64_t)INT64_MAX?
           -(int64_t)(UINT64_MAX-number)-1:(int64_t)number;
    return true;
}

static bool parse_absolute_unary(MiniAsAbsoluteExpressionParser *parser,
                                 int64_t *value) {
    skip_absolute_expression_space(parser);
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
        char op;
        int64_t rhs;
        int64_t next;

        skip_absolute_expression_space(parser);
        op = *parser->cursor;
        if (op != '*' && op != '/' && op != '%') {
            break;
        }
        ++parser->cursor;
        if (!parse_absolute_unary(parser, &rhs)) {
            return false;
        }
        if (op == '*') {
            if (!checked_mul_absolute_i64(result, rhs, &next)) {
                return false;
            }
        } else {
            if (rhs == 0 || (result == INT64_MIN && rhs == -1)) {
                return false;
            }
            next = op == '/' ? result / rhs : result % rhs;
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
        char op;
        int64_t rhs;
        int64_t next;

        skip_absolute_expression_space(parser);
        op = *parser->cursor;
        if (op != '+' && op != '-') {
            break;
        }
        ++parser->cursor;
        if (!parse_absolute_mul(parser, &rhs)) {
            return false;
        }
        if (op == '+') {
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
        bool left;
        int64_t shift;

        skip_absolute_expression_space(parser);
        if (strncmp(parser->cursor, "<<", 2U) == 0) {
            left = true;
        } else if (strncmp(parser->cursor, ">>", 2U) == 0) {
            left = false;
        } else {
            break;
        }
        parser->cursor += 2;
        if (!parse_absolute_sum(parser, &shift) || shift < 0 || shift >= 63) {
            return false;
        }
        if (left) {
            /* GNU GAS evaluates signed negative shifts as two's-complement
             * bit operations. C signed << of a negative value is undefined,
             * and rejecting it breaks real Linux extable_type_reg words:
             *   (17 | ((-14) << 16)) + (.Lregnr << 8)
             * Use the canonical unsigned bit pattern, then interpret as
             * signed (same convention as the shared numeric evaluator). */
            uint64_t bits=(uint64_t)result << (unsigned int)shift;
            result=(int64_t)bits;
        } else {
            /* Avoid implementation-dependent negative signed right shift.
             * GNU's >> is arithmetic, explicitly preserving sign bits. */
            unsigned count=(unsigned)shift;
            if(result<0 && count!=0U) {
                uint64_t bits=(uint64_t)result;
                bits=(bits>>count)|(UINT64_MAX<<(64U-count));
                result=(int64_t)bits;
            } else result>>=count;
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
        bool lt,gt,inclusive;
        int64_t rhs;
        skip_absolute_expression_space(parser);
        if((parser->cursor[0]!='<' && parser->cursor[0]!='>') ||
           parser->cursor[1]==parser->cursor[0])break;
        lt=parser->cursor[0]=='<';gt=!lt;
        inclusive=parser->cursor[1]=='=';
        parser->cursor+=inclusive?2U:1U;
        if(!parse_absolute_shift(parser,&rhs))return false;
        lhs=((lt && (inclusive?lhs<=rhs:lhs<rhs)) ||
             (gt && (inclusive?lhs>=rhs:lhs>rhs)))?-1:0;
    }
    *value=lhs;return true;
}

static bool parse_absolute_equality(MiniAsAbsoluteExpressionParser *parser,
                                    int64_t *value) {
    int64_t lhs;
    if(!parse_absolute_relational(parser,&lhs))return false;
    for(;;) {
        bool different;
        int64_t rhs;
        skip_absolute_expression_space(parser);
        if(strncmp(parser->cursor,"==",2U)==0)different=false;
        else if(strncmp(parser->cursor,"!=",2U)==0)different=true;
        else break;
        parser->cursor+=2U;
        if(!parse_absolute_relational(parser,&rhs))return false;
        lhs=((lhs!=rhs)==different)?-1:0;
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
        int64_t rhs;

        skip_absolute_expression_space(parser);
        if (*parser->cursor != '&' || parser->cursor[1] == '&') {
            break;
        }
        ++parser->cursor;
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
        int64_t rhs;

        skip_absolute_expression_space(parser);
        if (*parser->cursor != '^') {
            break;
        }
        ++parser->cursor;
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
        int64_t rhs;

        skip_absolute_expression_space(parser);
        if (*parser->cursor != '|' || parser->cursor[1] == '|') {
            break;
        }
        ++parser->cursor;
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
        int64_t rhs;
        skip_absolute_expression_space(parser);
        if(strncmp(parser->cursor,"&&",2U))break;
        parser->cursor+=2U;
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
        int64_t rhs;
        skip_absolute_expression_space(parser);
        if(strncmp(parser->cursor,"||",2U))break;
        parser->cursor+=2U;
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
    if(as->expr_dot_override)dot=as->expr_dot_value;
    else if(!minias_current_location(as,&dot))return false;
    if(dot>(uint64_t)INT64_MAX)return false;
    parser.as = as;
    parser.cursor = text;
    parser.dot = (int64_t)dot;
    parser.only_absolute_symbols = true;
    if (!parse_absolute_logical_or(&parser, value)) {
        return false;
    }
    skip_absolute_expression_space(&parser);
    return *parser.cursor == '\0';
}

bool minias_evaluate_absolute_operand(MiniAs *as,const char *text,
                                      int64_t *value) {
    return text && value && minias_evaluate_absolute_expression(as,text,value);
}

/* GNU GAS accepts:
 *   b external + (1b - 0b + (2 * 4))
 * after Common numeric-label rewriting.  Only the local-label subtraction
 * is folded: the base symbol remains a typed relocation with an exact
 * addend, not an inferred displacement or a parallel Target evaluator.
 * Both labels must ALREADY be defined in the SAME subsection, where
 * their difference is invariant under later section layout. */
/* A GNU expression such as (identity_mapped - relocate_kernel) is
 * absolute only AFTER both label anchors exist and subsection layout is
 * finalized. Preserve the two typed symbols throughout parse and measure:
 * parsing a forward symbol as zero or inventing a relocation is wrong.
 * Common owns syntax and final same-section semantics for all targets. */
bool minias_parse_symbol_difference(const char *expression,
                                    MiniAsSymbolDifference *out) {
    char *copy,*value,*minus;
    size_t n,i;
    unsigned level;
    bool wrapping;
    bool ok=false;
    if(!expression || !out)return false;
    copy=minias_strdup(expression);
    if(!copy)return false;
    value=minias_trim(copy);
    /* Strip whole-expression parentheses, never partial subexpressions. */
    for(;;) {
        n=strlen(value);
        if(n<3U || value[0]!='(' || value[n-1U]!=')')break;
        level=0U;wrapping=true;
        for(i=0U;i<n;++i) {
            if(value[i]=='(')++level;
            else if(value[i]==')') {
                if(level==0U){wrapping=false;break;}
                --level;
                if(level==0U && i!=n-1U){wrapping=false;break;}
            }
        }
        if(!wrapping || level!=0U)break;
        value[n-1U]='\0';
        value=minias_trim(value+1U);
    }
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

bool minias_resolve_symbol_difference(MiniAs *as,
                                      const MiniAsSymbolDifference *pair,
                                      int64_t *value) {
    const MiniAsSymbol *positive,*negative;
    int64_t first,second,delta;
    if(!as || !pair || !value)return false;
    positive=minias_get_symbol(as,pair->positive.name,false);
    negative=minias_get_symbol(as,pair->negative.name,false);
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

bool minias_fold_local_difference_addend(MiniAs *as,const char *expression,
                                         MiniAsSymbolExpr *out) {
    char *copy=minias_strdup(expression),*plus,*group,*minus,*local_extra;
    MiniAsSymbolExpr base,left,right;
    MiniAsSymbol *l,*r;
    int64_t diff,add=0,final;
    bool ok=false;
    size_t n;
    if(!copy)return false;
    plus=strchr(copy,'+');
    if(!plus)goto done;
    *plus++='\0';
    if(!minias_parse_symbol_addend(minias_trim(copy),&base))goto done;
    group=minias_trim(plus);
    n=strlen(group);
    if(n<5U || group[0]!='(' || group[n-1U]!=')')goto done;
    group[n-1U]='\0';
    group=minias_trim(group+1U);
    minus=strchr(group,'-');
    if(!minus)goto done;
    *minus++='\0';
    if(!minias_parse_symbol_addend(minias_trim(group),&left) ||
       left.addend!=0)goto done;
    local_extra=strchr(minus,'+');
    if(local_extra) {
        *local_extra++='\0';
        if(!minias_evaluate_absolute_expression(as,minias_trim(local_extra),&add))
            goto done;
    }
    if(!minias_parse_symbol_addend(minias_trim(minus),&right) ||
       right.addend!=0)goto done;
    l=minias_get_symbol(as,left.name,false);
    r=minias_get_symbol(as,right.name,false);
    if(!l || !r || !l->defined || !r->defined ||
       l->section<0 || l->section!=r->section ||
       l->subsection!=r->subsection ||
       l->value>INT64_MAX || r->value>INT64_MAX)goto done;
    diff=(int64_t)l->value-(int64_t)r->value;
    if((add>0 && diff>INT64_MAX-add) ||
       (add<0 && diff<INT64_MIN-add))goto done;
    diff+=add;
    if((diff>0 && base.addend>INT64_MAX-diff) ||
       (diff<0 && base.addend<INT64_MIN-diff))goto done;
    final=base.addend+diff;
    *out=base;
    out->addend=final;
    as->layout_sensitive=true;
    ok=true;
done:
    free(copy);
    return ok;
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
    if(as->expr_dot_override)dot=as->expr_dot_value;
    else if(!minias_current_location(as,&dot))return false;
    if(dot>(uint64_t)INT64_MAX)return false;
    parser.as = as;
    parser.cursor = text;
    parser.dot = (int64_t)dot;
    parser.only_absolute_symbols = true;
    if (!parse_absolute_logical_or(&parser, &signed_value)) {
        return false;
    }
    skip_absolute_expression_space(&parser);
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

/* A relocation-free constant from two earlier labels in the SAME
 * subsection, plus an absolute arithmetic tail. No source-specific labels,
 * guessed addresses, Target grammar, or cross-section cancellation. */
bool minias_evaluate_same_subsection_difference(MiniAs *as,
                                                 const char *expression,
                                                 int64_t *value) {
    char *copy,*body,*p,*suffix=NULL;
    MiniAsSymbolDifference pair;
    MiniAsSymbol *lhs,*rhs;
    unsigned depth=0U;
    int64_t delta,extra=0;
    bool ok=false;
    if(!as || !expression || !value)return false;
    copy=minias_strdup(expression);
    if(!copy)return false;
    body=minias_expr_strip_outer_parens(copy);
    for(p=body;*p;++p) {
        if(*p=='('){++depth;continue;}
        if(*p==')'){
            if(depth==0U)goto done;
            --depth;continue;
        }
        /* Split only the final absolute addition AFTER the label delta.
         * Inner (2*4) additions remain part of the Common expression. */
        if(depth==0U && *p=='+' && strchr(body,'-') &&
           (size_t)(p-body)>(size_t)(strchr(body,'-')-body))suffix=p;
    }
    if(depth!=0U)goto done;
    if(suffix) {
        *suffix++='\0';
        if(!minias_evaluate_absolute_expression(as,minias_trim(suffix),&extra))
            goto done;
    }
    if(!minias_parse_symbol_difference(body,&pair))goto done;
    lhs=minias_get_symbol(as,pair.positive.name,false);
    rhs=minias_get_symbol(as,pair.negative.name,false);
    if(!lhs || !rhs || !lhs->defined || !rhs->defined ||
       lhs->section<0 || lhs->section!=rhs->section ||
       lhs->subsection!=rhs->subsection ||
       !minias_resolve_symbol_difference(as,&pair,&delta) ||
       !minias_checked_add_i64(delta,extra,value))goto done;
    ok=true;
done:
    free(copy);
    return ok;
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
        /* Symbolic absolute expressions are intentionally recomputed after
         * layout. Numeric-only constants are frozen as immediates now. */
        if(minias_eval_u64_expression(token,&value)) {
            out->kind=MINIAS_MC_EXPR_CONSTANT;
            out->constant=value;
            matched=true;
        } else if(minias_parse_absolute_data_bits(as,token,&value)) {
            out->kind=MINIAS_MC_EXPR_DEFERRED_ABSOLUTE;
            out->deferred_text=minias_strdup(token);
            matched=out->deferred_text!=NULL;
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
                   minias_expr_parse_symbol_absolute_tail(as,token,&lhs))) {
            out->kind=MINIAS_MC_EXPR_SYMBOL;
            matched=true;
        } else {
            minias_set_error(as,"unsupported-mc-expression:%s:line=%zu",
                             token,stmt->line);
        }
        if(matched && out->kind!=MINIAS_MC_EXPR_CONSTANT &&
           out->kind!=MINIAS_MC_EXPR_DEFERRED_ABSOLUTE) {
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

static bool emit_symbol_minus_dot(MiniAs *as,
                                  const MiniAsStmt *stmt,
                                  unsigned int width,
                                  uint64_t relocation_offset,
                                  const MiniAsSymbolExpr *expr) {
    MiniAsSymbol *target = minias_get_symbol(as, expr->name, false);
    uint32_t add_type = as->target->fixup_relocation_type(MINIAS_FIXUP_ADD, width);
    uint32_t sub_type = as->target->fixup_relocation_type(MINIAS_FIXUP_SUB, width);

    if (target != NULL && target->defined && target->section == stmt->section) {
        int64_t difference =
            (int64_t)target->value + expr->addend - (int64_t)relocation_offset;
        uint64_t value = (uint64_t)difference;
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
        MiniAsFixupKind kind=width==4U?
            as->target->dot_relative_fixup:MINIAS_FIXUP_PREL64;
        if((width==4U || width==8U) &&
           kind!=MINIAS_FIXUP_NONE &&
           as->target->fixup_relocation_type(kind,width)!=0U)
            return minias_section_append_zero(as,stmt->section,width) &&
                   minias_add_fixup(as,stmt->section,relocation_offset,
                                    kind,width,expr->name,expr->addend);
    }

    if (add_type == 0U || sub_type == 0U) {
        minias_set_error(as,
                         "unsupported-symbol-difference-width:%u:line=%zu",
                         width,
                         stmt->line);
        return false;
    }

    {
        char anchor_name[96];
        int written;
        MiniAsSymbol *anchor;

        written = snprintf(anchor_name,
                           sizeof(anchor_name),
                           ".Lminias_expr_%zu",
                           ++as->expr_anchor_counter);
        if (written < 0 || (size_t)written >= sizeof(anchor_name)) {
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

        return minias_section_append_zero(as, stmt->section, width) &&
               minias_add_fixup(as,
                                     stmt->section,
                                     relocation_offset,
                                     MINIAS_FIXUP_ADD,
                                     width,
                                     expr->name,
                                     expr->addend) &&
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
    const MiniAsSymbolExpr *expr,
    uint64_t divisor) {
    MiniAsSymbol *target = minias_get_symbol(as, expr->name, false);
    int64_t difference;
    int64_t quotient;
    uint64_t bits;
    unsigned char bytes[8];
    unsigned int i;

    if (divisor == 0U || divisor > (uint64_t)INT64_MAX ||
        target == NULL || !target->defined ||
        target->section != stmt->section ||
        target->subsection != stmt->subsection ||
        target->value > (uint64_t)INT64_MAX ||
        relocation_offset > (uint64_t)INT64_MAX) {
        minias_set_error(as,
                         "unsupported-divided-symbol-difference:%s/%llu:line=%zu",
                         expr->name,
                         (unsigned long long)divisor,
                         stmt->line);
        return false;
    }
    difference = (int64_t)target->value + expr->addend -
                 (int64_t)relocation_offset;
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
                                   const MiniAsSymbolExpr *lhs_expr,
                                   const MiniAsSymbolExpr *rhs_expr) {
    MiniAsSymbol *lhs = minias_get_symbol(as, lhs_expr->name, false);
    MiniAsSymbol *rhs = minias_get_symbol(as, rhs_expr->name, false);
    uint32_t add_type = as->target->fixup_relocation_type(MINIAS_FIXUP_ADD, width);
    uint32_t sub_type = as->target->fixup_relocation_type(MINIAS_FIXUP_SUB, width);

    if (lhs != NULL && rhs != NULL && lhs->defined && rhs->defined &&
        /* If both names resolve to the same real section, its load
         * address cancels. GNU GAS resolves GLOBAL, WEAK and local names
         * alike, including when the data value belongs to a different
         * output section. Never fold SHN_COMMON/UNDEF or cross-section
         * operands: they are not absolute without linker semantics.
         * Keep target ADD/SUB pairs on RISC-V/LoongArch, where relocation
         * contracts are required by kernel .alternative patching. */
        lhs->section == rhs->section &&
        (lhs->section >= 0 || lhs->section == MINIAS_SECTION_ABS) &&
        (add_type == 0U || sub_type == 0U)) {
        /* Modular arithmetic gives correct low 8/16/32/64 bits and avoids
         * undefined signed overflow for large absolute/forward symbols. */
        uint64_t value=(lhs->value+(uint64_t)lhs_expr->addend)-
                       (rhs->value+(uint64_t)rhs_expr->addend);
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
    if((width==4U || width==8U) &&
       (add_type==0U || sub_type==0U) &&
       rhs!=NULL && rhs->defined &&
       /* GNU GAS resolves defined GLOBAL/WEAK anchors to their section
        * positions for PC64 as well as LOCAL anchors. This binding
        * behavior is the same on x86-64 and AArch64. */
       rhs->section==stmt->section) {
        MiniAsFixupKind kind=width==4U?
            as->target->dot_relative_fixup:MINIAS_FIXUP_PREL64;
        if(kind!=MINIAS_FIXUP_NONE &&
           as->target->fixup_relocation_type(kind,width)!=0U) {
            uint64_t delta=relocation_offset-rhs->value+
                           (uint64_t)lhs_expr->addend-
                           (uint64_t)rhs_expr->addend;
            return minias_section_append_zero(as,stmt->section,width) &&
                   minias_add_fixup(as,stmt->section,relocation_offset,
                                    kind,width,lhs_expr->name,(int64_t)delta);
        }
    }
    if (add_type == 0U || sub_type == 0U) {
        minias_set_error(as,
                         "unsupported-symbol-difference-width:%u:line=%zu",
                         width,
                         stmt->line);
        return false;
    }
    return minias_section_append_zero(as, stmt->section, width) &&
           minias_add_fixup(as,
                                 stmt->section,
                                 relocation_offset,
                                 MINIAS_FIXUP_ADD,
                                 width,
                                 lhs_expr->name,
                                 lhs_expr->addend) &&
           minias_add_fixup(as,
                                 stmt->section,
                                 relocation_offset,
                                 MINIAS_FIXUP_SUB,
                                 width,
                                 rhs_expr->name,
                                 rhs_expr->addend);
}

static bool emit_symbol_difference_divided(MiniAs *as,
        const MiniAsStmt *stmt,unsigned int width,
        const MiniAsSymbolExpr *lhs_expr,
        const MiniAsSymbolExpr *rhs_expr,uint64_t divisor,
        bool arithmetic_shift) {
    MiniAsSymbol *lhs=minias_get_symbol(as,lhs_expr->name,false);
    MiniAsSymbol *rhs=minias_get_symbol(as,rhs_expr->name,false);
    int64_t left,right,diff,quotient;
    unsigned char bytes[8];
    unsigned i;
    /* A cross-section difference cannot be divided using an ELF fixup.
     * Keep the operation fail-closed rather than fabricating a value. */
    if(!lhs || !rhs || !lhs->defined || !rhs->defined ||
       lhs->section!=rhs->section ||
       (lhs->section<0 && lhs->section!=MINIAS_SECTION_ABS) ||
       lhs->value>(uint64_t)INT64_MAX ||
       rhs->value>(uint64_t)INT64_MAX ||
       divisor==0U || divisor>(uint64_t)INT64_MAX ||
       !minias_checked_add_i64((int64_t)lhs->value,lhs_expr->addend,&left) ||
       !minias_checked_add_i64((int64_t)rhs->value,rhs_expr->addend,&right) ||
       !checked_sub_i64(left,right,&diff)) {
        minias_set_error(as,"unsupported-divided-symbol-pair:line=%zu",
                         stmt->line);
        return false;
    }
    if(arithmetic_shift) {
        uint64_t shifted=(uint64_t)diff/divisor;
        if(width<8U &&
           shifted>((UINT64_C(1)<<(width*8U))-1U)) {
            minias_set_error(as,
                             "shifted-symbol-pair-field-overflow:line=%zu",
                             stmt->line);
            return false;
        }
        quotient=(int64_t)shifted;
    }else quotient=diff/(int64_t)divisor;
    for(i=0U;i<width;++i)
        bytes[i]=(unsigned char)((uint64_t)quotient>>(8U*i));
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
    for(i=0U;i<data->count;++i) {
        const MiniAsMCExpr *x=&data->fields[i];
        MiniAsSymbolExpr lhs={0},rhs={0};
        uint64_t value=x->constant;
        uint64_t at=(uint64_t)as->sections[(size_t)stmt->section].size;
        bool literal=x->kind==MINIAS_MC_EXPR_CONSTANT;
        unsigned j;
        unsigned char bytes[8];
        if(x->kind==MINIAS_MC_EXPR_DEFERRED_ABSOLUTE) {
            as->expr_dot_override=true;
            as->expr_dot_value=at;
            literal=x->deferred_text!=NULL &&
                    minias_parse_absolute_data_bits(as,x->deferred_text,&value);
            as->expr_dot_override=false;
            if(!literal) {
                minias_set_error(as,"unresolved-mc-absolute:line=%zu",
                                 stmt->line);
                return false;
            }
        }
        if(x->has_lhs) {
            const MiniAsSymbol *symbol;
            size_t len;
            if(x->lhs_symbol_index>=as->symbol_count) {
                minias_set_error(as,"mc-lhs-symbol-out-of-range:line=%zu",
                                 stmt->line);
                return false;
            }
            symbol=&as->symbols[x->lhs_symbol_index];
            len=strlen(symbol->name);
            if(len>=sizeof(lhs.name)) {
                minias_set_error(as,"mc-lhs-symbol-name-too-long");
                return false;
            }
            memcpy(lhs.name,symbol->name,len+1U);
            lhs.addend=x->lhs_addend;
        }
        if(x->has_rhs) {
            const MiniAsSymbol *symbol;
            size_t len;
            if(x->rhs_symbol_index>=as->symbol_count) {
                minias_set_error(as,"mc-rhs-symbol-out-of-range:line=%zu",
                                 stmt->line);
                return false;
            }
            symbol=&as->symbols[x->rhs_symbol_index];
            len=strlen(symbol->name);
            if(len>=sizeof(rhs.name)) {
                minias_set_error(as,"mc-rhs-symbol-name-too-long");
                return false;
            }
            memcpy(rhs.name,symbol->name,len+1U);
            rhs.addend=x->rhs_addend;
        }
        if(x->kind==MINIAS_MC_EXPR_SYMBOL) {
            MiniAsSymbol *sym=x->has_lhs ?
                &as->symbols[x->lhs_symbol_index] : NULL;
            if(sym && sym->defined && sym->section==MINIAS_SECTION_ABS) {
                literal=true;
                value=sym->value+(uint64_t)lhs.addend;
            } else {
                /* GNU permits a forward .short absolute .set/.equ symbol,
                 * but no target-independent ABS16 unresolved reference. */
                if(data->width!=4U && data->width!=8U) {
                    minias_set_error(as,"unsupported-symbol-width:%u:line=%zu",
                                     data->width,stmt->line);
                    return false;
                }
                if(!minias_section_append_zero(as,stmt->section,data->width) ||
                   !minias_add_fixup(as,stmt->section,at,
                          MINIAS_FIXUP_ABS,data->width,lhs.name,lhs.addend))
                    return false;
                continue;
            }
        } else if(x->kind==MINIAS_MC_EXPR_SYMBOL_MINUS_DOT) {
            if(!emit_symbol_minus_dot(as,stmt,data->width,at,&lhs))return false;
            continue;
        } else if(x->kind==MINIAS_MC_EXPR_SYMBOL_MINUS_DOT_DIV) {
            if(!emit_symbol_minus_dot_divided(as,stmt,data->width,at,
                                              &lhs,x->divisor))return false;
            continue;
        } else if(x->kind==MINIAS_MC_EXPR_DOT_MINUS_SYMBOL) {
            MiniAsSymbol *origin=x->has_rhs ?
                &as->symbols[x->rhs_symbol_index] : NULL;
            if(origin==NULL || !origin->defined ||
               origin->section!=stmt->section) {
                minias_set_error(as,"unresolved-mc-dot-difference:line=%zu",
                                 stmt->line);
                return false;
            }
            literal=true;
            /* '.' has no SymbolID: its lexical addend lives in MCExpr.
             * The symbol resolver must not drop it when constructing a
             * temporary MiniAsSymbolExpr for ordinary symbol operands. */
            value=at+(uint64_t)x->lhs_addend-
                  (origin->value+(uint64_t)x->rhs_addend);
        } else if(x->kind==MINIAS_MC_EXPR_DIFFERENCE ||
                  x->kind==MINIAS_MC_EXPR_DIFFERENCE_DIV) {
            MiniAsSymbol *left=x->has_lhs ?
                &as->symbols[x->lhs_symbol_index] : NULL;
            MiniAsSymbol *right=x->has_rhs ?
                &as->symbols[x->rhs_symbol_index] : NULL;
            if(x->kind==MINIAS_MC_EXPR_DIFFERENCE &&
               (as->target->fixup_relocation_type(MINIAS_FIXUP_ADD,
                                                  data->width)==0U ||
                as->target->fixup_relocation_type(MINIAS_FIXUP_SUB,
                                                  data->width)==0U) &&
               left && right && left->defined && right->defined &&
               left->section==right->section &&
               (left->section==MINIAS_SECTION_ABS ||
                (left->section>=0 && left->subsection==right->subsection))) {
                literal=true;
                value=(left->value+(uint64_t)lhs.addend)-
                      (right->value+(uint64_t)rhs.addend);
            }else if(x->kind==MINIAS_MC_EXPR_DIFFERENCE_DIV) {
                if(!emit_symbol_difference_divided(as,stmt,data->width,
                           &lhs,&rhs,x->divisor,x->logical_shift))return false;
                continue;
            }else {
                if(!emit_symbol_difference(as,stmt,data->width,at,
                                           &lhs,&rhs))return false;
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

/* MCData has sole ownership of its parsed operands and deferred strings. */
void minias_mcdata_destroy(MiniAsMCData *data) {
    size_t i;
    if(data==NULL)return;
    for(i=0U;i<data->count;++i)free(data->fields[i].deferred_text);
    free(data->fields);
    free(data);
}
