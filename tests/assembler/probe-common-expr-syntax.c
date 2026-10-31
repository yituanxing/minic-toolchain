#include "expression_grammar.h"
#include "minias_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int expect_u64(const char *text,uint64_t expected) {
    uint64_t actual=0U;
    if(!minias_eval_u64_expression(text,&actual) || actual!=expected) {
        fprintf(stderr,"unsigned AST failure: %s\n",text);
        return 1;
    }
    return 0;
}

static int expect_i64(const char *text,int64_t expected) {
    int64_t actual=0;
    if(!minias_eval_i64_expression(text,&actual) || actual!=expected) {
        fprintf(stderr,"signed AST failure: %s\n",text);
        return 1;
    }
    return 0;
}

int main(void) {
    MiniAsExprSyntax syntax;
    size_t i;
    bool symbol=false,dot=false;
    if(expect_u64("(2 + 3 * 4) << 2",56U) ||
       expect_u64("0b1010ULL | 3",11U) ||
       expect_u64("-0xffffffffffffffff",1U) ||
       expect_u64("(~0ULL >> 63)",1U) ||
       expect_u64("1 + 2 + 3 + 4 + 5 + 6 + 7 + 8 + 9",45U) ||
       expect_i64("(-33 >> 5)",-2) ||
       expect_i64("(~0 >> 63)",-1))return 1;
    if(minias_eval_u64_expression("1 < 2",&(uint64_t){0}) ||
       minias_eval_u64_expression("0 && 1",&(uint64_t){0}) ||
       minias_eval_u64_expression("1 << 64",&(uint64_t){0}) ||
       minias_eval_u64_expression("1 / 0",&(uint64_t){0}) ||
       minias_eval_u64_expression("0x10000000000000000",&(uint64_t){0})) {
        fprintf(stderr,"numeric policy unexpectedly widened\n");return 1;
    }
    if(!minias_expr_parse_syntax("(forward - .) + (7 << 2)",&syntax)) {
        fprintf(stderr,"symbolic AST failed\n");return 1;
    }
    for(i=0U;i<syntax.count;++i) {
        const MiniAsExprNode *n=&syntax.nodes[i];
        if(n->kind==MINIAS_EXPR_SYMBOL && n->length==7U &&
           strncmp(syntax.text+n->start,"forward",7U)==0)symbol=true;
        if(n->kind==MINIAS_EXPR_DOT)dot=true;
    }
    minias_expr_syntax_destroy(&syntax);
    if(!symbol || !dot) {
        fprintf(stderr,"typed symbol/dot spans missing\n");return 1;
    }
    puts("MINIAS_EXPR_SYNTAX=PASS grammar=1 numeric_policy=2 symbol_dot=1");
    return 0;
}
