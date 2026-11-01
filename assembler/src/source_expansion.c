#include "source_graph_internal.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Source-level comments are target-prefix-aware, but never Target syntax.
 * Macro/repeat classification and logical statement splitting share it. */
static void strip_comment(char *line, const MiniAsTargetOps *target) {
    char quote = '\0';
    bool escaped = false;
    char *p;

    for (p = line; *p != '\0'; ++p) {
        if (escaped) {
            escaped = false;
            continue;
        }
        if (quote != '\0') {
            if (*p == '\\') {
                escaped = true;
                continue;
            }
            if (*p == quote) {
                quote = '\0';
            }
            continue;
        }
        if (*p == '\'' || *p == '"') {
            quote = *p;
            continue;
        }
        if ((target->comment_prefix == '/' && *p == '/' && p[1] == '/') ||
            (target->comment_prefix != '/' && *p == target->comment_prefix)) {
            *p = '\0';
            return;
        }
    }
}

/* A purged GNU macro owns exactly the resources freed by assembler exit.
 * Keep a single lifetime owner so later redefinitions never retain stale
 * macro parameter/body pointers in the symbol-independent Common layer. */
static void minias_macro_destroy(MiniAsMacro *macro) {
    size_t i;
    free(macro->name);
    for(i=0U;i<macro->param_count;++i) {
        free(macro->params[i].name);
        free(macro->params[i].default_value);
    }
    free(macro->params);
    minias_source_lines_destroy(macro->body,macro->body_count);
    memset(macro,0,sizeof(*macro));
}

/* Source expansion owns the entire fresh-pass lexical/conditional lifetime.
 * The persistent graph is borrowed and is released separately by the
 * invocation after its final semantic pass (including failure paths). */
bool minias_source_context_init(MiniAs *as) {
    if(as==NULL || as->source!=NULL)return false;
    as->source=calloc(1U,sizeof(*as->source));
    if(as->source==NULL) {
        minias_set_error(as,"out-of-memory:source-context");
        return false;
    }
    as->source->conditional_active=true;
    return true;
}

void minias_source_context_destroy(MiniAsSourceContext *source) {
    size_t i;
    if(source==NULL)return;
    free(source->conditionals);
    for(i=0U;i<source->macro_count;++i)
        minias_macro_destroy(&source->macros[i]);
    free(source->macros);
    free(source);
}

MiniAsStableSiteId minias_source_current_site(const MiniAs *as) {
    return as->source->current_site;
}

void minias_source_restore_site(MiniAs *as,MiniAsStableSiteId site) {
    as->source->current_site=site;
}

/* Bind the persistent SourceGraph to this fresh lexical pass. Common only
 * receives the processed Fragment stream, never the FILE/expansion frame or
 * the conditional stack. Errors and the replay pass counter are unchanged. */
bool minias_source_context_process_graph(MiniAs *as,MiniAsSourceGraph *graph) {
    if(as->source->graph!=NULL || graph==NULL ||
       graph->physical.kind!=MINIAS_SOURCE_FRAME_FILE) {
        minias_set_error(as,"invalid-source-graph-state");
        return false;
    }
    as->source->graph=graph;
    ++graph->parse_passes;
    if(!minias_source_frame_process(as,&graph->physical))return false;
    if(as->source->conditional_count!=0U) {
        minias_set_error(as,"unterminated-directive:.if:depth=%zu",
                         as->source->conditional_count);
        return false;
    }
    return true;
}

/* Conditional comparisons use the same Common MCExpr precedence grammar as
 * data and target operands. Keep only the historical single '=' spelling
 * accepted by MiniAS source files: it is a compatibility fallback, not a
 * second parser for ==, !=, <=, >=, shifts, or logical operators. */
static bool evaluate_if_expression(MiniAs *as,const char *text,bool *result) {
    int64_t value;
    const char *p,*equal=NULL;
    unsigned depth=0U;
    char *copy,*left,*right;
    int64_t lhs,rhs;
    bool ok;
    if(!text || !result)return false;
    if(minias_evaluate_absolute_expression(as,text,&value)) {
        *result=value!=0;
        return true;
    }
    /* Historic '.if name=0' can only compare at top level. The standard
     * expression grammar handles every other operator, including '=='. */
    if(strstr(text,"&&")!=NULL || strstr(text,"||")!=NULL)return false;
    for(p=text;*p!='\0';++p) {
        if(*p=='(') {++depth;continue;}
        if(*p==')') {
            if(depth==0U)return false;
            --depth;continue;
        }
        if(depth==0U && *p=='=' && p[1]!='=' &&
           (p==text || (p[-1]!='=' && p[-1]!='!' &&
                        p[-1]!='<' && p[-1]!='>'))) {
            equal=p;
            break;
        }
    }
    if(equal==NULL)return false;
    copy=minias_strdup(text);
    if(copy==NULL)return false;
    copy[equal-text]='\0';
    left=minias_trim(copy);
    right=minias_trim(copy+(equal-text)+1U);
    ok=*left!='\0' && *right!='\0' &&
       minias_evaluate_absolute_expression(as,left,&lhs) &&
       minias_evaluate_absolute_expression(as,right,&rhs);
    if(ok)*result=lhs==rhs;
    free(copy);
    return ok;
}

static bool minias_source_push_conditional_value(MiniAs *as,bool condition_true) {
    MiniAsConditional *conditional;
    if (!minias_grow_array((void **)&as->source->conditionals,
                    &as->source->conditional_capacity,
                    sizeof(*as->source->conditionals),
                    as->source->conditional_count + 1U)) {
        minias_set_error(as, "out-of-memory:conditional");
        return false;
    }
    conditional = &as->source->conditionals[as->source->conditional_count++];
    conditional->parent_active = as->source->conditional_active;
    conditional->condition_true = condition_true;
    conditional->else_seen = false;
    conditional->source_site = as->source->current_site;
    as->source->conditional_active = conditional->parent_active && condition_true;
    minias_source_graph_record_condition(as,conditional->source_site,
                                  as->source->conditional_active);
    return true;
}

/* Reuse the sole Common numeric-label resolver in GAS conditionals:
 * backward 661b/662b references have exact fragment-backed values. */
static bool evaluate_conditional_with_labels(MiniAs *as,const char *args,
                                              size_t line,bool *selected) {
    char *expanded=NULL;
    bool valid;
    if(!minias_numeric_refs_rewrite(as,args,&expanded,line))return false;
    valid=evaluate_if_expression(as,expanded,selected) ||
          (as->target->evaluate_target_if &&
           as->target->evaluate_target_if(expanded,selected));
    free(expanded);return valid;
}

static bool minias_source_push_conditional(MiniAs *as, const char *args, size_t line) {
    bool condition_true;
    /* False parent: keep the nested conditional frame, but GNU GAS
     * does not resolve symbols in branches that cannot be reached. */
    if(!as->source->conditional_active)
        return minias_source_push_conditional_value(as,false);
    if (!evaluate_conditional_with_labels(as,args,line,&condition_true)) {
        minias_set_error(as, "unsupported-expression:.if:%s:line=%zu", args, line);
        return false;
    }
    return minias_source_push_conditional_value(as,condition_true);
}

/* All numeric GNU .if* variants have the same Common conditional frame:
 * skip unreachable parents without evaluating unresolved expressions. */
static bool minias_source_push_conditional_numeric(MiniAs *as,const char *args,
                                     const char *directive,size_t line) {
    int64_t value;
    bool active;
    if(!as->source->conditional_active)return minias_source_push_conditional_value(as,false);
    if(!minias_evaluate_absolute_expression(as,args,&value)) {
        minias_set_error(as,"unsupported-expression:%s:%s:line=%zu",
                         directive,args,line);
        return false;
    }
    active=!strcmp(directive,".ifeq")?value==0:
           !strcmp(directive,".ifne")?value!=0:
           !strcmp(directive,".iflt")?value<0:
           !strcmp(directive,".ifle")?value<=0:
           !strcmp(directive,".ifgt")?value>0:value>=0;
    return minias_source_push_conditional_value(as,active);
}

/* GNU GAS .ifdef/.ifndef inspect the CURRENT defined symbol table,
 * not relocation references, global declarations or future labels.
 * Share the exact same conditional stack as .if/.ifc/.elseif and do
 * not create symbol records while checking existence. */
static bool minias_source_push_conditional_defined(MiniAs *as,const char *args,
                                      bool negate,size_t line) {
    const char *p=args;
    MiniAsSymbol *symbol;
    bool defined;
    if(!as->source->conditional_active)
        return minias_source_push_conditional_value(as,false);
    if(!(*p=='_' || *p=='.' || *p=='$' ||
         isalpha((unsigned char)*p))) {
        minias_set_error(as,"bad-directive:.ifdef:line=%zu",line);
        return false;
    }
    for(++p;*p;++p) {
        if(!(*p=='_' || *p=='.' || *p=='$' ||
             isalnum((unsigned char)*p))) {
            minias_set_error(as,"bad-directive:.ifdef:line=%zu",line);
            return false;
        }
    }
    symbol=minias_get_symbol(as,args,false);
    defined=symbol && (symbol->defined || symbol->alias_pending);
    return minias_source_push_conditional_value(as,negate?!defined:defined);
}

/* Shared GAS .ifc/.ifnc conditional text: split on a comma outside
 * quotes, strip paired quote delimiters, then compare exact byte strings.
 * Expansion of macro arguments already happens upstream.  No target
 * ISA-specific conditional parser or new conditional stack is needed. */
/* GNU GAS .ifc/.ifnc compares trimmed *literal text*: a missing side
 * is a valid empty string, a quoted comma is still a separator, and a
 * comma in the second operand belongs to that operand. Macro expansion
 * happens before this common conditional stack is evaluated. */
static bool minias_source_push_conditional_strings(MiniAs *as,const char *args,
                                     bool unequal,bool double_quoted,
                                     size_t line) {
    char *copy=minias_strdup(args),*lhs,*rhs,*comma;
    bool equal;
    if(!copy){minias_set_error(as,"out-of-memory:.ifc");return false;}
    if(double_quoted) {
        /* GAS .ifeqs/.ifnes require exactly two double-quoted strings.
         * A comma inside a quoted operand is data, not the delimiter.
         * Keep this on the same Common conditional stack as .ifc/.ifnc. */
        char *end;
        lhs=minias_trim(copy);
        if(*lhs!='"')goto malformed;
        ++lhs;
        end=strchr(lhs,'"');
        if(!end)goto malformed;
        *end++='\0';
        end=minias_trim(end);
        if(*end!=',')goto malformed;
        rhs=minias_trim(end+1);
        if(*rhs!='"')goto malformed;
        ++rhs;
        end=strchr(rhs,'"');
        if(!end)goto malformed;
        *end++='\0';
        if(*minias_trim(end)!='\0')goto malformed;
    }else {
        /* Existing .ifc/.ifnc first-comma semantics are intentional. */
        comma=strchr(copy,',');
        if(!comma)goto malformed;
        *comma='\0';
        lhs=minias_trim(copy);
        rhs=minias_trim(comma+1);
    }
    equal=strcmp(lhs,rhs)==0;
    free(copy);
    return minias_source_push_conditional_value(as,unequal?!equal:equal);
malformed:
    minias_set_error(as,"bad-directive:%s:line=%zu",
                     double_quoted?".ifeqs":".ifc",line);
    free(copy);return false;
}

/* GAS .elseif shares the same frame as .if/.else. Once an
 * earlier arm wins, do not evaluate later arms: symbols there need
 * not exist, and ignored nested branches must not leak statements. */
static bool minias_source_elseif_conditional(MiniAs *as,const char *args,size_t line) {
    MiniAsConditional *conditional;
    bool condition=false;
    if(as->source->conditional_count==0U) {
        minias_set_error(as,"unmatched-directive:.elseif:line=%zu",line);
        return false;
    }
    conditional=&as->source->conditionals[as->source->conditional_count-1U];
    if(conditional->else_seen) {
        minias_set_error(as,"elseif-after-else:line=%zu",line);
        return false;
    }
    if(conditional->parent_active && !conditional->condition_true) {
        if(!evaluate_conditional_with_labels(as,args,line,&condition)) {
            minias_set_error(as,"unsupported-expression:.elseif:%s:line=%zu",
                             args,line);
            return false;
        }
    }
    as->source->conditional_active=conditional->parent_active &&
                           !conditional->condition_true && condition;
    conditional->condition_true|=condition;
    minias_source_graph_record_condition(as,conditional->source_site,
                                  as->source->conditional_active);
    return true;
}

static bool minias_source_else_conditional(MiniAs *as, size_t line) {
    MiniAsConditional *conditional;

    if (as->source->conditional_count == 0U) {
        minias_set_error(as, "unmatched-directive:.else:line=%zu", line);
        return false;
    }
    conditional = &as->source->conditionals[as->source->conditional_count - 1U];
    if (conditional->else_seen) {
        minias_set_error(as, "duplicate-directive:.else:line=%zu", line);
        return false;
    }
    conditional->else_seen = true;
    as->source->conditional_active =
        conditional->parent_active && !conditional->condition_true;
    minias_source_graph_record_condition(as,conditional->source_site,
                                  as->source->conditional_active);
    return true;
}

static bool minias_source_pop_conditional(MiniAs *as, size_t line) {
    MiniAsConditional conditional;

    if (as->source->conditional_count == 0U) {
        minias_set_error(as, "unmatched-directive:.endif:line=%zu", line);
        return false;
    }
    conditional = as->source->conditionals[--as->source->conditional_count];
    as->source->conditional_active = conditional.parent_active;
    return true;
}

/* Conditional dispatch belongs to the same pass-local source owner as its
 * stack, including the lexical-vs-layout-sensitive replay decision.  The
 * ordinary statement lowerer calls this before parsing labels/opcodes. */
bool minias_source_try_conditional(MiniAs *as,char *text,size_t line,
                                   bool *handled) {
    *handled=true;
    if((strncmp(text,".ifb",4U)==0 ||
        strncmp(text,".ifnb",5U)==0) &&
       ((text[3]=='b' && (text[4]=='\0' ||
                         isspace((unsigned char)text[4]))) ||
        (text[3]=='n' && text[4]=='b' &&
         (text[5]=='\0' || isspace((unsigned char)text[5]))))) {
        bool nonblank=text[3]=='n';
        const char *arg=text+(nonblank?5U:4U);
        while(isspace((unsigned char)*arg))++arg;
        return minias_source_push_conditional_value(as,
                                  nonblank?*arg!='\0':*arg=='\0');
    }
    if ((strncmp(text,".ifeqs",6U)==0 ||
         strncmp(text,".ifnes",6U)==0) &&
        (text[6]=='\0' || isspace((unsigned char)text[6]))) {
        bool unequal=text[3]=='n';
        return minias_source_push_conditional_strings(as,minias_trim(text+6U),
                                                      unequal,true,line);
    }
    if ((strncmp(text,".ifc",4U)==0 ||
         strncmp(text,".ifnc",5U)==0) &&
        ((text[3]=='c' && (text[4]=='\0' ||
                          isspace((unsigned char)text[4]))) ||
         (text[3]=='n' && text[4]=='c' &&
          (text[5]=='\0' || isspace((unsigned char)text[5]))))) {
        bool unequal=text[3]=='n';
        return minias_source_push_conditional_strings(as,
                minias_trim(text+(unequal?5U:4U)),unequal,false,line);
    }
    if ((strncmp(text,".ifdef",6U)==0 &&
         (text[6]=='\0' || isspace((unsigned char)text[6]))) ||
        (strncmp(text,".ifndef",7U)==0 &&
         (text[7]=='\0' || isspace((unsigned char)text[7]))) ||
        (strncmp(text,".ifnotdef",9U)==0 &&
         (text[9]=='\0' || isspace((unsigned char)text[9])))) {
        bool negate=text[3]=='n';
        unsigned width=negate?(text[4]=='o'?9U:7U):6U;
        return minias_source_push_conditional_defined(as,
                        minias_trim(text+width),negate,line);
    }
    {
        static const char *const numeric_forms[]={
            ".ifeq",".ifne",".iflt",".ifle",".ifgt",".ifge"
        };
        unsigned i;
        for(i=0U;i<sizeof(numeric_forms)/sizeof(numeric_forms[0]);++i) {
            const char *name=numeric_forms[i];
            size_t width=strlen(name);
            if(strncmp(text,name,width)==0 &&
               (text[width]=='\0' || isspace((unsigned char)text[width]))) {
                int64_t literal;
                if(!minias_eval_i64_expression(text+width,&literal))
                    as->layout_sensitive=true;
                return minias_source_push_conditional_numeric(as,
                           minias_trim(text+width),name,line);
            }
        }
    }
    if (strncmp(text,".if",3U)==0 &&
        (text[3]=='\0' || isspace((unsigned char)text[3]))) {
        /* Literal conditions are lexical; symbol/dot-dependent choices
         * can change their Fragment set after MCLayout. */
        int64_t literal;
        if(!minias_eval_i64_expression(text+3U,&literal))
            as->layout_sensitive=true;
        return minias_source_push_conditional(as,minias_trim(text+3U),line);
    }
    if (strncmp(text,".elseif",7U)==0 &&
        (text[7]=='\0' || isspace((unsigned char)text[7]))) {
        int64_t literal;
        if(!minias_eval_i64_expression(text+7U,&literal))
            as->layout_sensitive=true;
        return minias_source_elseif_conditional(as,minias_trim(text+7U),line);
    }
    if(strcmp(text,".else")==0)return minias_source_else_conditional(as,line);
    if(strcmp(text,".endif")==0)return minias_source_pop_conditional(as,line);
    *handled=false;
    return true;
}

bool minias_source_conditional_active(const MiniAs *as) {
    return as->source->conditional_active;
}

static bool valid_irp_parameter_name(const char *name);
static char *substitute_irp_parameter(MiniAs *as,
                                      const char *source,
                                      const char *name,
                                      const char *value);

enum {
    MINIAS_MACRO_NONE = 0,
    MINIAS_MACRO_BEGIN = 1,
    MINIAS_MACRO_END = 2,
    MINIAS_REPEAT_REPT = 3,
    MINIAS_REPEAT_ENDR = 4,
    MINIAS_REPEAT_IRP = 5,
    MINIAS_COLLECTOR_MACRO_ERROR = -1,
    MINIAS_COLLECTOR_REPEAT_ERROR = -2
};

/* Macro and repeat collection use different GNU grammars, but both need the
 * same lexical comment/whitespace pass. A non-NULL argument buffer selects
 * each grammar; ordinary source checks macro first, nested blocks only their
 * own delimiter type. Keep their separate truncation/error contracts. */
static int classify_collector_line(const MiniAs *as, const char *text,
                                   char *macro_argument, size_t macro_size,
                                   char *repeat_argument, size_t repeat_size,
                                   size_t *label_prefix_length) {
    char *copy = minias_strdup(text);
    char *trimmed;
    const char *rest = NULL;
    int kind = MINIAS_MACRO_NONE;

    if(macro_argument!=NULL && macro_size!=0U)macro_argument[0]='\0';
    if(repeat_argument!=NULL && repeat_size!=0U)repeat_argument[0]='\0';
    if(label_prefix_length!=NULL)*label_prefix_length=0U;
    if (copy == NULL)
        return macro_argument!=NULL ? MINIAS_COLLECTOR_MACRO_ERROR :
                                      MINIAS_COLLECTOR_REPEAT_ERROR;
    strip_comment(copy, as->target);
    trimmed = minias_trim(copy);
    if(macro_argument!=NULL) {
        if (strncmp(trimmed, ".macro", 6U) == 0 &&
            (trimmed[6] == '\0' || isspace((unsigned char)trimmed[6]))) {
            rest = trimmed + 6;
            kind = MINIAS_MACRO_BEGIN;
        } else if (strcmp(trimmed, ".endm") == 0 ||
                   strcmp(trimmed, ".endmacro") == 0) {
            kind = MINIAS_MACRO_END;
        }
    }

    if(kind==MINIAS_MACRO_NONE && repeat_argument!=NULL) {
        char *cursor=trimmed;
        rest=NULL;
        /* GNU permits LABEL: before .rept/.irp; macro openers do not
         * recognize a leading label. Keep its original byte offset. */
        for(;;) {
            char *end=cursor;
            while(*end && !isspace((unsigned char)*end) && *end!=':')
                ++end;
            if(*end!=':' || end==cursor)break;
            cursor=end+1U;
            while(isspace((unsigned char)*cursor))++cursor;
        }
        if(cursor!=trimmed) {
            if(label_prefix_length)
                *label_prefix_length=(size_t)(cursor-copy);
            trimmed=cursor;
        }
        if (strncmp(trimmed, ".rept", 5U) == 0 &&
            (trimmed[5] == '\0' || isspace((unsigned char)trimmed[5]))) {
            rest=trimmed+5;
            kind=MINIAS_REPEAT_REPT;
        } else if (strncmp(trimmed, ".rep", 4U) == 0 &&
                   (trimmed[4] == '\0' || isspace((unsigned char)trimmed[4]))) {
            /* GNU .rep is a lexical alias for .rept, not another engine. */
            rest=trimmed+4;
            kind=MINIAS_REPEAT_REPT;
        } else if (strncmp(trimmed, ".irp", 4U) == 0 &&
                   (trimmed[4] == '\0' || isspace((unsigned char)trimmed[4]))) {
            rest=trimmed+4;
            kind=MINIAS_REPEAT_IRP;
        } else if (strcmp(trimmed, ".endr") == 0) {
            kind=MINIAS_REPEAT_ENDR;
        }
        if(label_prefix_length && kind!=MINIAS_REPEAT_REPT &&
           kind!=MINIAS_REPEAT_IRP)*label_prefix_length=0U;
    }
    /* The two collector grammars choose different directives, but neither
     * needs its own argument-copy and truncation implementation. */
    if(rest!=NULL) {
        const bool macro=kind==MINIAS_MACRO_BEGIN;
        char *destination=macro?macro_argument:repeat_argument;
        size_t capacity=macro?macro_size:repeat_size;
        while(*rest==' ' || *rest=='\t')++rest;
        if(capacity!=0U) {
            int written=snprintf(destination,capacity,"%s",rest);
            if(written<0 || (size_t)written>=capacity) {
                free(copy);
                return macro?MINIAS_COLLECTOR_MACRO_ERROR:
                             MINIAS_COLLECTOR_REPEAT_ERROR;
            }
        }
    }
    free(copy);
    return kind;
}

enum {
    MINIAS_MACRO_SEPARATOR_COMMA=1U,
    MINIAS_MACRO_SEPARATOR_SPACE=2U
};

/* GNU macro declaration and invocation tokenization share a single lexical
 * quote/escape/bracket scanner. Each caller selects comma, whitespace, or
 * both as top-level delimiters; quoted and nested text is never split. */
static const char *find_macro_top_level_separator(const char *text,
                                                   unsigned separators) {
    int depth = 0;
    char quote = '\0';
    for (; *text != '\0'; ++text) {
        if (quote != '\0') {
            if (*text == '\\' && text[1] != '\0') {
                ++text;
                continue;
            }
            if (*text == quote) quote = '\0';
            continue;
        }
        if (*text == '\'' || *text == '"') {
            quote = *text;
        } else if (*text == '(' || *text == '[' || *text == '{') {
            ++depth;
        } else if (*text == ')' || *text == ']' || *text == '}') {
            if (depth > 0) --depth;
        } else if (depth == 0 &&
                   (((separators&MINIAS_MACRO_SEPARATOR_COMMA)!=0U &&
                     *text==',') ||
                    ((separators&MINIAS_MACRO_SEPARATOR_SPACE)!=0U &&
                     (*text==' ' || *text=='\t')))) {
            return text;
        }
    }
    return NULL;
}

static size_t split_macro_tokens(char *text, char **tokens, size_t capacity) {
    size_t count=0U;
    char *p=text;
    while(*p!='\0') {
        char *separator;
        while(*p==' ' || *p=='\t' || *p==',')++p;
        if(*p=='\0')break;
        if(count==capacity)return SIZE_MAX;
        tokens[count++]=p;
        separator=(char *)find_macro_top_level_separator(
            p,MINIAS_MACRO_SEPARATOR_COMMA|MINIAS_MACRO_SEPARATOR_SPACE);
        if(separator==NULL)break;
        *separator='\0';
        p=separator+1U;
    }
    return count;
}

static bool macro_has_top_level_comma(const char *text) {
    return find_macro_top_level_separator(text,MINIAS_MACRO_SEPARATOR_COMMA)
           !=NULL;
}

static char *find_macro_argument_space(char *text) {
    return (char *)find_macro_top_level_separator(
        text,MINIAS_MACRO_SEPARATOR_SPACE);
}

static size_t split_macro_arguments(char *text,
                                    char **tokens,
                                    size_t capacity,
                                    size_t expected_count,
                                    bool preserve_parenthesized_operand) {
    char *segments[64];
    size_t segment_count = 0U;
    size_t count = 0U;
    char *cursor = text;
    size_t needed;
    bool last_was_comma = false;

    if (!macro_has_top_level_comma(text)) {
        /* GNU GAS treats a one-parameter arithmetic macro invocation
         * such as 'nops (32 - 16) / 4' as ONE argument, not the 3
         * whitespace tokens of the generic .macro parameter lexer.
         * Preserve strict rejection of 'single 1 2' (no expression
         * operator) and keep comma-separated multi-arg GNU binding. */
        if(expected_count==1U && capacity!=0U) {
            char *whole=minias_trim(text);
            if(*whole && strpbrk(whole,"+-*/%&|^")!=NULL) {
                tokens[0]=whole;
                return 1U;
            }
        }
        return split_macro_tokens(text, tokens, capacity);
    }

    for (;;) {
        char *p;

        while (*cursor == ' ' || *cursor == '\t') {
            ++cursor;
        }
        if (*cursor == '\0') {
            /* Explicit trailing comma is one more EMPTY positional
             * argument. Do not synthesize it without a delimiter. */
            if(last_was_comma) {
                if(segment_count==sizeof(segments)/sizeof(segments[0]))
                    return SIZE_MAX;
                segments[segment_count++]=cursor;
            }
            break;
        }
        if (segment_count == sizeof(segments) / sizeof(segments[0])) {
            return SIZE_MAX;
        }
        p=(char *)find_macro_top_level_separator(
            cursor,MINIAS_MACRO_SEPARATOR_COMMA);
        last_was_comma=p!=NULL;
        if(last_was_comma)*p='\0';
        segments[segment_count++] = minias_trim(cursor);
        if(!last_was_comma)break;
        cursor=p+1U;
    }

    needed = expected_count > segment_count
                 ? expected_count - segment_count
                 : 0U;
    for (size_t i = 0U; i < segment_count; ++i) {
        char *segment = segments[i];

        while (needed != 0U) {
            char *space = find_macro_argument_space(segment);
            char *rest;

            if (space == NULL) {
                break;
            }
            /* A GNU comma-delimited argument like
             *   ldr_l x0, symbol + 24
             * is ONE symbolic expression (the third 'tmp' is omitted),
             * not 'symbol' plus an invented tmp='+ 24'. Keep one Common
             * expression-aware lexical rule without ISA knowledge. */
            {
                char *rhs=space+1U,*lhs=space;
                while(*rhs==' ' || *rhs=='\t')++rhs;
                while(lhs>segment && (lhs[-1]==' ' || lhs[-1]=='\t'))
                    --lhs;
                if((*rhs && strchr("+-*/%&|^<>",*rhs)) ||
                   (lhs>segment && strchr("+-*/%&|^<>",lhs[-1])) ||
                   (preserve_parenthesized_operand && *rhs=='('))
                    break;
            }
            *space = '\0';
            rest = space + 1U;
            while (*rest == ' ' || *rest == '\t') {
                ++rest;
            }
            if (*segment == '\0' || *rest == '\0') {
                segment = rest;
                continue;
            }
            if (count == capacity) {
                return SIZE_MAX;
            }
            tokens[count++] = minias_trim(segment);
            segment = rest;
            --needed;
        }
        /* Blank comma-delimited arguments occupy a slot. Critical for
         * real Linux GHASH __pmull_p8_tail ...,8b,,sh1,... where the
         * deliberately empty 't' suffix must NOT consume sh1 and turn
         * the ISA mnemonic into invalid 'pmullsh1'. */
        if (count == capacity) return SIZE_MAX;
        tokens[count++] = minias_trim(segment);
    }
    return count;
}

static MiniAsMacro *minias_macro_find(MiniAs *as, const char *name) {
    size_t i;

    for (i = 0U; i < as->source->macro_count; ++i) {
        if (strcmp(as->source->macros[i].name, name) == 0) {
            return &as->source->macros[i];
        }
    }
    return NULL;
}

static void minias_macro_purge(MiniAsSourceContext *source, const char *name) {
    size_t i;
    for(i=0U;i<source->macro_count;++i) {
        if(strcmp(source->macros[i].name,name)!=0)continue;
        minias_macro_destroy(&source->macros[i]);
        if(i+1U<source->macro_count)
            memmove(&source->macros[i],&source->macros[i+1U],
                    (source->macro_count-i-1U)*sizeof(*source->macros));
        --source->macro_count;
        return;
    }
}

static void compact_macro_parameter_spacing(char *text) {
    char *read = text;
    char *write = text;

    while (*read != '\0') {
        if (*read == '=' || *read == ':') {
            while (write > text && (write[-1] == ' ' || write[-1] == '\t')) {
                --write;
            }
            *write++ = *read++;
            while (*read == ' ' || *read == '\t') {
                ++read;
            }
            continue;
        }
        *write++ = *read++;
    }
    *write = '\0';
}

static bool define_macro(MiniAs *as,
                         const char *argument,
                         const MiniAsSourceLine *lines,
                         size_t count,
                         size_t line) {
    char *copy = minias_strdup(argument);
    char *tokens[64];
    size_t token_count;
    MiniAsMacro *macro;
    size_t i;

    if (copy == NULL) {
        minias_set_error(as, "out-of-memory:macro-definition");
        return false;
    }
    compact_macro_parameter_spacing(copy);
    token_count = split_macro_tokens(copy, tokens, 64U);
    if (token_count == SIZE_MAX || token_count == 0U ||
        !valid_irp_parameter_name(tokens[0])) {
        minias_set_error(as, "bad-directive:.macro:line=%zu", line);
        goto failed;
    }
    if (minias_macro_find(as, tokens[0]) != NULL) {
        minias_set_error(as, "duplicate-macro:%s:line=%zu", tokens[0], line);
        goto failed;
    }
    if (!minias_grow_array((void **)&as->source->macros,
                    &as->source->macro_capacity,
                    sizeof(*as->source->macros),
                    as->source->macro_count + 1U)) {
        minias_set_error(as, "out-of-memory:macro");
        goto failed;
    }
    macro = &as->source->macros[as->source->macro_count];
    memset(macro, 0, sizeof(*macro));
    /* Every partially built macro must be visible to the sole destructor
     * even when a later parameter/body allocation fails. */
    ++as->source->macro_count;
    macro->definition_site=as->source->current_site;
    macro->name = minias_strdup(tokens[0]);
    if (macro->name == NULL) {
        minias_set_error(as, "out-of-memory:macro-name");
        goto failed;
    }
    if (token_count > 1U) {
        macro->params = calloc(token_count - 1U, sizeof(*macro->params));
        if (macro->params == NULL) {
            minias_set_error(as, "out-of-memory:macro-params");
            goto failed;
        }
    }
    macro->param_count = token_count - 1U;
    for (i = 1U; i < token_count; ++i) {
        char *spec = tokens[i];
        char *qualifier = strchr(spec, ':');
        char *equal = strchr(spec, '=');

        if (qualifier != NULL && (equal == NULL || qualifier < equal)) {
            *qualifier++ = '\0';
        } else if (equal != NULL) {
            *equal++ = '\0';
        }
        if (!valid_irp_parameter_name(spec)) {
            minias_set_error(as, "bad-macro-parameter:%s:line=%zu",
                             spec, line);
            goto failed;
        }
        macro->params[i - 1U].name = minias_strdup(spec);
        if (macro->params[i - 1U].name == NULL) {
            minias_set_error(as, "out-of-memory:macro-param-name");
            goto failed;
        }
        if (qualifier != NULL) {
            if(!strcmp(qualifier,"vararg") &&
               i+1U==token_count && equal==NULL) {
                macro->params[i-1U].variadic=true;
            }else if(!strcmp(qualifier,"req")) {
                macro->params[i-1U].required=true;
            }else {
                minias_set_error(as,"unsupported-macro-qualifier:%s:line=%zu",
                                 qualifier,line);
                goto failed;
            }
        }
        if (equal != NULL) {
            macro->params[i - 1U].default_value = minias_strdup(equal);
            if (macro->params[i - 1U].default_value == NULL) {
                minias_set_error(as, "out-of-memory:macro-param-default");
                goto failed;
            }
        }
    }
    if (count != 0U) {
        macro->body = calloc(count, sizeof(*macro->body));
        if (macro->body == NULL) {
            minias_set_error(as, "out-of-memory:macro-body");
            goto failed;
        }
    }
    macro->body_count = count;
    for (i = 0U; i < count; ++i) {
        macro->body[i].text = minias_strdup(lines[i].text);
        macro->body[i].line = lines[i].line;
        macro->body[i].source_segment=lines[i].source_segment;
        if (macro->body[i].text == NULL) {
            minias_set_error(as, "out-of-memory:macro-body-line");
            goto failed;
        }
    }
    free(copy);
    return true;
failed:
    free(copy);
    return false;
}

static char *substitute_macro_counter(MiniAs *as,
                                      const char *source,
                                      size_t counter) {
    char counter_text[32];
    size_t counter_len;
    size_t occurrences = 0U;
    size_t i;
    size_t source_len = strlen(source);
    char *output;
    size_t out = 0U;

    (void)snprintf(counter_text, sizeof(counter_text), "%zu", counter);
    counter_len = strlen(counter_text);
    for (i = 0U; i + 1U < source_len; ++i) {
        if (source[i] == '\\' && source[i + 1U] == '@') {
            ++occurrences;
            ++i;
        }
    }
    if (occurrences == 0U) {
        return minias_strdup(source);
    }
    if (counter_len > 2U &&
        occurrences > (SIZE_MAX - source_len) / (counter_len - 2U)) {
        minias_set_error(as, "source-size-overflow:macro-counter");
        return NULL;
    }
    output = malloc(source_len + occurrences * (counter_len - 2U) + 1U);
    if (output == NULL) {
        minias_set_error(as, "out-of-memory:macro-counter");
        return NULL;
    }
    for (i = 0U; i < source_len; ++i) {
        if (source[i] == '\\' && i + 1U < source_len &&
            source[i + 1U] == '@') {
            memcpy(output + out, counter_text, counter_len);
            out += counter_len;
            ++i;
        } else {
            output[out++] = source[i];
        }
    }
    output[out] = '\0';
    return output;
}

static bool process_source_range(MiniAs *as,
                                 MiniAsSourceLine *lines,
                                 size_t begin,
                                 size_t end);
static bool process_inline_repeat_line(MiniAs *as,const char *source,
                                       size_t line,bool *handled);

/* Collector entry is the same for physical FILE frames and expansion views.
 * Its caller owns frame storage; the recursive collector may borrow a slice
 * for nested macro and repetition bodies without making source copies. */
bool minias_source_frame_process(MiniAs *as,
                                 const MiniAsSourceFrame *frame) {
    MiniAsStableSiteId previous_expansion=as->source->current_expansion_site;
    MiniAsStableSiteId previous_site=as->source->current_site;
    bool ok;
    if(frame==NULL ||
       (frame->kind!=MINIAS_SOURCE_FRAME_FILE &&
        frame->kind!=MINIAS_SOURCE_FRAME_EXPANSION) ||
       (frame->count!=0U && frame->lines==NULL)) {
        minias_set_error(as,"invalid-source-frame");
        return false;
    }
    if(frame->kind==MINIAS_SOURCE_FRAME_EXPANSION) {
        const MiniAsSourceSiteKey key={
            .parent=frame->invocation_site,
            .definition=frame->definition_site,
            .iteration=frame->iteration,
            .kind=frame->expansion_kind
        };
        if(frame->invocation_site==MINIAS_SITE_NONE ||
           (frame->expansion_kind!=MINIAS_SOURCE_SITE_MACRO &&
            frame->expansion_kind!=MINIAS_SOURCE_SITE_REPT &&
            frame->expansion_kind!=MINIAS_SOURCE_SITE_IRP &&
            frame->expansion_kind!=MINIAS_SOURCE_SITE_INLINE_REPEAT)) {
            minias_set_error(as,"invalid-source-expansion-site");
            return false;
        }
        if(!minias_source_graph_intern(as,key,&as->source->current_expansion_site))
            return false;
    }else {
        as->source->current_expansion_site=MINIAS_SITE_NONE;
    }
    ok=process_source_range(as,frame->lines,0U,frame->count);
    as->source->current_expansion_site=previous_expansion;
    as->source->current_site=previous_site;
    return ok;
}

static char *strip_macro_argument_quotes(char *text) {
    size_t len;

    if (text == NULL) {
        return text;
    }
    len = strlen(text);
    if (len >= 2U &&
        ((text[0] == '"' && text[len - 1U] == '"') ||
         (text[0] == '\'' && text[len - 1U] == '\''))) {
        memmove(text, text + 1U, len - 2U);
        text[len - 2U] = '\0';
    }
    return text;
}

static bool minias_macro_expand(MiniAs *as,
                                    MiniAsMacro *macro,
                                    const char *args,
                                    size_t line) {
    char *copy = minias_strdup(args);
    char *tokens[64];
    char evaluated[64][32];
    const char *values[64];
    bool assigned[64]={false};
    char *vararg_buffer=NULL;
    size_t vararg_capacity=0U;
    size_t vararg_length=0U;
    bool has_vararg=macro->param_count>0U &&
           macro->params[macro->param_count-1U].variadic;
    size_t positional=0U;
    size_t supplied;
    MiniAsSourceLine *expanded = NULL;
    size_t i;
    size_t counter;
    /* .purgem or nested .macro can mutate the registry while executing
     * an already-expanded immutable source snapshot. Never dereference
     * the borrowed macro pointer after process_source_range returns. */
    size_t body_count=macro->body_count;
    bool ok = false;

    if (copy == NULL) {
        minias_set_error(as, "out-of-memory:macro-arguments");
        goto done;
    }
    supplied = split_macro_arguments(copy,
                                     tokens,
                                     64U,
                                     macro->param_count,
                                     as->target->macro_preserve_parenthesized_operand);
    if(supplied==SIZE_MAX || (!has_vararg && supplied>macro->param_count)) {
        minias_set_error(as, "bad-macro-arguments:%s:line=%zu",
                         macro->name, line);
        goto done;
    }
    /* GAS .altmacro evaluates %<absolute expression> at MACRO
     * invocation time. This is required by ARM64 fpsimdmacros.h's
     * recursive __for %\from, %((\from+\to)/2) generator; treating %0 as
     * a name changes control flow and silently bypasses generated ISA.
     * Conversion is bounded on-stack and happens BEFORE argument binding.
     * Modulo (%) inside a normal expression is never reinterpreted. */
    if(as->source->altmacro_active) {
        for(i=0U;i<supplied;++i) {
            int64_t value;
            const char *expression=tokens[i];
            if(*expression!='%')continue;
            ++expression;
            if(!*expression ||
               !minias_evaluate_absolute_expression(as,expression,&value)) {
                minias_set_error(as,"bad-altmacro-percent:%s:line=%zu",
                                 tokens[i],line);
                goto done;
            }
            (void)snprintf(evaluated[i],sizeof(evaluated[i]),"%lld",
                           (long long)value);
            tokens[i]=evaluated[i];
        }
    }
    if(has_vararg) {
        if(!minias_grow_array((void **)&vararg_buffer,&vararg_capacity,
                              sizeof(*vararg_buffer),1U)) {
            minias_set_error(as,"out-of-memory:macro-vararg");
            goto done;
        }
        vararg_buffer[0]='\0';
    }
    /* GNU GAS named macro arguments can appear in any order:
     * .macro SAVE_ALL reg=0 docfi=0 / SAVE_ALL docfi=1,reg=7.
     * Do not substitute the literal string "docfi=1" for \\docfi,
     * since that silently changes .if 1 to .if docfi=1.
     * Positional and keyword argument binding share this Common owner. */
    for(i=0U;i<macro->param_count;++i)
        values[i]=macro->params[i].default_value ?
                  macro->params[i].default_value:"";
    for(i=0U;i<supplied;++i) {
        char *token=tokens[i],*equal=strchr(token,'=');
        size_t index;
        if(equal && equal!=token && equal[1]!='=' &&
           equal[-1]!='=') {
            bool matched=false;
            char *key;
            *equal='\0';
            key=minias_trim(token);
            for(index=0U;index<macro->param_count;++index)
                if(!strcmp(key,macro->params[index].name)) {
                    matched=true;break;
                }
            if(!matched || assigned[index]) {
                minias_set_error(as,"bad-macro-keyword:%s:%s:line=%zu",
                                 macro->name,key,line);
                goto done;
            }
            {
                const char *item=strip_macro_argument_quotes(
                                      minias_trim(equal+1U));
                /* Empty GNU named/positional arguments retain their
                 * declaration defaults. ARM64 __frame extra=0 is invoked
                 * through an omitted optional outer parameter. */
                if(*item || !macro->params[index].default_value)
                    values[index]=item;
            }
        }else if(has_vararg &&
                 positional>=macro->param_count-1U) {
            const char *item=strip_macro_argument_quotes(token);
            size_t len=strlen(item);
            size_t separator=vararg_length!=0U?1U:0U;
            size_t required;
            index=macro->param_count-1U;
            if(assigned[index] && values[index]!=vararg_buffer) {
                minias_set_error(as,"duplicate-macro-vararg:%s:line=%zu",
                                 macro->name,line);
                goto done;
            }
            /* .altmacro's %expression can be longer than the source
             * spelling; never size substituted output from strlen(args).
             * Check before addition so an empty trailing comma cannot
             * underflow the remaining capacity after a full argument. */
            if(vararg_length>SIZE_MAX-1U-separator ||
               len>SIZE_MAX-1U-separator-vararg_length) {
                minias_set_error(as,"macro-vararg-too-long:line=%zu",line);
                goto done;
            }
            required=vararg_length+separator+len+1U;
            if(!minias_grow_array((void **)&vararg_buffer,&vararg_capacity,
                                  sizeof(*vararg_buffer),required)) {
                minias_set_error(as,"out-of-memory:macro-vararg");
                goto done;
            }
            if(separator)vararg_buffer[vararg_length++]=',';
            memcpy(vararg_buffer+vararg_length,item,len+1U);
            vararg_length+=len;
            values[index]=vararg_buffer;
            positional=macro->param_count;
        }else {
            while(positional<macro->param_count && assigned[positional])
                ++positional;
            if(positional>=macro->param_count) {
                minias_set_error(as,"too-many-macro-arguments:%s:line=%zu",
                                 macro->name,line);
                goto done;
            }
            index=positional++;
            {
                const char *item=strip_macro_argument_quotes(token);
                if(*item || !macro->params[index].default_value)
                    values[index]=item;
            }
        }
        assigned[index]=true;
    }
    for(i=0U;i<macro->param_count;++i)
        if((!assigned[i] || !*values[i]) &&
           macro->params[i].required) {
            minias_set_error(as,"missing-macro-argument:%s:%s:line=%zu",
                             macro->name,macro->params[i].name,line);
            goto done;
        }
    if (as->source->macro_expansion_depth >= 64U) {
        minias_set_error(as, "macro-expansion-depth:%s:line=%zu",
                         macro->name, line);
        goto done;
    }
    if (body_count != 0U) {
        expanded = calloc(body_count, sizeof(*expanded));
        if (expanded == NULL) {
            minias_set_error(as, "out-of-memory:macro-expanded-lines");
            goto done;
        }
    }
    counter = as->source->macro_expansion_counter++;
    for (i = 0U; i < body_count; ++i) {
        char *current = substitute_macro_counter(as, macro->body[i].text, counter);
        size_t p;

        if (current == NULL) {
            goto done;
        }
        for (p = 0U; p < macro->param_count; ++p) {
            char *next = substitute_irp_parameter(as,
                                                  current,
                                                  macro->params[p].name,
                                                  values[p]);
            free(current);
            if (next == NULL) {
                goto done;
            }
            current = next;
        }
        expanded[i].text = current;
        expanded[i].line = macro->body[i].line;
        expanded[i].source_segment=macro->body[i].source_segment;
    }
    ++as->source->macro_expansion_depth;
    {
        const MiniAsSourceFrame frame={
            .kind=MINIAS_SOURCE_FRAME_EXPANSION,
            .lines=expanded,
            .count=body_count,
            .expansion_kind=MINIAS_SOURCE_SITE_MACRO,
            .invocation_site=as->source->current_site,
            .definition_site=macro->definition_site
        };
        ok=minias_source_frame_process(as,&frame);
    }
    --as->source->macro_expansion_depth;

done:
    if (expanded != NULL)
        minias_source_lines_destroy(expanded, body_count);
    free(vararg_buffer);
    free(copy);
    return ok;
}

/* Macro registry mutation and invocation use one SourceContext owner.
 * The statement lowerer consumes only the decision and error result. */
bool minias_source_try_macro_invocation(MiniAs *as,char *op,char *args,
                                        size_t line,bool *handled) {
    MiniAsMacro *macro;
    *handled=true;
    /* Purging a missing macro is an idempotent GNU operation; an active
     * expansion already owns its substituted source snapshot. */
    if(!strcmp(op,".purgem")) {
        char *name=minias_trim(args);
        if(!*name || strpbrk(name,", \t\r")!=NULL) {
            minias_set_error(as,"bad-directive:.purgem:%s:line=%zu",name,line);
            return false;
        }
        minias_macro_purge(as->source,name);
        return true;
    }
    macro=minias_macro_find(as,op);
    if(macro!=NULL)return minias_macro_expand(as,macro,args,line);
    /* A bare MACRO(symbol) keeps parentheses as literal argument text;
     * temporarily NUL-terminate only the name lookup, then restore op. */
    if(args[0]=='\0' && op[0]!='.') {
        char *open=strchr(op,'(');
        size_t length=strlen(op);
        if(open!=NULL && open>op && length>1U && op[length-1U]==')') {
            *open='\0';
            macro=minias_macro_find(as,op);
            *open='(';
            if(macro!=NULL)return minias_macro_expand(as,macro,open,line);
        }
    }
    *handled=false;
    return true;
}

bool minias_source_try_control_directive(MiniAs *as,const char *op,
                                         const char *args,size_t line,
                                         bool *handled) {
    *handled=true;
    if(!strcmp(op,".altmacro") || !strcmp(op,".noaltmacro")) {
        if(*args!='\0') {
            minias_set_error(as,"bad-directive:%s:line=%zu",op,line);
            return false;
        }
        as->source->altmacro_active=strcmp(op,".altmacro")==0;
        return true;
    }
    if(!strcmp(op,".end")) {
        /* GAS accepts any trailing operand and closes the source stream. */
        as->source->source_ended=true;
        return true;
    }
    *handled=false;
    return true;
}

static char *find_statement_separator(char *text) {
    char quote = '\0';
    bool escaped = false;
    char *p;

    for (p = text; *p != '\0'; ++p) {
        if (escaped) {
            escaped = false;
            continue;
        }
        if (quote != '\0') {
            if (*p == '\\') {
                escaped = true;
            } else if (*p == quote) {
                quote = '\0';
            }
            continue;
        }
        if (*p == '\'' || *p == '"') {
            quote = *p;
            continue;
        }
        if (*p == ';') {
            return p;
        }
    }
    return NULL;
}

static bool process_source_line(MiniAs *as, const char *source, size_t line) {
    char *copy = minias_strdup(source);
    char *cursor;
    char *semicolon;
    MiniAsStableSiteId source_site=as->source->current_site;
    size_t segment=0U;
    bool split;
    bool success=false;

    if (copy == NULL) {
        minias_set_error(as, "out-of-memory:source-line");
        return false;
    }
    strip_comment(copy, as->target);
    semicolon=find_statement_separator(copy);
    split=semicolon!=NULL;
    cursor = copy;
    while (cursor != NULL && !as->source->source_ended) {
        if(split) {
            const MiniAsSourceSiteKey key={
                .parent=source_site,
                .iteration=segment,
                .kind=MINIAS_SOURCE_SITE_SEGMENT
            };
            if(!minias_source_graph_intern(as,key,&as->source->current_site)) {
                goto finished;
            }
        }
        if (semicolon != NULL) {
            *semicolon = '\0';
        }
        if (!minias_statement_process(as, cursor, line)) {
            goto finished;
        }
        cursor = semicolon == NULL ? NULL : semicolon + 1;
        semicolon = cursor == NULL ? NULL : find_statement_separator(cursor);
        ++segment;
    }
    success=true;
finished:
    free(copy);
    as->source->current_site=source_site;
    return success;
}

/* A .irp/.rept opener's leading label binds once at the
 * pre-expansion location, through Common's ordinary label parser. */
static bool emit_repeat_leading_label(MiniAs *as,const char *line,
                                      size_t prefix,size_t source_line) {
    char *copy;bool ok;
    if(prefix==0U)return true;
    if(prefix>strlen(line))return false;
    copy=malloc(prefix+1U);
    if(!copy){minias_set_error(as,"out-of-memory:repeat-leading-label");return false;}
    memcpy(copy,line,prefix);copy[prefix]='\0';
    ok=process_source_line(as,copy,source_line);
    free(copy);return ok;
}

static bool repeat_count_value(MiniAs *as,
                               const char *argument,
                               size_t line,
                               uint64_t *count) {
    int64_t value;
    char *rewritten = NULL;
    bool ok;

    /* GNU GAS permits 661b/662b local numeric labels in .rept counts,
     * including the kernel NOPS pattern (662b-661b)/4. Rewrite through
     * the same Common local-label owner as instructions and .byte; do
     * not clone target-specific numeric-label parsing. */
    if (!minias_numeric_refs_rewrite(as, argument, &rewritten, line)) {
        return false;
    }
    ok = *rewritten != '\0' &&
         minias_evaluate_absolute_expression(as, rewritten, &value) &&
         value >= 0;
    free(rewritten);
    if (!ok) {
        minias_set_error(as,
                         "unsupported-expression:.rept:%s:line=%zu",
                         argument,
                         line);
        return false;
    }
    if ((uint64_t)value > UINT32_MAX) {
        minias_set_error(as,
                         "repeat-count-too-large:%s:line=%zu",
                         argument,
                         line);
        return false;
    }
    *count = (uint64_t)value;
    return true;
}

static bool valid_irp_parameter_name(const char *name) {
    const unsigned char *p = (const unsigned char *)name;

    if (*p == '\0') {
        return false;
    }
    for (; *p != '\0'; ++p) {
        if (!isalnum(*p) && *p != '_') {
            return false;
        }
    }
    return true;
}

static bool irp_parameter_match(const char *text,
                                const char *name,
                                size_t name_len,
                                size_t *consumed) {
    size_t n = 1U + name_len;
    unsigned char next;

    if (text[0] != '\\' || strncmp(text + 1, name, name_len) != 0) {
        return false;
    }
    next = (unsigned char)text[n];
    if (isalnum(next) || next == '_') {
        return false;
    }
    if (text[n] == '\\' && text[n + 1U] == '(' &&
        text[n + 2U] == ')') {
        n += 3U;
    }
    *consumed = n;
    return true;
}

static char *substitute_irp_parameter(MiniAs *as,
                                      const char *source,
                                      const char *name,
                                      const char *value) {
    size_t source_pos = 0U;
    size_t output_size = 0U;
    size_t name_len = strlen(name);
    size_t value_len = strlen(value);
    char *output;
    size_t output_pos = 0U;

    while (source[source_pos] != '\0') {
        size_t consumed;
        if (irp_parameter_match(source + source_pos,
                                name,
                                name_len,
                                &consumed)) {
            if (value_len > SIZE_MAX - output_size) {
                minias_set_error(as, "source-size-overflow:.irp");
                return NULL;
            }
            output_size += value_len;
            source_pos += consumed;
        } else {
            if (output_size == SIZE_MAX) {
                minias_set_error(as, "source-size-overflow:.irp");
                return NULL;
            }
            ++output_size;
            ++source_pos;
        }
    }
    if (output_size == SIZE_MAX) {
        minias_set_error(as, "source-size-overflow:.irp");
        return NULL;
    }
    output = malloc(output_size + 1U);
    if (output == NULL) {
        minias_set_error(as, "out-of-memory:irp-substitution");
        return NULL;
    }

    source_pos = 0U;
    while (source[source_pos] != '\0') {
        size_t consumed;
        if (irp_parameter_match(source + source_pos,
                                name,
                                name_len,
                                &consumed)) {
            if (value_len != 0U) {
                memcpy(output + output_pos, value, value_len);
            }
            output_pos += value_len;
            source_pos += consumed;
        } else {
            output[output_pos++] = source[source_pos++];
        }
    }
    output[output_pos] = '\0';
    return output;
}

/* The .irp invocation owns one reusable expansion-line container. Recursed
 * frames borrow the array synchronously; only substituted text changes. */
static bool process_irp_block(MiniAs *as,
                              const MiniAsSourceLine *lines,
                              size_t count,
                              const char *argument,
                              size_t line,
                              MiniAsStableSiteId invocation_site) {
    char *copy=minias_strdup(argument);
    char *comma,*name,*cursor;
    MiniAsSourceLine *expanded=NULL;
    size_t iteration=0U;
    bool ok=false;

    if(copy==NULL) {
        minias_set_error(as,"out-of-memory:irp-argument");
        return false;
    }
    comma=strchr(copy,',');
    if(comma==NULL) {
        minias_set_error(as,"bad-directive:.irp:line=%zu",line);
        goto done;
    }
    *comma='\0';
    name=minias_trim(copy);
    if(!valid_irp_parameter_name(name)) {
        minias_set_error(as,"bad-directive:.irp:line=%zu",line);
        goto done;
    }

    cursor=comma+1U;
    for(;!as->source->source_ended;) {
        char *next_comma,*next_space,*next,*value;
        bool comma_delimited;
        size_t i;
        /* Keep the existing GNU comma/whitespace delimiter policy. */
        while(*cursor==' ' || *cursor=='\t')++cursor;
        next_comma=strchr(cursor,',');
        next_space=find_macro_argument_space(cursor);
        comma_delimited=next_comma!=NULL &&
                       (next_space==NULL || next_comma<next_space);
        next=comma_delimited?next_comma:next_space;
        if(next!=NULL)*next='\0';
        value=minias_trim(cursor);

        if(count!=0U && expanded==NULL) {
            expanded=calloc(count,sizeof(*expanded));
            if(expanded==NULL) {
                minias_set_error(as,"out-of-memory:irp-lines");
                goto done;
            }
        }
        for(i=0U;i<count;++i) {
            expanded[i].line=lines[i].line;
            expanded[i].source_segment=lines[i].source_segment;
            expanded[i].text=substitute_irp_parameter(as,lines[i].text,
                                                      name,value);
            if(expanded[i].text==NULL)goto done;
        }
        const MiniAsSourceFrame frame={
            .kind=MINIAS_SOURCE_FRAME_EXPANSION,
            .lines=expanded,
            .count=count,
            .expansion_kind=MINIAS_SOURCE_SITE_IRP,
            .invocation_site=invocation_site,
            .definition_site=invocation_site,
            .iteration=iteration
        };
        if(!minias_source_frame_process(as,&frame))goto done;
        /* The borrowed SourceFrame has returned; release the strings but
         * preserve the zero-owned array slots for the following value. */
        for(i=0U;i<count;++i) {
            free(expanded[i].text);
            expanded[i].text=NULL;
        }
        ++iteration;
        if(next==NULL)break;
        cursor=next+1U;
        /* Real trailing commas produce empty arguments; trailing whitespace
         * without a comma does not invent a new positional argument. */
        if(!comma_delimited) {
            while(*cursor==' ' || *cursor=='\t')++cursor;
            if(*cursor=='\0')break;
        }
    }
    ok=true;
done:
    if(expanded!=NULL)minias_source_lines_destroy(expanded,count);
    free(copy);
    return ok;
}

/* Macro and repeat collectors differ in delimiter grammar, not in their
 * nesting walk. Only outer collectors consume arguments; nested scans
 * identify delimiters without imposing a second parameter-length limit. */
static bool find_collector_close(MiniAs *as,const MiniAsSourceLine *lines,
                                 size_t opening,size_t end,int kind,
                                 size_t *closing) {
    const bool macro=kind==MINIAS_MACRO_BEGIN;
    const char *directive=macro?".macro":
                          kind==MINIAS_REPEAT_IRP?".irp":".rept";
    size_t depth=1U,j=opening+1U;
    for(;j<end;++j) {
        /* Only the delimiter kind is used during nesting. Do not copy
         * discarded arguments into a capped 1 KiB/256-byte scratch array
         * or reject nested directives just because their text is long. */
        char unused_argument;
        int nested=classify_collector_line(as,lines[j].text,
                    macro?&unused_argument:NULL,0U,
                    macro?NULL:&unused_argument,0U,NULL);
        if(nested<0) {
            minias_set_error(as,"%s",macro?
                "out-of-memory:macro-classify":
                "out-of-memory:repeat-classify");
            return false;
        }
        if(macro?nested==MINIAS_MACRO_BEGIN:
           (nested==MINIAS_REPEAT_REPT || nested==MINIAS_REPEAT_IRP)) {
            ++depth;
        }else if(nested==(macro?MINIAS_MACRO_END:MINIAS_REPEAT_ENDR) &&
                 --depth==0U) {
            *closing=j;
            return true;
        }
    }
    minias_set_error(as,"unterminated-directive:%s:line=%zu",
                     directive,lines[opening].line);
    return false;
}

static bool process_source_range(MiniAs *as,
                                 MiniAsSourceLine *lines,
                                 size_t begin,
                                 size_t end) {
    size_t i = begin;
    /* Reuse one checked buffer for mutually exclusive macro/repeat args. */
    char *collector_argument=NULL;
    size_t collector_capacity=0U;

    while (i < end && !as->source->source_ended) {
        MiniAsStableSiteId line_site;
        bool inline_handled=false;
        size_t repeat_label_prefix=0U;
        int kind;

        if(!minias_source_graph_line_site(as,&lines[i],&line_site))goto failed;
        as->source->current_site=line_site;

        /* File and expansion frames must agree on the semicolon-separated
         * GNU inline `.rept/.irp ... .endr` grammar. Dispatch that one
         * lexical special case before collector classification in BOTH. */
        if(!process_inline_repeat_line(as,lines[i].text,lines[i].line,
                                       &inline_handled))goto failed;
        if(inline_handled) {++i;continue;}

        /* GAS does not lex .macro/.rept/.irp at all in an inactive .if
         * branch. Only nested conditionals can reactivate parsing. In
         * particular, an unterminated .macro and unmatched .endm/.endr in
         * skipped text are harmless source, not collector syntax. */
        if(!as->source->conditional_active) {
            if(!process_source_line(as,lines[i].text,lines[i].line))goto failed;
            ++i;
            continue;
        }

        /* The parameter text cannot exceed the source line itself. */
        size_t needed=strlen(lines[i].text)+1U;
        if(!minias_grow_array((void **)&collector_argument,
                              &collector_capacity,1U,needed)) {
            minias_set_error(as,"out-of-memory:source-collector-arguments");
            goto failed;
        }
        kind=classify_collector_line(as,lines[i].text,
                                     collector_argument,collector_capacity,
                                     collector_argument,collector_capacity,
                                     &repeat_label_prefix);
        if (kind < 0) {
            minias_set_error(as, "%s",kind==MINIAS_COLLECTOR_MACRO_ERROR ?
                             "out-of-memory:macro-classify" :
                             "out-of-memory:repeat-classify");
            goto failed;
        }
        if (kind == MINIAS_MACRO_END) {
            minias_set_error(as,
                             "unmatched-directive:.endm:line=%zu",
                             lines[i].line);
            goto failed;
        }
        if (kind == MINIAS_MACRO_BEGIN) {
            size_t j;
            if(!find_collector_close(as,lines,i,end,kind,&j))goto failed;
            if (!define_macro(as,
                              collector_argument,
                              lines + i + 1U,
                              j - i - 1U,
                              lines[i].line))goto failed;
            i = j + 1U;
            continue;
        }

        if (kind == MINIAS_REPEAT_ENDR) {
            minias_set_error(as,
                             "unmatched-directive:.endr:line=%zu",
                             lines[i].line);
            goto failed;
        }
        if (kind == MINIAS_REPEAT_REPT || kind == MINIAS_REPEAT_IRP) {
            size_t j;
            if(!emit_repeat_leading_label(as,lines[i].text,
                                          repeat_label_prefix,
                                          lines[i].line))goto failed;
            if(!find_collector_close(as,lines,i,end,kind,&j))goto failed;
            if (kind == MINIAS_REPEAT_REPT) {
                uint64_t repetitions;
                uint64_t iteration;

                if (!repeat_count_value(as,
                                        collector_argument,
                                        lines[i].line,
                                        &repetitions)) {
                    goto failed;
                }
                for (iteration = 0U;
                     iteration < repetitions && !as->source->source_ended;
                     ++iteration) {
                    const MiniAsSourceFrame body={
                        .kind=MINIAS_SOURCE_FRAME_EXPANSION,
                        .lines=lines+i+1U,
                        .count=j-i-1U,
                        .expansion_kind=MINIAS_SOURCE_SITE_REPT,
                        .invocation_site=line_site,
                        .definition_site=line_site,
                        .iteration=(size_t)iteration
                    };
                    if (!minias_source_frame_process(as,&body)) {
                        goto failed;
                    }
                }
            } else if (!process_irp_block(as,
                                          lines + i + 1U,
                                          j - i - 1U,
                                          collector_argument,
                                          lines[i].line,
                                          line_site)) {
                goto failed;
            }
            i = j + 1U;
            continue;
        }
        if (!process_source_line(as, lines[i].text, lines[i].line)) {
            goto failed;
        }
        ++i;
    }
    free(collector_argument);
    return true;
failed:
    free(collector_argument);
    return false;
}

/* GNU GAS permits semicolon-separated assignments and complete .rept/.irp
 * blocks on ONE physical source line (Linux head_64.S page tables).  Route
 * logical statements back through the existing Common repeat/macro engine;
 * do not recognize repeat syntax a second time in Target parsers. */
static bool process_inline_repeat_line(MiniAs *as,const char *source,
                                       size_t line,bool *handled) {
    MiniAsSourceLine *pieces=NULL;
    size_t count=0U,capacity=0U;
    char *copy,*cursor;
    bool opener=false,closer=false,ok=true;
    *handled=false;
    if(strchr(source,';')==NULL)return true;
    copy=minias_strdup(source);
    if(copy==NULL) {
        minias_set_error(as,"out-of-memory:inline-repeat");return false;
    }
    strip_comment(copy,as->target);
    cursor=copy;
    while(cursor!=NULL) {
        char *end=find_statement_separator(cursor);
        char unused_argument;
        int kind;
        if(end!=NULL)*end='\0';
        /* Kind-only inline recognition must not copy unused arguments. */
        kind=classify_collector_line(as,cursor,NULL,0U,
                                     &unused_argument,0U,NULL);
        if(kind<0 || !minias_grow_array((void **)&pieces,&capacity,
                                   sizeof(*pieces),count+1U)) {
            minias_set_error(as,"out-of-memory:inline-repeat-segments");
            ok=false;break;
        }
        pieces[count].text=cursor;
        pieces[count].line=line;
        pieces[count].source_segment=count;
        ++count;
        if(kind==MINIAS_REPEAT_REPT || kind==MINIAS_REPEAT_IRP)opener=true;
        if(kind==MINIAS_REPEAT_ENDR)closer=true;
        cursor=end==NULL?NULL:end+1U;
    }
    if(ok && opener && closer) {
        const MiniAsSourceFrame frame={
            .kind=MINIAS_SOURCE_FRAME_EXPANSION,
            .lines=pieces,
            .count=count,
            .expansion_kind=MINIAS_SOURCE_SITE_INLINE_REPEAT,
            .invocation_site=as->source->current_site,
            .definition_site=as->source->current_site
        };
        *handled=true;
        ok=minias_source_frame_process(as,&frame);
    }
    free(pieces);
    free(copy);
    return ok;
}

