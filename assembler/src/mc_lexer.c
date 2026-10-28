#include "minias_internal.h"

#include <ctype.h>
#include <string.h>

/* MC owns only lexical separation. ISA parsing remains target-private:
 * [] memory operands, () displacement expressions, and {} register lists
 * are groups, not extra top-level instruction operands. The one delimiter
 * stack also catches mismatched nesting that three ad-hoc target scanners
 * previously interpreted differently. Quotes protect literal commas. */
bool minias_split_mc_operands(const char *input,char *const outputs[],
                              const size_t capacities[],unsigned limit,
                              unsigned *count) {
    char closing[64];
    unsigned depth=0U,n=0U;
    char quote='\0';
    bool escape=false;
    const char *start,*p;
    if(!input || !outputs || !capacities || !count || limit==0U)
        return false;
    start=input;
    for(p=input;;++p) {
        const char ch=*p;
        if(quote!='\0') {
            if(ch=='\0')return false;
            if(escape)escape=false;
            else if(ch=='\\')escape=true;
            else if(ch==quote)quote='\0';
            continue;
        }
        if(ch=='\'' || ch=='"') {
            quote=ch;
            continue;
        }
        if(ch=='(' || ch=='[' || ch=='{') {
            if(depth==sizeof(closing))return false;
            closing[depth++]=ch=='('?')':ch=='['?']':'}';
            continue;
        }
        if(ch==')' || ch==']' || ch=='}') {
            if(depth==0U || closing[--depth]!=ch)return false;
            continue;
        }
        if(ch==',' && depth!=0U)continue;
        if(ch==',' || ch=='\0') {
            const char *lo=start,*hi=p;
            size_t len;
            if(depth!=0U || n==limit || !outputs[n] ||
               capacities[n]==0U)return false;
            while(lo<hi && isspace((unsigned char)*lo))++lo;
            while(hi>lo && isspace((unsigned char)hi[-1]))--hi;
            len=(size_t)(hi-lo);
            if(len==0U || len>=capacities[n])return false;
            memcpy(outputs[n],lo,len);
            outputs[n][len]='\0';
            ++n;
            if(ch=='\0')break;
            start=p+1U;
        }
    }
    *count=n;
    return true;
}
