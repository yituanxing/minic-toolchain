#include "minias_internal.h"
#include "expression_grammar.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

typedef struct MiniAsAddendParser {
    const char *cursor;
    bool signed_shift;
} MiniAsAddendParser;

typedef struct MiniAsExprOperatorSpec {
    const char *text;
    MiniAsExprBinaryOp op;
    unsigned precedence;
    unsigned width;
} MiniAsExprOperatorSpec;

/* Single longest-token-first GNU operator table.  No evaluator is allowed
 * to privately reinterpret `<<` as `<`, `&&` as `&`, etc.  The unsigned,
 * checked signed and deferred evaluators select their own supported levels. */
#define EXPR_TOKEN(text,op,precedence) \
    {text,op,precedence,(unsigned)(sizeof(text)-1U)}
static const MiniAsExprOperatorSpec minias_expr_binary_ops[] = {
    EXPR_TOKEN("||",MINIAS_EXPR_OP_LOR,1U),
    EXPR_TOKEN("&&",MINIAS_EXPR_OP_LAND,2U),
    EXPR_TOKEN("==",MINIAS_EXPR_OP_EQ,6U),
    EXPR_TOKEN("!=",MINIAS_EXPR_OP_NE,6U),
    EXPR_TOKEN("<=",MINIAS_EXPR_OP_LE,7U),
    EXPR_TOKEN(">=",MINIAS_EXPR_OP_GE,7U),
    EXPR_TOKEN("<<",MINIAS_EXPR_OP_SHL,8U),
    EXPR_TOKEN(">>",MINIAS_EXPR_OP_SHR,8U),
    EXPR_TOKEN("|",MINIAS_EXPR_OP_OR,3U),
    EXPR_TOKEN("^",MINIAS_EXPR_OP_XOR,4U),
    EXPR_TOKEN("&",MINIAS_EXPR_OP_AND,5U),
    EXPR_TOKEN("<",MINIAS_EXPR_OP_LT,7U),
    EXPR_TOKEN(">",MINIAS_EXPR_OP_GT,7U),
    EXPR_TOKEN("+",MINIAS_EXPR_OP_ADD,9U),
    EXPR_TOKEN("-",MINIAS_EXPR_OP_SUB,9U),
    EXPR_TOKEN("*",MINIAS_EXPR_OP_MUL,10U),
    EXPR_TOKEN("/",MINIAS_EXPR_OP_DIV,10U),
    EXPR_TOKEN("%",MINIAS_EXPR_OP_MOD,10U)
};
#undef EXPR_TOKEN

bool minias_expr_scan_binary(const char *cursor,MiniAsExprBinary *out) {
    size_t i;
    if(!cursor || !out)return false;
    for(i=0U;i<sizeof(minias_expr_binary_ops)/
                sizeof(minias_expr_binary_ops[0]);++i) {
        const MiniAsExprOperatorSpec *spec=&minias_expr_binary_ops[i];
        if(cursor[0]!=spec->text[0] ||
           strncmp(cursor,spec->text,spec->width)!=0)continue;
        out->op=spec->op;
        out->precedence=spec->precedence;
        out->width=spec->width;
        return true;
    }
    return false;
}

/* One canonical integer literal owner for Target ISA immediates and
 * Common layout expressions. C17 strtoull(base=0) is not required to
 * recognize GNU/GCC 0b... binary literals on older CI libc versions. */
bool minias_parse_number_literal(const char *text,char **end,
                                 uint64_t *value) {
    const char *cursor=text;
    uint64_t result;
    char *parsed_end=NULL;
    unsigned suffix_u=0U,suffix_l=0U;
    if(!text || !end || !value)return false;
    if(cursor[0]=='0' && (cursor[1]=='b' || cursor[1]=='B')) {
        cursor+=2U;
        if(*cursor!='0' && *cursor!='1')return false;
        result=0U;
        while(*cursor=='0' || *cursor=='1') {
            unsigned bit=(unsigned)(*cursor-'0');
            if(result>(UINT64_MAX-bit)/2U)return false;
            result=result*2U+bit;
            ++cursor;
        }
    } else {
        unsigned long long parsed;
        errno=0;
        parsed=strtoull(text,&parsed_end,0);
        if(errno || parsed_end==text)return false;
        result=(uint64_t)parsed;
        cursor=parsed_end;
    }
    while(*cursor=='U' || *cursor=='u' ||
          *cursor=='L' || *cursor=='l') {
        if(*cursor=='u' || *cursor=='U')++suffix_u;
        else ++suffix_l;
        ++cursor;
    }
    if(suffix_u>1U || suffix_l>2U)return false;
    *end=(char *)cursor;
    *value=result;
    return true;
}

static bool parse_addend_or(MiniAsAddendParser *parser, uint64_t *value);

static bool parse_addend_primary(MiniAsAddendParser *parser, uint64_t *value) {
    char *end = NULL;
    minias_expr_skip_space(&parser->cursor);
    if (*parser->cursor == '(') {
        ++parser->cursor;
        if (!parse_addend_or(parser, value)) {
            return false;
        }
        minias_expr_skip_space(&parser->cursor);
        if (*parser->cursor != ')') {
            return false;
        }
        ++parser->cursor;
        return true;
    }
    if(!minias_parse_number_literal(parser->cursor,&end,value))
        return false;
    parser->cursor=end;
    return true;
}

static bool parse_addend_unary(MiniAsAddendParser *parser, uint64_t *value) {
    minias_expr_skip_space(&parser->cursor);
    if (*parser->cursor == '+') {
        ++parser->cursor;
        return parse_addend_unary(parser, value);
    }
    if (*parser->cursor == '-') {
        uint64_t operand;
        ++parser->cursor;
        if (!parse_addend_unary(parser, &operand)) {
            return false;
        }
        *value = UINT64_C(0) - operand;
        return true;
    }
    if (*parser->cursor == '!') {
        uint64_t operand;
        ++parser->cursor;
        if(!parse_addend_unary(parser,&operand))return false;
        *value=operand==0U?1U:0U;
        return true;
    }
    if (*parser->cursor == '~') {
        uint64_t operand;
        ++parser->cursor;
        if (!parse_addend_unary(parser, &operand)) {
            return false;
        }
        *value = ~operand;
        return true;
    }
    return parse_addend_primary(parser, value);
}

static bool parse_addend_mul(MiniAsAddendParser *parser, uint64_t *value) {
    uint64_t result;

    if (!parse_addend_unary(parser, &result)) {
        return false;
    }
    for (;;) {
        MiniAsExprBinaryOp op;
        uint64_t rhs;

        if (!minias_expr_take_binary(&parser->cursor,10U,&op)) {
            break;
        }
        if (!parse_addend_unary(parser, &rhs)) {
            return false;
        }
        if (op == MINIAS_EXPR_OP_MUL) {
            result *= rhs;
        } else {
            if (rhs == 0U) {
                return false;
            }
            result = op == MINIAS_EXPR_OP_DIV ? result / rhs : result % rhs;
        }
    }
    *value = result;
    return true;
}

static bool parse_addend_sum(MiniAsAddendParser *parser, uint64_t *value) {
    uint64_t result;

    if (!parse_addend_mul(parser, &result)) {
        return false;
    }
    for (;;) {
        MiniAsExprBinaryOp op;
        uint64_t rhs;

        if (!minias_expr_take_binary(&parser->cursor,9U,&op)) {
            break;
        }
        if (!parse_addend_mul(parser, &rhs)) {
            return false;
        }
        result = op == MINIAS_EXPR_OP_ADD ? result + rhs : result - rhs;
    }
    *value = result;
    return true;
}

static bool parse_addend_shift(MiniAsAddendParser *parser, uint64_t *value) {
    uint64_t result;

    if (!parse_addend_sum(parser, &result)) {
        return false;
    }
    for (;;) {
        MiniAsExprBinaryOp op;
        uint64_t rhs;

        if (!minias_expr_take_binary(&parser->cursor,8U,&op)) {
            break;
        }
        if (!parse_addend_sum(parser, &rhs) || rhs >= 64U) {
            return false;
        }
        if (op == MINIAS_EXPR_OP_SHL) {
            result <<= (unsigned int)rhs;
        } else if (parser->signed_shift && rhs != 0U &&
                   (result & (UINT64_C(1) << 63U)) != 0U) {
            /* GNU signed assembler immediates use arithmetic >>.
             * Make sign extension explicit: signed C >> is not portable. */
            result = (result >> (unsigned int)rhs) |
                     (~UINT64_C(0) << (unsigned int)(64U-rhs));
        } else {
            result >>= (unsigned int)rhs;
        }
    }
    *value = result;
    return true;
}

static bool parse_addend_and(MiniAsAddendParser *parser, uint64_t *value) {
    uint64_t result;

    if (!parse_addend_shift(parser, &result)) {
        return false;
    }
    for (;;) {
        MiniAsExprBinaryOp op;
        uint64_t rhs;
        if (!minias_expr_take_binary(&parser->cursor,5U,&op)) {
            break;
        }
        if (!parse_addend_shift(parser, &rhs)) {
            return false;
        }
        result &= rhs;
    }
    *value = result;
    return true;
}

static bool parse_addend_xor(MiniAsAddendParser *parser, uint64_t *value) {
    uint64_t result;

    if (!parse_addend_and(parser, &result)) {
        return false;
    }
    for (;;) {
        MiniAsExprBinaryOp op;
        uint64_t rhs;
        if (!minias_expr_take_binary(&parser->cursor,4U,&op)) {
            break;
        }
        if (!parse_addend_and(parser, &rhs)) {
            return false;
        }
        result ^= rhs;
    }
    *value = result;
    return true;
}

static bool parse_addend_or(MiniAsAddendParser *parser, uint64_t *value) {
    uint64_t result;

    if (!parse_addend_xor(parser, &result)) {
        return false;
    }
    for (;;) {
        MiniAsExprBinaryOp op;
        uint64_t rhs;
        if (!minias_expr_take_binary(&parser->cursor,3U,&op)) {
            break;
        }
        if (!parse_addend_xor(parser, &rhs)) {
            return false;
        }
        result |= rhs;
    }
    *value = result;
    return true;
}

/* Shared wrapping 64-bit integer-expression grammar for symbolic addends,
 * ISA immediates and machine-specific operands. */
bool minias_eval_u64_expression(const char *text, uint64_t *value) {
    MiniAsAddendParser parser;

    parser.cursor = text;
    parser.signed_shift = false;
    if (!parse_addend_or(&parser, value)) {
        return false;
    }
    minias_expr_skip_space(&parser.cursor);
    return *parser.cursor == '\0';
}

/* Same grammar and precedence as the RV-proven unsigned expression path.
 * Only the interpretation of a negative right-shift is target-selectable.
 * This must NOT replace minias_eval_u64_expression's old semantics. */
bool minias_eval_i64_expression(const char *text, int64_t *value) {
    MiniAsAddendParser parser;
    uint64_t bits;
    if (!text || !value) return false;
    parser.cursor=text;
    parser.signed_shift=true;
    if (!parse_addend_or(&parser,&bits)) return false;
    minias_expr_skip_space(&parser.cursor);
    if (*parser.cursor!='\0') return false;
    *value=(int64_t)bits;
    return true;
}

static bool parse_symbol_addend_inner(const char *text,
                                     MiniAsSymbolExpr *expr,
                                     unsigned depth) {
    const char *p;
    const char *start;
    size_t length;
    int sign = 1;
    uint64_t addend_bits = 0U;

    if (text == NULL || expr == NULL) {
        return false;
    }
    p = text;
    while (*p == ' ' || *p == '\t') {
        ++p;
    }
    /* GNU GAS permits an entire symbol+constant expression enclosed in
     * grouping parentheses. The same semantic Fixup/Addend grammar is
     * used by every target, notably x86_64 addq $(symbol - 0xff...).
     * Require that the outside ')' really matches the first '('; do not
     * strip a grouping used only around one subexpression. */
    if (*p == '(' && depth < 32U) {
        size_t n = strlen(p), i;
        unsigned nested = 0U;
        bool outer = false;
        while(n && isspace((unsigned char)p[n-1U])) --n;
        if(n>=3U && p[n-1U]==')') {
            for(i=0U;i<n;++i) {
                if(p[i]=='(')++nested;
                else if(p[i]==')') {
                    if(nested==0U)break;
                    if(--nested==0U) {
                        outer=i==n-1U;
                        break;
                    }
                }
            }
            if(outer) {
                char *inner=malloc(n-1U);
                bool ok;
                if(!inner)return false;
                memcpy(inner,p+1U,n-2U);
                inner[n-2U]='\0';
                ok=parse_symbol_addend_inner(inner,expr,depth+1U);
                free(inner);
                return ok;
            }
        }
    }
    if (!minias_expr_symbol_start(*p)) {
        return false;
    }
    start = p++;
    while (minias_expr_symbol_continue(*p)) {
        ++p;
    }
    length = (size_t)(p - start);
    if (length == 0U || length >= sizeof(expr->name)) {
        return false;
    }
    memcpy(expr->name, start, length);
    expr->name[length] = '\0';

    while (*p == ' ' || *p == '\t') {
        ++p;
    }
    if (*p == '+' || *p == '-') {
        if (*p == '-') {
            sign = -1;
        }
        ++p;
        while (*p == ' ' || *p == '\t') {
            ++p;
        }
        if (!minias_eval_u64_expression(p, &addend_bits)) {
            return false;
        }
        p += strlen(p);
    }
    if (*p != '\0') {
        return false;
    }
    if (sign < 0) {
        addend_bits = UINT64_C(0) - addend_bits;
    }
    expr->addend = (int64_t)addend_bits;
    return true;
}

bool minias_parse_symbol_addend(const char *text, MiniAsSymbolExpr *expr) {
    return parse_symbol_addend_inner(text,expr,0U);
}
