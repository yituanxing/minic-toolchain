#include "minias_internal.h"
#include "expression_grammar.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

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

bool minias_expr_symbol_start(char ch) {
    return (unsigned char)ch==1U || ch == '.' || ch == '$' ||
           ch == '_' || isalpha((unsigned char)ch);
}

bool minias_expr_symbol_continue(char ch) {
    return minias_expr_symbol_start(ch) || isdigit((unsigned char)ch);
}

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

typedef struct MiniAsSyntaxParser {
    MiniAsExprSyntax *syntax;
    const char *cursor;
    unsigned depth;
} MiniAsSyntaxParser;

static bool syntax_add(MiniAsSyntaxParser *parser,
                       MiniAsExprNode node,size_t *id) {
    MiniAsExprSyntax *syntax=parser->syntax;
    if(syntax->count>=512U)return false;
    if(syntax->count==syntax->capacity) {
        size_t capacity=syntax->capacity*2U;
        MiniAsExprNode *grown;
        if(capacity>512U)capacity=512U;
        if(syntax->nodes==syntax->inline_nodes) {
            grown=malloc(capacity*sizeof(*grown));
            if(grown)memcpy(grown,syntax->inline_nodes,
                            syntax->count*sizeof(*grown));
        }else {
            grown=realloc(syntax->nodes,capacity*sizeof(*grown));
        }
        if(!grown)return false;
        syntax->nodes=grown;
        syntax->capacity=capacity;
    }
    *id=syntax->count++;
    syntax->nodes[*id]=node;
    return true;
}

static bool syntax_parse_binary(MiniAsSyntaxParser *parser,
                                unsigned minimum,size_t *result);

static bool syntax_parse_prefix(MiniAsSyntaxParser *parser,size_t *result) {
    MiniAsExprNode node={0};
    const char *start;
    char *end=NULL;
    size_t operand;
    if(++parser->depth>96U)return false;
    minias_expr_skip_space(&parser->cursor);
    if(*parser->cursor=='(') {
        ++parser->cursor;
        if(!syntax_parse_binary(parser,1U,result))return false;
        minias_expr_skip_space(&parser->cursor);
        if(*parser->cursor!=')')return false;
        ++parser->cursor;
        --parser->depth;
        return true;
    }
    if(*parser->cursor=='+' || *parser->cursor=='-' ||
       *parser->cursor=='~' || *parser->cursor=='!') {
        char ch=*parser->cursor++;
        if(!syntax_parse_prefix(parser,&operand))return false;
        if(ch=='+') {*result=operand;--parser->depth;return true;}
        node.kind=MINIAS_EXPR_UNARY;
        node.unary=ch;node.left=operand;
        --parser->depth;
        return syntax_add(parser,node,result);
    }
    if(*parser->cursor=='.' &&
       !minias_expr_symbol_continue(parser->cursor[1])) {
        ++parser->cursor;--parser->depth;
        node.kind=MINIAS_EXPR_DOT;
        return syntax_add(parser,node,result);
    }
    if(minias_expr_symbol_start(*parser->cursor)) {
        start=parser->cursor++;
        while(minias_expr_symbol_continue(*parser->cursor))
            ++parser->cursor;
        node.length=(size_t)(parser->cursor-start);
        if(node.length==0U || node.length>=256U)return false;
        node.start=(size_t)(start-parser->syntax->text);
        node.kind=MINIAS_EXPR_SYMBOL;
        --parser->depth;
        return syntax_add(parser,node,result);
    }
    if(!minias_parse_number_literal(parser->cursor,&end,&node.literal))
        return false;
    parser->cursor=end;
    node.kind=MINIAS_EXPR_LITERAL;
    --parser->depth;
    return syntax_add(parser,node,result);
}

static bool syntax_parse_binary(MiniAsSyntaxParser *parser,
                                unsigned minimum,size_t *result) {
    size_t lhs,rhs;
    if(!syntax_parse_prefix(parser,&lhs))return false;
    for(;;) {
        MiniAsExprBinary token;
        MiniAsExprNode node={0};
        minias_expr_skip_space(&parser->cursor);
        if(!minias_expr_scan_binary(parser->cursor,&token) ||
           token.precedence<minimum)break;
        parser->cursor+=token.width;
        if(!syntax_parse_binary(parser,token.precedence+1U,&rhs))
            return false;
        node.kind=MINIAS_EXPR_BINARY;
        node.op=token.op;
        node.left=lhs;node.right=rhs;
        if(!syntax_add(parser,node,&lhs))return false;
    }
    *result=lhs;
    return true;
}

void minias_expr_syntax_destroy(MiniAsExprSyntax *syntax) {
    if(!syntax)return;
    if(syntax->nodes!=syntax->inline_nodes)free(syntax->nodes);
    memset(syntax,0,sizeof(*syntax));
}

/* The one GNU expression grammar. Syntax retains neither symbol ownership
 * nor evaluation policy: consumers bind names and evaluate after parse. */
bool minias_expr_parse_syntax(const char *text,MiniAsExprSyntax *out) {
    MiniAsSyntaxParser parser;
    if(!text || !out)return false;
    memset(out,0,sizeof(*out));
    out->text=text;
    out->nodes=out->inline_nodes;
    out->capacity=sizeof(out->inline_nodes)/sizeof(out->inline_nodes[0]);
    parser.syntax=out;
    parser.cursor=text;
    parser.depth=0U;
    if(!syntax_parse_binary(&parser,1U,&out->root))goto invalid;
    minias_expr_skip_space(&parser.cursor);
    if(*parser.cursor!='\0')goto invalid;
    return true;
invalid:
    minias_expr_syntax_destroy(out);
    return false;
}

/* Target integer operands wrap to 64 bits. Their arithmetic is intentionally
 * distinct from checked Common absolute expressions and deferred relocation
 * algebra, but uses exactly the same syntax tree. */
bool minias_expr_eval_integer(const MiniAsExprSyntax *syntax,
                              bool signed_shift,uint64_t *result) {
    uint64_t values[512];
    size_t i;
    if(!syntax || !result || !syntax->count ||
       syntax->count>512U)return false;
    for(i=0U;i<syntax->count;++i) {
        const MiniAsExprNode *node=&syntax->nodes[i];
        uint64_t lhs=0U,rhs=0U;
        if(node->left<i)lhs=values[node->left];
        if(node->right<i)rhs=values[node->right];
        switch(node->kind) {
        case MINIAS_EXPR_LITERAL:values[i]=node->literal;break;
        case MINIAS_EXPR_SYMBOL:
        case MINIAS_EXPR_DOT:return false;
        case MINIAS_EXPR_UNARY:
            switch(node->unary) {
            case '-':values[i]=0U-lhs;break;
            case '~':values[i]=~lhs;break;
            case '!':values[i]=lhs==0U?1U:0U;break;
            default:return false;
            }
            break;
        case MINIAS_EXPR_BINARY:
            switch(node->op) {
            case MINIAS_EXPR_OP_MUL:values[i]=lhs*rhs;break;
            case MINIAS_EXPR_OP_DIV:
                if(!rhs)return false;
                values[i]=lhs/rhs;break;
            case MINIAS_EXPR_OP_MOD:
                if(!rhs)return false;
                values[i]=lhs%rhs;break;
            case MINIAS_EXPR_OP_ADD:values[i]=lhs+rhs;break;
            case MINIAS_EXPR_OP_SUB:values[i]=lhs-rhs;break;
            case MINIAS_EXPR_OP_SHL:
                if(rhs>=64U)return false;
                values[i]=lhs<<(unsigned)rhs;break;
            case MINIAS_EXPR_OP_SHR:
                if(rhs>=64U)return false;
                if(signed_shift && rhs!=0U &&
                   (lhs & (UINT64_C(1)<<63U))) {
                    values[i]=(lhs>>(unsigned)rhs) |
                              (~UINT64_C(0)<<(unsigned)(64U-rhs));
                }else values[i]=lhs>>(unsigned)rhs;
                break;
            case MINIAS_EXPR_OP_AND:values[i]=lhs&rhs;break;
            case MINIAS_EXPR_OP_XOR:values[i]=lhs^rhs;break;
            case MINIAS_EXPR_OP_OR:values[i]=lhs|rhs;break;
            default:return false;
            }
            break;
        default:return false;
        }
    }
    *result=values[syntax->root];
    return true;
}

static bool eval_integer_text(const char *text,bool signed_shift,
                              uint64_t *result) {
    MiniAsExprSyntax syntax;
    bool valid;
    if(!minias_expr_parse_syntax(text,&syntax))return false;
    valid=minias_expr_eval_integer(&syntax,signed_shift,result);
    minias_expr_syntax_destroy(&syntax);
    return valid;
}

bool minias_eval_u64_expression(const char *text,uint64_t *value) {
    return value && eval_integer_text(text,false,value);
}

bool minias_eval_i64_expression(const char *text,int64_t *value) {
    uint64_t bits;
    if(!value || !eval_integer_text(text,true,&bits))return false;
    *value=(int64_t)bits;
    return true;
}

static bool parse_symbol_addend_inner(const char *text,
                                     MiniAsSymbolExpr *expr,
                                     unsigned depth) {
    const char *p;
    const char *start;
    size_t length;
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
        /* Parse the sign as part of the entire arithmetic suffix.  In
         * `symbol - BASE + FLAGS`, negating (BASE + FLAGS) instead would
         * silently change +FLAGS into -FLAGS.  Linux x86's initial page
         * tables use this exact GNU expression and need the Present bit. */
        if (!minias_eval_u64_expression(p, &addend_bits)) {
            return false;
        }
        p += strlen(p);
    }
    if (*p != '\0') {
        return false;
    }
    expr->addend = (int64_t)addend_bits;
    return true;
}

bool minias_parse_symbol_addend(const char *text, MiniAsSymbolExpr *expr) {
    return parse_symbol_addend_inner(text,expr,0U);
}
