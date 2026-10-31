#include "minias_internal.h"
#include "expression_grammar.h"

#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* Common MCExpr ownership. GNU expression grammar, absolute operands,
 * symbolic differences, persistent Symbol IDs, and semantic Fixup creation
 * are independent of ISA and ELF e_machine. Target encoders never duplicate
 * these algorithms; the driver owns sections, macros, and fragment layout. */

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

/* Syntax is shared with Target immediates and retained deferred programs.
 * Checked absolute arithmetic and symbol resolution remain Common policy:
 * GNU comparisons yield -1, logical operators yield 1, and signed
 * multiplication/division reject overflow rather than wrapping. */
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

/* Absolute evaluation and relocation-affine lowering must use one checked
 * scalar policy. Relocatable terms may only flow through + and -; a fully
 * absolute subtree retains the exact same arithmetic as the GNU evaluator. */
static bool evaluate_absolute_unary(char op,int64_t value,int64_t *out) {
    switch(op) {
    case '-':if(value==INT64_MIN)return false;*out=-value;return true;
    case '~':*out=~value;return true;
    case '!':*out=value==0?1:0;return true;
    default:return false;
    }
}

static bool evaluate_absolute_binary(MiniAsExprBinaryOp op,
                                      int64_t lhs,int64_t rhs,int64_t *out) {
    switch(op) {
    case MINIAS_EXPR_OP_MUL:
        return checked_mul_absolute_i64(lhs,rhs,out);
    case MINIAS_EXPR_OP_DIV:
    case MINIAS_EXPR_OP_MOD:
        if(rhs==0 || (lhs==INT64_MIN && rhs==-1))return false;
        *out=op==MINIAS_EXPR_OP_DIV?lhs/rhs:lhs%rhs;return true;
    case MINIAS_EXPR_OP_ADD:return minias_checked_add_i64(lhs,rhs,out);
    case MINIAS_EXPR_OP_SUB:return checked_sub_i64(lhs,rhs,out);
    case MINIAS_EXPR_OP_SHL:
    case MINIAS_EXPR_OP_SHR:
        if(rhs<0 || rhs>=64)return false;
        *out=signed_from_bits(op==MINIAS_EXPR_OP_SHL?
              ((uint64_t)lhs<<(unsigned)rhs):
              ((uint64_t)lhs>>(unsigned)rhs));return true;
    case MINIAS_EXPR_OP_LT:*out=lhs<rhs?-1:0;return true;
    case MINIAS_EXPR_OP_LE:*out=lhs<=rhs?-1:0;return true;
    case MINIAS_EXPR_OP_GT:*out=lhs>rhs?-1:0;return true;
    case MINIAS_EXPR_OP_GE:*out=lhs>=rhs?-1:0;return true;
    case MINIAS_EXPR_OP_EQ:*out=lhs==rhs?-1:0;return true;
    case MINIAS_EXPR_OP_NE:*out=lhs!=rhs?-1:0;return true;
    case MINIAS_EXPR_OP_AND:*out=lhs&rhs;return true;
    case MINIAS_EXPR_OP_XOR:*out=lhs^rhs;return true;
    case MINIAS_EXPR_OP_OR:*out=lhs|rhs;return true;
    case MINIAS_EXPR_OP_LAND:*out=lhs!=0 && rhs!=0?1:0;return true;
    case MINIAS_EXPR_OP_LOR:*out=lhs!=0 || rhs!=0?1:0;return true;
    default:return false;
    }
}

static bool evaluate_absolute_syntax(MiniAs *as,
                                     const MiniAsExprSyntax *syntax,
                                     int64_t dot,bool *only_abs,
                                     int64_t *result) {
    int64_t values[512];
    size_t i;
    if(!syntax || !only_abs || !result || !syntax->count ||
       syntax->count>512U)return false;
    *only_abs=true;
    for(i=0U;i<syntax->count;++i) {
        const MiniAsExprNode *node=&syntax->nodes[i];
        int64_t lhs=0,rhs=0,next=0;
        if(node->left<i)lhs=values[node->left];
        if(node->right<i)rhs=values[node->right];
        switch(node->kind) {
        case MINIAS_EXPR_LITERAL:
            next=signed_from_bits(node->literal);
            break;
        case MINIAS_EXPR_DOT:
            *only_abs=false;
            next=dot;
            break;
        case MINIAS_EXPR_SYMBOL: {
            char name[256];
            MiniAsSymbol *symbol;
            if(!node->length || node->length>=sizeof(name))
                return false;
            memcpy(name,syntax->text+node->start,node->length);
            name[node->length]='\0';
            symbol=minias_get_symbol(as,name,false);
            if(!symbol || !symbol->defined ||
               symbol->section==MINIAS_SECTION_COMMON ||
               (symbol->section!=MINIAS_SECTION_ABS &&
                symbol->value>(uint64_t)INT64_MAX))return false;
            if(symbol->section!=MINIAS_SECTION_ABS)*only_abs=false;
            next=signed_from_bits(symbol->value);
            break;
        }
        case MINIAS_EXPR_UNARY:
            if(!evaluate_absolute_unary(node->unary,lhs,&next))return false;
            break;
        case MINIAS_EXPR_BINARY:
            if(!evaluate_absolute_binary(node->op,lhs,rhs,&next))return false;
            break;
        default:return false;
        }
        values[i]=next;
    }
    *result=values[syntax->root];
    return true;
}

bool minias_evaluate_absolute_expression(MiniAs *as,
                                         const char *text,
                                         int64_t *value) {
    MiniAsExprSyntax syntax;
    uint64_t dot,unsigned_value;
    bool only_abs,valid;
    if(!as || !text || !value ||
       !minias_expr_parse_syntax(text,&syntax))return false;
    if(minias_expr_eval_integer(&syntax,false,&unsigned_value) &&
       unsigned_value<=(uint64_t)INT64_MAX) {
        *value=(int64_t)unsigned_value;
        minias_expr_syntax_destroy(&syntax);
        return true;
    }
    valid=minias_current_location(as,&dot) &&
          dot<=(uint64_t)INT64_MAX &&
          evaluate_absolute_syntax(as,&syntax,(int64_t)dot,
                                   &only_abs,value);
    minias_expr_syntax_destroy(&syntax);
    return valid;
}

bool minias_evaluate_absolute_operand(MiniAs *as,const char *text,
                                      int64_t *value) {
    return text && value && minias_evaluate_absolute_expression(as,text,value);
}

/* GNU DATA relocation expressions are affine programs over at most two
 * symbols and one Fragment dot. Lower the shared typed syntax directly;
 * never split source strings at the first '-' or scan parentheses again.
 * Only numeric subtrees use the shared checked-absolute operation policy. */
typedef struct MiniAsAffineValue {
    const MiniAsExprNode *symbols[2];
    int signs[2];
    unsigned symbol_count;
    int dot_coefficient;
    int64_t constant;
    uint64_t divisor;
    bool logical_shift;
} MiniAsAffineValue;

typedef struct MiniAsAffineForm {
    MiniAsMCExprKind kind;
    MiniAsSymbolExpr lhs,rhs;
    uint64_t divisor;
    bool logical_shift;
} MiniAsAffineForm;

static bool affine_has_relocation(const MiniAsAffineValue *value) {
    return value->symbol_count!=0U || value->dot_coefficient!=0;
}

static bool affine_combine(const MiniAsAffineValue *lhs,
                            const MiniAsAffineValue *rhs,int sign,
                            MiniAsAffineValue *out) {
    unsigned i;
    if(lhs->divisor!=0U || rhs->divisor!=0U ||
       lhs->symbol_count+rhs->symbol_count>2U)return false;
    *out=*lhs;
    /* Relocatable DATA addends are GNU 64-bit modular values. A checked
     * signed intermediate would reject MAX-(-MAX), whose final RELA addend
     * is correctly -2. Keep checked signed math for absolute-only trees. */
    out->constant=signed_from_bits(sign>0?
        (uint64_t)lhs->constant+(uint64_t)rhs->constant:
        (uint64_t)lhs->constant-(uint64_t)rhs->constant);
    out->dot_coefficient+=sign*rhs->dot_coefficient;
    for(i=0U;i<rhs->symbol_count;++i) {
        out->symbols[out->symbol_count]=rhs->symbols[i];
        out->signs[out->symbol_count++]=sign*rhs->signs[i];
    }
    return true;
}

static bool affine_copy_symbol(const MiniAsExprSyntax *syntax,
                                const MiniAsExprNode *node,
                                MiniAsSymbolExpr *symbol) {
    if(!node || node->kind!=MINIAS_EXPR_SYMBOL ||
       node->length==0U || node->length>=sizeof(symbol->name))return false;
    memset(symbol,0,sizeof(*symbol));
    memcpy(symbol->name,syntax->text+node->start,node->length);
    symbol->name[node->length]='\0';
    return true;
}

static bool affine_lower_syntax(MiniAs *as,const MiniAsExprSyntax *syntax,
                                MiniAsAffineForm *result) {
    MiniAsAffineValue values[512];
    MiniAsAffineValue *root;
    size_t i;
    unsigned positive=2U,negative=2U;
    if(!syntax || !result || !syntax->count ||
       syntax->count>sizeof(values)/sizeof(values[0]))return false;
    memset(values,0,syntax->count*sizeof(values[0]));
    for(i=0U;i<syntax->count;++i) {
        const MiniAsExprNode *node=&syntax->nodes[i];
        MiniAsAffineValue *out=&values[i];
        const MiniAsAffineValue *lhs=&values[node->left];
        const MiniAsAffineValue *rhs=&values[node->right];
        switch(node->kind) {
        case MINIAS_EXPR_LITERAL:
            out->constant=signed_from_bits(node->literal);
            break;
        case MINIAS_EXPR_SYMBOL: {
            MiniAsSymbolExpr name;
            MiniAsSymbol *symbol;
            if(!affine_copy_symbol(syntax,node,&name))return false;
            symbol=as!=NULL?minias_get_symbol(as,name.name,false):NULL;
            if(symbol && symbol->defined &&
               symbol->section==MINIAS_SECTION_ABS)
                out->constant=signed_from_bits(symbol->value);
            else {
                out->symbols[0]=node;
                out->signs[0]=1;
                out->symbol_count=1U;
            }
            break;
        }
        case MINIAS_EXPR_DOT:
            out->dot_coefficient=1;
            break;
        case MINIAS_EXPR_UNARY:
            if(node->left>=i || lhs->divisor!=0U)return false;
            *out=*lhs;
            if(affine_has_relocation(out)) {
                if(node->unary!='-')return false;
                out->constant=signed_from_bits(0U-(uint64_t)out->constant);
                out->dot_coefficient=-out->dot_coefficient;
                for(unsigned k=0U;k<out->symbol_count;++k)
                    out->signs[k]=-out->signs[k];
            }else if(!evaluate_absolute_unary(node->unary,
                                               lhs->constant,&out->constant))
                return false;
            break;
        case MINIAS_EXPR_BINARY:
            if(node->left>=i || node->right>=i)return false;
            if((node->op==MINIAS_EXPR_OP_ADD ||
                node->op==MINIAS_EXPR_OP_SUB) &&
               (affine_has_relocation(lhs) || affine_has_relocation(rhs))) {
                if(!affine_combine(lhs,rhs,
                                  node->op==MINIAS_EXPR_OP_ADD?1:-1,out))
                    return false;
            }else if(!affine_has_relocation(lhs) &&
                     !affine_has_relocation(rhs)) {
                if(lhs->divisor!=0U || rhs->divisor!=0U ||
                   !evaluate_absolute_binary(node->op,lhs->constant,
                                             rhs->constant,&out->constant))
                    return false;
            }else if((node->op==MINIAS_EXPR_OP_DIV ||
                      node->op==MINIAS_EXPR_OP_SHR) &&
                     i==syntax->root && lhs->divisor==0U &&
                     !affine_has_relocation(rhs) && rhs->divisor==0U &&
                     rhs->constant>0 &&
                     (node->op!=MINIAS_EXPR_OP_SHR || rhs->constant<63)) {
                *out=*lhs;
                out->logical_shift=node->op==MINIAS_EXPR_OP_SHR;
                out->divisor=out->logical_shift?
                    UINT64_C(1)<<(unsigned)rhs->constant:
                    (uint64_t)rhs->constant;
            }else return false;
            break;
        default:return false;
        }
    }
    root=&values[syntax->root];
    memset(result,0,sizeof(*result));
    result->divisor=root->divisor;
    result->logical_shift=root->logical_shift;
    for(i=0U;i<root->symbol_count;++i) {
        if(root->signs[i]==1) {
            if(positive!=2U)return false;
            positive=(unsigned)i;
        }else if(root->signs[i]==-1) {
            if(negative!=2U)return false;
            negative=(unsigned)i;
        }else return false;
    }
    if(positive!=2U &&
       !affine_copy_symbol(syntax,root->symbols[positive],&result->lhs))
        return false;
    if(negative!=2U &&
       !affine_copy_symbol(syntax,root->symbols[negative],&result->rhs))
        return false;
    result->lhs.addend=root->constant;
    if(root->symbol_count==1U && positive!=2U &&
       root->dot_coefficient==0 && root->divisor==0U)
        result->kind=MINIAS_MC_EXPR_SYMBOL;
    else if(root->symbol_count==1U && positive!=2U &&
            root->dot_coefficient==-1)
        result->kind=root->divisor!=0U?
            MINIAS_MC_EXPR_SYMBOL_MINUS_DOT_DIV:
            MINIAS_MC_EXPR_SYMBOL_MINUS_DOT;
    else if(root->symbol_count==1U && negative!=2U &&
            root->dot_coefficient==1 && root->divisor==0U)
        result->kind=MINIAS_MC_EXPR_DOT_MINUS_SYMBOL;
    else if(root->symbol_count==2U && positive!=2U && negative!=2U &&
            root->dot_coefficient==0)
        result->kind=root->divisor!=0U?
            MINIAS_MC_EXPR_DIFFERENCE_DIV:MINIAS_MC_EXPR_DIFFERENCE;
    else return false;
    return true;
}

static bool parse_symbol_difference(const char *,
                                    MiniAsSymbolExpr *,MiniAsSymbolExpr *);

/* A GNU expression such as (identity_mapped - relocate_kernel) is
 * absolute only AFTER both label anchors exist and subsection layout is
 * finalized. Preserve the two typed symbols throughout parse and measure:
 * parsing a forward symbol as zero or inventing a relocation is wrong.
 * Common owns syntax and final same-section semantics for all targets. */
bool minias_parse_symbol_difference(const char *expression,
                                    MiniAsSymbolDifference *out) {
    return expression && out &&
           parse_symbol_difference(expression,&out->positive,&out->negative);
}

/* One Common parser for a retained section-symbol difference followed by
 * a GNU absolute arithmetic suffix. A64 branch/memory operands share this
 * with any future target; neither Target reinterprets '-4' as an addend on
 * the negative symbol, which would flip its sign. */
bool minias_parse_symbol_difference_absolute_tail(
    MiniAs *as,const char *text,MiniAsSymbolDifference *result) {
    MiniAsExprSyntax syntax;
    MiniAsAffineForm form;
    bool valid;
    if(!as || !text || !result ||
       !minias_expr_parse_syntax(text,&syntax))return false;
    valid=affine_lower_syntax(as,&syntax,&form) &&
          form.kind==MINIAS_MC_EXPR_DIFFERENCE;
    if(valid) {
        result->positive=form.lhs;
        result->negative=form.rhs;
    }
    minias_expr_syntax_destroy(&syntax);
    return valid;
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

/* For P+a - (N+b), a same-section pair is a final-layout absolute value.
 * Otherwise, if N is a local anchor in the field's section, the linker can
 * compute the same expression with R_PC32(P, field-N+a-b):
 *     P + (field-N+a-b) - field = P+a-N-b.
 * This arithmetic belongs to Common MCExpr; the target decides whether the
 * requested field has a PC-relative relocation form. */
bool minias_resolve_symbol_difference_field(MiniAs *as,
                                            const MiniAsSymbolDifferenceRef *pair,
                                            int section,uint64_t field_offset,
                                            int64_t *absolute,bool *has_reloc,
                                            MiniAsSymbolRef *reloc_ref) {
    const MiniAsSymbol *positive,*negative;
    int64_t field_minus_negative,with_positive,addend;
    if(!as || !pair || !absolute || !has_reloc || !reloc_ref)return false;
    *has_reloc=false;
    if(minias_resolve_symbol_difference_ref(as,pair,absolute))return true;
    positive=minias_symbol_ref_lookup(as,pair->positive);
    negative=minias_symbol_ref_lookup(as,pair->negative);
    /* The positive SymbolID may not exist yet: a first reference to an
     * external is materialized by the fixup, never by name interning. */
    if(!negative || !negative->defined ||
       negative->section!=section || section<0 ||
       (positive && positive->defined &&
        positive->section==negative->section) ||
       field_offset>INT64_MAX || negative->value>INT64_MAX)return false;
    if(!checked_sub_i64((int64_t)field_offset,
                        (int64_t)negative->value,&field_minus_negative) ||
       !minias_checked_add_i64(field_minus_negative,
                              pair->positive.addend,&with_positive) ||
       !checked_sub_i64(with_positive,pair->negative.addend,&addend))
        return false;
    *reloc_ref=pair->positive;
    reloc_ref->addend=addend;
    *has_reloc=true;
    return true;
}

static bool evaluate_data_constant_syntax(MiniAs *as,
                                          const MiniAsExprSyntax *syntax,
                                          uint64_t *value) {
    uint64_t dot;
    int64_t signed_value;
    bool only_abs,valid;
    /* The canonical unsigned grammar subsumes the old strtoll/strtoull
     * fixed-DATA literal paths, including signed bit patterns and nested
     * parentheses. Never revive a separate literal interpreter here.
     * Numeric evaluation cannot fabricate symbols or relocations. */
    if(minias_expr_eval_integer(syntax,false,value))return true;
    valid=minias_current_location(as,&dot) &&
          dot<=(uint64_t)INT64_MAX &&
          evaluate_absolute_syntax(as,syntax,(int64_t)dot,
                                   &only_abs,&signed_value) && only_abs;
    if(valid)*value=(uint64_t)signed_value;
    return valid;
}

bool minias_parse_absolute_data_bits(MiniAs *as,
                                     const char *text,
                                     uint64_t *value) {
    MiniAsExprSyntax syntax;
    bool valid;
    if(!as || !text || !value ||
       !minias_expr_parse_syntax(text,&syntax))return false;
    valid=evaluate_data_constant_syntax(as,&syntax,value);
    minias_expr_syntax_destroy(&syntax);
    return valid;
}

/* One AST supplies every GNU relocation-tail classification. */
bool minias_expr_parse_symbol_absolute_tail(MiniAs *as,const char *text,
                                             MiniAsSymbolExpr *expr) {
    MiniAsExprSyntax syntax;
    MiniAsAffineForm form;
    bool valid;
    if(!as || !text || !expr ||
       !minias_expr_parse_syntax(text,&syntax))return false;
    valid=affine_lower_syntax(as,&syntax,&form) &&
          form.kind==MINIAS_MC_EXPR_SYMBOL;
    if(valid)*expr=form.lhs;
    minias_expr_syntax_destroy(&syntax);
    return valid;
}

static bool parse_symbol_difference(const char *text,
                                    MiniAsSymbolExpr *lhs,
                                    MiniAsSymbolExpr *rhs) {
    MiniAsExprSyntax syntax;
    MiniAsAffineForm form;
    bool valid;
    if(!text || !lhs || !rhs ||
       !minias_expr_parse_syntax(text,&syntax))return false;
    valid=affine_lower_syntax(NULL,&syntax,&form) &&
          form.kind==MINIAS_MC_EXPR_DIFFERENCE;
    if(valid) {*lhs=form.lhs;*rhs=form.rhs;}
    minias_expr_syntax_destroy(&syntax);
    return valid;
}

bool minias_parse_size_end_addend(const char *text,const char *name,
                                  int64_t *addend) {
    MiniAsExprSyntax syntax;
    MiniAsAffineForm form;
    bool valid;
    if(!text || !name || !addend ||
       !minias_expr_parse_syntax(text,&syntax))return false;
    valid=affine_lower_syntax(NULL,&syntax,&form) &&
          form.kind==MINIAS_MC_EXPR_DOT_MINUS_SYMBOL &&
          strcmp(form.rhs.name,name)==0;
    if(valid)*addend=form.lhs.addend;
    minias_expr_syntax_destroy(&syntax);
    return valid;
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

void minias_deferred_expr_destroy(MiniAsDeferredExpr *tree) {
    if(tree!=NULL) {free(tree->nodes);free(tree);}
}

MiniAsDeferredExpr *minias_compile_deferred_expr(MiniAs *as,
                                                 const char *text) {
    MiniAsExprSyntax syntax;
    MiniAsDeferredExpr *tree=NULL;
    size_t i;
    if(!as || !text)return NULL;
    if(!minias_expr_parse_syntax(text,&syntax))goto invalid_syntax;
    tree=calloc(1U,sizeof(*tree));
    if(!tree)goto no_memory;
    tree->nodes=calloc(syntax.count,sizeof(*tree->nodes));
    if(!tree->nodes)goto no_memory;
    tree->count=tree->capacity=syntax.count;
    tree->root=syntax.root;
    /* Node indices are stable and child-first in the shared syntax tree.
     * Bind names to the enduring Symbol table exactly once, then discard
     * the source-text syntax before MCLayout and Fragment relaxation. */
    for(i=0U;i<syntax.count;++i) {
        const MiniAsExprNode *node=&syntax.nodes[i];
        DeferredNode *out=&tree->nodes[i];
        out->left=node->left;out->right=node->right;
        switch(node->kind) {
        case MINIAS_EXPR_LITERAL:
            out->op=DEF_LITERAL;
            out->value=signed_from_bits(node->literal);
            break;
        case MINIAS_EXPR_DOT:out->op=DEF_DOT;break;
        case MINIAS_EXPR_SYMBOL: {
            char name[256];
            MiniAsSymbol *symbol;
            if(!node->length || node->length>=sizeof(name))goto invalid;
            memcpy(name,text+node->start,node->length);
            name[node->length]='\0';
            symbol=minias_get_symbol(as,name,true);
            if(!symbol)goto invalid;
            out->op=DEF_SYMBOL;
            out->symbol_index=(size_t)(symbol-as->symbols);
            break;
        }
        case MINIAS_EXPR_UNARY:
            out->op=node->unary=='-'?DEF_NEG:
                    node->unary=='~'?DEF_INV:DEF_LNOT;
            break;
        case MINIAS_EXPR_BINARY:
            out->op=deferred_operator(node->op);
            break;
        default:goto invalid;
        }
    }
    minias_expr_syntax_destroy(&syntax);
    return tree;
no_memory:
    minias_set_error(as,"out-of-memory:deferred-expression");
invalid:
    minias_deferred_expr_destroy(tree);
    minias_expr_syntax_destroy(&syntax);
invalid_syntax:
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
            /* GNU GAS treats right shifts of absolute 64-bit expressions
             * as logical bit-pattern shifts, even after a same-section
             * symbolic subtraction yields a negative signed value. Match
             * the eager absolute grammar: bit 63 is a legal shift amount.
             * Only resolved absolute operands may reach this path (checked
             * below); a relocatable shift must never be guessed. */
            if(r<0 || r>=64)return false;
            bits=(uint64_t)l;
            if(n->op==DEF_SHL)bits<<=(unsigned)r;
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
    /* Keep MCData's semantic field count identical to the source producer:
     * GAS's operand-free `.long`/`.word` emits no bytes and no fixups. */
    count=*minias_trim(copy)=='\0'?0U:1U;
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
    data->fields=count?calloc(count,sizeof(*data->fields)):NULL;
    if(count && !data->fields) {
        free(copy);minias_set_error(as,"out-of-memory:mc-data-fields");
        return false;
    }
    data->width=width;
    data->count=count;
    walk=count?copy:NULL;
    while(walk && i<count) {
        char *comma=strchr(walk,',');
        char *token;
        uint64_t value=0U;
        bool matched=false;
        MiniAsSymbolExpr lhs={0},rhs={0};
        MiniAsMCExpr *out=&data->fields[i];
        if(comma)*comma='\0';
        token=minias_trim(walk);
        /* A DATA field is parsed once. Scalar and relocatable policies both
         * inspect this very AST at the lexical source site; only bound
         * SymbolIDs and typed arithmetic survive relaxation. */
        {
            MiniAsExprSyntax syntax;
            MiniAsAffineForm affine;
            if(minias_expr_parse_syntax(token,&syntax)) {
                if(evaluate_data_constant_syntax(as,&syntax,&value)) {
                    out->kind=MINIAS_MC_EXPR_CONSTANT;
                    out->constant=value;
                    matched=true;
                }else if(affine_lower_syntax(as,&syntax,&affine) &&
                         ((width==2U || width==4U || width==8U) ||
                          (affine.kind!=MINIAS_MC_EXPR_SYMBOL_MINUS_DOT &&
                           affine.kind!=MINIAS_MC_EXPR_SYMBOL_MINUS_DOT_DIV)) &&
                         (width==1U || width==2U || width==4U || width==8U)) {
                    out->kind=affine.kind;
                    out->divisor=affine.divisor;
                    out->logical_shift=affine.logical_shift;
                    lhs=affine.lhs;
                    rhs=affine.rhs;
                    matched=true;
                }
                minias_expr_syntax_destroy(&syntax);
            }
        }
        if(!matched)
            minias_set_error(as,"unsupported-mc-expression:%s:line=%zu",
                             token,stmt->line);
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
        /* Local binding and ELF visibility are independent. The Target ABI
         * owns the visibility of assembler-synthesized expression anchors;
         * the ELF writer must not infer it from their private spelling. */
        anchor->visibility = as->target->synthetic_expr_anchors_hidden
                                 ? MINIAS_STV_HIDDEN : MINIAS_STV_DEFAULT;

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
    if((width==1U || width==2U || width==4U || width==8U) &&
       (!can_add || !can_sub) &&
       rhs->defined &&
       /* GNU GAS resolves defined GLOBAL/WEAK anchors to their section
        * positions for PC64 as well as LOCAL anchors. This binding
        * behavior is the same on x86-64 and AArch64. */
       rhs->section==stmt->section) {
        MiniAsFixupKind kind=(width<=4U)?
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

/* Complete, strictly typed DATA operands have no borrowed source text. */
void minias_mcdata_destroy(MiniAsMCData *data) {
    if(data==NULL)return;
    free(data->fields);
    free(data);
}
