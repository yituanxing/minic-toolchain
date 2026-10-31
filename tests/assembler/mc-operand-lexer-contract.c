#include "minias_internal.h"

#include <stdio.h>
#include <string.h>

static bool expect(const char *source,unsigned expected,
                   const char *const words[]) {
    char text[4][384]={{0}};
    char *out[4]={text[0],text[1],text[2],text[3]};
    const size_t capacity[4]={sizeof(text[0]),sizeof(text[1]),
                              sizeof(text[2]),sizeof(text[3])};
    unsigned count=0U,i;
    if(!minias_split_mc_operands(source,out,capacity,4U,&count) ||
       count!=expected)return false;
    for(i=0U;i<count;++i)
        if(strcmp(out[i],words[i])!=0)return false;
    return true;
}
static bool reject(const char *source) {
    char text[4][32]={{0}};
    char *out[4]={text[0],text[1],text[2],text[3]};
    const size_t capacity[4]={32U,32U,32U,32U};
    unsigned count=0U;
    return !minias_split_mc_operands(source,out,capacity,4U,&count);
}
static bool reject_overflow(void) {
    char first[4]={0},second[4]={0};
    char *out[2]={first,second};
    const size_t capacity[2]={sizeof(first),sizeof(second)};
    unsigned count=0U;
    return !minias_split_mc_operands("very_long_register,%r1",out,
                                     capacity,2U,&count);
}
int main(void) {
    static const char *const x86[]={"8(%rax,%rbx,4)","%r9"};
    static const char *const a64[]={"{v0.16b,v1.16b}","[x2,#16]","#3"};
    static const char *const nested[]={"(symbol+(4*2))","[x1,#(3+5)]"};
    static const char *const quoted[]={"\"a,b\"","'(x,y)'"};
    if(!expect(" 8(%rax,%rbx,4),  %r9 ",2U,x86) ||
       !expect("{v0.16b,v1.16b}, [x2,#16], #3",3U,a64) ||
       !expect("(symbol+(4*2)),[x1,#(3+5)]",2U,nested) ||
       !expect("\"a,b\",'(x,y)'",2U,quoted) ||
       !reject("%rax,,%rdi") || !reject("%rax,") ||
       !reject("[x0,#8),x1") || !reject("(%rax,%rcx") ||
       !reject("%rax,%rdi,%rcx,%r8,%r9") ||
       !reject("\"unterminated,operand") ||
       !reject("[x0,(x1}] ") || !reject_overflow()) {
        fputs("MC_OPERAND_LEXER_CONTRACT=FAIL\n",stderr);
        return 1;
    }
    puts("MC_OPERAND_LEXER_CONTRACT=PASS x86_sib=1 a64_groups=1 nested=1 quoted=1 invalid=8");
    return 0;
}
