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
void minias_macro_destroy(MiniAsMacro *macro) {
    size_t i;
    free(macro->name);
    for(i=0U;i<macro->param_count;++i) {
        free(macro->params[i].name);
        free(macro->params[i].default_value);
    }
    free(macro->params);
    for(i=0U;i<macro->body_count;++i)
        free(macro->body[i]);
    free(macro->body);
    free(macro->body_lines);
    free(macro->body_source_segments);
    memset(macro,0,sizeof(*macro));
}

/* Source expansion owns the entire fresh-pass lexical/conditional lifetime.
 * The persistent graph is borrowed and is released separately by the
 * invocation after its final semantic pass (including failure paths). */
void minias_source_context_destroy(MiniAsSourceContext *source) {
    size_t i;
    if(source==NULL)return;
    free(source->conditionals);
    for(i=0U;i<source->macro_count;++i)
        minias_macro_destroy(&source->macros[i]);
    free(source->macros);
    memset(source,0,sizeof(*source));
}

static bool valid_irp_parameter_name(const char *name);
static char *substitute_irp_parameter(MiniAs *as,
                                      const char *source,
                                      const char *name,
                                      const char *value);

enum {
    MINIAS_MACRO_NONE = 0,
    MINIAS_MACRO_BEGIN = 1,
    MINIAS_MACRO_END = 2
};

static int classify_macro_line(const MiniAs *as, const char *text,
                               char *argument,
                               size_t argument_size) {
    char *copy = minias_strdup(text);
    char *trimmed;
    const char *rest = NULL;
    int kind = MINIAS_MACRO_NONE;

    if (argument_size != 0U) {
        argument[0] = '\0';
    }
    if (copy == NULL) {
        return -1;
    }
    strip_comment(copy, as->target);
    trimmed = minias_trim(copy);
    if (strncmp(trimmed, ".macro", 6U) == 0 &&
        (trimmed[6] == '\0' || isspace((unsigned char)trimmed[6]))) {
        rest = trimmed + 6;
        kind = MINIAS_MACRO_BEGIN;
    } else if (strcmp(trimmed, ".endm") == 0 ||
               strcmp(trimmed, ".endmacro") == 0) {
        kind = MINIAS_MACRO_END;
    }
    if (rest != NULL) {
        while (*rest == ' ' || *rest == '\t') {
            ++rest;
        }
        if (argument_size != 0U) {
            int written = snprintf(argument, argument_size, "%s", rest);
            if (written < 0 || (size_t)written >= argument_size) {
                free(copy);
                return -1;
            }
        }
    }
    free(copy);
    return kind;
}

static size_t split_macro_tokens(char *text, char **tokens, size_t capacity) {
    size_t count = 0U;
    char *p = text;

    while (*p != '\0') {
        char *start;
        int depth = 0;
        char quote = '\0';

        while (*p == ' ' || *p == '\t' || *p == ',') {
            ++p;
        }
        if (*p == '\0') {
            break;
        }
        if (count == capacity) {
            return SIZE_MAX;
        }
        start = p;
        while (*p != '\0') {
            if (quote != '\0') {
                if (*p == '\\' && p[1] != '\0') {
                    p += 2;
                    continue;
                }
                if (*p == quote) {
                    quote = '\0';
                }
                ++p;
                continue;
            }
            if (*p == '\'' || *p == '"') {
                quote = *p++;
                continue;
            }
            if (*p == '(' || *p == '[' || *p == '{') {
                ++depth;
                ++p;
                continue;
            }
            if (*p == ')' || *p == ']' || *p == '}') {
                if (depth > 0) {
                    --depth;
                }
                ++p;
                continue;
            }
            if (depth == 0 &&
                (*p == ',' || *p == ' ' || *p == '\t')) {
                break;
            }
            ++p;
        }
        if (*p != '\0') {
            *p++ = '\0';
        }
        tokens[count++] = start;
    }
    return count;
}

static bool macro_has_top_level_comma(const char *text) {
    int depth = 0;
    char quote = '\0';

    for (; *text != '\0'; ++text) {
        if (quote != '\0') {
            if (*text == '\\' && text[1] != '\0') {
                ++text;
                continue;
            }
            if (*text == quote) {
                quote = '\0';
            }
            continue;
        }
        if (*text == '\'' || *text == '"') {
            quote = *text;
        } else if (*text == '(' || *text == '[' || *text == '{') {
            ++depth;
        } else if (*text == ')' || *text == ']' || *text == '}') {
            if (depth > 0) {
                --depth;
            }
        } else if (*text == ',' && depth == 0) {
            return true;
        }
    }
    return false;
}

static char *find_macro_argument_space(char *text) {
    int depth = 0;
    char quote = '\0';
    char *p;

    for (p = text; *p != '\0'; ++p) {
        if (quote != '\0') {
            if (*p == '\\' && p[1] != '\0') {
                ++p;
                continue;
            }
            if (*p == quote) {
                quote = '\0';
            }
            continue;
        }
        if (*p == '\'' || *p == '"') {
            quote = *p;
        } else if (*p == '(' || *p == '[' || *p == '{') {
            ++depth;
        } else if (*p == ')' || *p == ']' || *p == '}') {
            if (depth > 0) {
                --depth;
            }
        } else if (depth == 0 && (*p == ' ' || *p == '\t')) {
            return p;
        }
    }
    return NULL;
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
        char *start;
        char *p;
        int depth = 0;
        char quote = '\0';

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
        start = cursor;
        p = cursor;
        while (*p != '\0') {
            if (quote != '\0') {
                if (*p == '\\' && p[1] != '\0') {
                    p += 2;
                    continue;
                }
                if (*p == quote) {
                    quote = '\0';
                }
                ++p;
                continue;
            }
            if (*p == '\'' || *p == '"') {
                quote = *p++;
                continue;
            }
            if (*p == '(' || *p == '[' || *p == '{') {
                ++depth;
                ++p;
                continue;
            }
            if (*p == ')' || *p == ']' || *p == '}') {
                if (depth > 0) {
                    --depth;
                }
                ++p;
                continue;
            }
            if (*p == ',' && depth == 0) {
                break;
            }
            ++p;
        }
        last_was_comma=*p==',';
        if(last_was_comma)*p++ = '\0';
        segments[segment_count++] = minias_trim(start);
        cursor = p;
        if(!last_was_comma)break;
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

MiniAsMacro *minias_macro_find(MiniAs *as, const char *name) {
    size_t i;

    for (i = 0U; i < as->source.macro_count; ++i) {
        if (strcmp(as->source.macros[i].name, name) == 0) {
            return &as->source.macros[i];
        }
    }
    return NULL;
}

void minias_macro_purge(MiniAsSourceContext *source, const char *name) {
    size_t i;
    for(i=0U;i<source->macro_count;++i) {
        if(strcmp(source->macros[i].name,name)!=0)continue;
        minias_macro_destroy(&source->macros[i]);
        if(i+1U<source->macro_count)
            memmove(&source->macros[i],&source->macros[i+1U],
                    (source->macro_count-i-1U)*sizeof(*source->macros));
        --source->macro_count;
        memset(&source->macros[source->macro_count],0,
               sizeof(*source->macros));
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
                         MiniAsSourceLine *lines,
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
        free(copy);
        return false;
    }
    if (minias_macro_find(as, tokens[0]) != NULL) {
        minias_set_error(as, "duplicate-macro:%s:line=%zu", tokens[0], line);
        free(copy);
        return false;
    }
    if (!minias_grow_array((void **)&as->source.macros,
                    &as->source.macro_capacity,
                    sizeof(*as->source.macros),
                    as->source.macro_count + 1U)) {
        minias_set_error(as, "out-of-memory:macro");
        free(copy);
        return false;
    }
    macro = &as->source.macros[as->source.macro_count];
    memset(macro, 0, sizeof(*macro));
    macro->definition_site=as->source.current_site;
    macro->name = minias_strdup(tokens[0]);
    if (macro->name == NULL) {
        minias_set_error(as, "out-of-memory:macro-name");
        free(copy);
        return false;
    }
    if (token_count > 1U) {
        macro->params = calloc(token_count - 1U, sizeof(*macro->params));
        if (macro->params == NULL) {
            minias_set_error(as, "out-of-memory:macro-params");
            free(copy);
            return false;
        }
    }
    macro->param_count = token_count - 1U;
    for (i = 1U; i < token_count; ++i) {
        char *spec = tokens[i];
        char *qualifier = strchr(spec, ':');
        char *equal = strchr(spec, '=');
        char *end = NULL;

        if (qualifier != NULL && (equal == NULL || qualifier < equal)) {
            *qualifier++ = '\0';
            end = qualifier - 1;
        } else if (equal != NULL) {
            *equal++ = '\0';
            end = equal - 1;
        }
        if (!valid_irp_parameter_name(spec)) {
            minias_set_error(as, "bad-macro-parameter:%s:line=%zu",
                             spec, line);
            free(copy);
            return false;
        }
        macro->params[i - 1U].name = minias_strdup(spec);
        if (macro->params[i - 1U].name == NULL) {
            minias_set_error(as, "out-of-memory:macro-param-name");
            free(copy);
            return false;
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
                free(copy);
                return false;
            }
        }
        if (equal != NULL) {
            macro->params[i - 1U].default_value = minias_strdup(equal);
            if (macro->params[i - 1U].default_value == NULL) {
                minias_set_error(as, "out-of-memory:macro-param-default");
                free(copy);
                return false;
            }
        }
        (void)end;
    }
    if (count != 0U) {
        macro->body = calloc(count, sizeof(*macro->body));
        macro->body_lines = calloc(count, sizeof(*macro->body_lines));
        macro->body_source_segments=calloc(count,
                              sizeof(*macro->body_source_segments));
        if (macro->body == NULL || macro->body_lines == NULL ||
            macro->body_source_segments == NULL) {
            minias_set_error(as, "out-of-memory:macro-body");
            free(copy);
            return false;
        }
    }
    macro->body_count = count;
    for (i = 0U; i < count; ++i) {
        macro->body[i] = minias_strdup(lines[i].text);
        macro->body_lines[i] = lines[i].line;
        macro->body_source_segments[i]=lines[i].source_segment;
        if (macro->body[i] == NULL) {
            minias_set_error(as, "out-of-memory:macro-body-line");
            free(copy);
            return false;
        }
    }
    ++as->source.macro_count;
    free(copy);
    return true;
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
    MiniAsStableSiteId previous_expansion=as->source.current_expansion_site;
    MiniAsStableSiteId previous_site=as->source.current_site;
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
        if(!minias_source_graph_intern(as,key,&as->source.current_expansion_site))
            return false;
    }else {
        as->source.current_expansion_site=MINIAS_SITE_NONE;
    }
    ok=process_source_range(as,frame->lines,0U,frame->count);
    as->source.current_expansion_site=previous_expansion;
    as->source.current_site=previous_site;
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

bool minias_macro_expand(MiniAs *as,
                                    MiniAsMacro *macro,
                                    const char *args,
                                    size_t line) {
    char *copy = minias_strdup(args);
    char *tokens[64];
    char evaluated[64][32];
    const char *values[64];
    bool assigned[64]={false};
    char *vararg_buffer=NULL;
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
        return false;
    }
    supplied = split_macro_arguments(copy,
                                     tokens,
                                     64U,
                                     macro->param_count,
                                     as->target->macro_preserve_parenthesized_operand);
    if(supplied==SIZE_MAX || (!has_vararg && supplied>macro->param_count)) {
        minias_set_error(as, "bad-macro-arguments:%s:line=%zu",
                         macro->name, line);
        free(vararg_buffer);
        free(copy);
        return false;
    }
    /* GAS .altmacro evaluates %<absolute expression> at MACRO
     * invocation time. This is required by ARM64 fpsimdmacros.h's
     * recursive __for %\from, %((\from+\to)/2) generator; treating %0 as
     * a name changes control flow and silently bypasses generated ISA.
     * Conversion is bounded on-stack and happens BEFORE argument binding.
     * Modulo (%) inside a normal expression is never reinterpreted. */
    if(as->source.altmacro_active) {
        for(i=0U;i<supplied;++i) {
            int64_t value;
            const char *expression=tokens[i];
            if(*expression!='%')continue;
            ++expression;
            if(!*expression ||
               !minias_evaluate_absolute_expression(as,expression,&value)) {
                minias_set_error(as,"bad-altmacro-percent:%s:line=%zu",
                                 tokens[i],line);
                free(copy);return false;
            }
            (void)snprintf(evaluated[i],sizeof(evaluated[i]),"%lld",
                           (long long)value);
            tokens[i]=evaluated[i];
        }
    }
    if(has_vararg) {
        vararg_buffer=malloc(strlen(args)+1U);
        if(!vararg_buffer) {
            minias_set_error(as,"out-of-memory:macro-vararg");
            free(vararg_buffer);free(copy);return false;
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
                free(vararg_buffer);free(copy);return false;
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
            size_t len=strlen(item),max=strlen(args);
            index=macro->param_count-1U;
            if(assigned[index] && values[index]!=vararg_buffer) {
                minias_set_error(as,"duplicate-macro-vararg:%s:line=%zu",
                                 macro->name,line);
                free(vararg_buffer);free(copy);return false;
            }
            if(len>max-vararg_length-
                   (vararg_length!=0U?1U:0U)) {
                minias_set_error(as,"macro-vararg-too-long:line=%zu",line);
                free(vararg_buffer);free(copy);return false;
            }
            if(vararg_length)vararg_buffer[vararg_length++]=',';
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
                free(vararg_buffer);free(copy);return false;
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
            free(vararg_buffer);free(copy);return false;
        }
    if (as->source.macro_expansion_depth >= 64U) {
        minias_set_error(as, "macro-expansion-depth:%s:line=%zu",
                         macro->name, line);
        free(vararg_buffer);
        free(copy);
        return false;
    }
    if (body_count != 0U) {
        expanded = calloc(body_count, sizeof(*expanded));
        if (expanded == NULL) {
            minias_set_error(as, "out-of-memory:macro-expanded-lines");
            free(copy);
            return false;
        }
    }
    counter = as->source.macro_expansion_counter++;
    for (i = 0U; i < body_count; ++i) {
        char *current = substitute_macro_counter(as, macro->body[i], counter);
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
        expanded[i].line = macro->body_lines[i];
        expanded[i].source_segment=macro->body_source_segments[i];
    }
    ++as->source.macro_expansion_depth;
    {
        const MiniAsSourceFrame frame={
            .kind=MINIAS_SOURCE_FRAME_EXPANSION,
            .lines=expanded,
            .count=body_count,
            .expansion_kind=MINIAS_SOURCE_SITE_MACRO,
            .invocation_site=as->source.current_site,
            .definition_site=macro->definition_site
        };
        ok=minias_source_frame_process(as,&frame);
    }
    --as->source.macro_expansion_depth;

done:
    minias_source_lines_destroy(expanded, body_count);
    free(vararg_buffer);
    free(copy);
    return ok;
}

enum {
    MINIAS_REPEAT_NONE = 0,
    MINIAS_REPEAT_REPT = 1,
    MINIAS_REPEAT_ENDR = 2,
    MINIAS_REPEAT_IRP = 3
};

static int classify_repeat_line(const MiniAs *as, const char *text,
                                char *argument,
                                size_t argument_size,
                                size_t *label_prefix_length) {
    char *copy;
    char *trimmed;
    const char *rest = NULL;
    int kind = MINIAS_REPEAT_NONE;

    if (argument_size != 0U) {
        argument[0] = '\0';
    }
    copy = minias_strdup(text);
    if (copy == NULL) {
        return -1;
    }
    strip_comment(copy, as->target);
    trimmed = minias_trim(copy);
    if(label_prefix_length)*label_prefix_length=0U;
    /* GNU allows LABEL: .irp/.rept ...; classify the opener after
     * its lexical labels, while preserving their source offset so
     * the shared label handler binds them once, BEFORE expansion. */
    {
        char *cursor=trimmed;
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
    }

    if (strncmp(trimmed, ".rept", 5U) == 0 &&
        (trimmed[5] == '\0' || isspace((unsigned char)trimmed[5]))) {
        rest = trimmed + 5;
        kind = MINIAS_REPEAT_REPT;
    } else if (strncmp(trimmed, ".rep", 4U) == 0 &&
               (trimmed[4] == '\0' || isspace((unsigned char)trimmed[4]))) {
        /* GNU .rep is the same text repetition directive as .rept.
         * Resolve only the lexical alias; Common IRP/repetition owns
         * balanced nesting, numeric evaluation and source fragments. */
        rest = trimmed + 4;
        kind = MINIAS_REPEAT_REPT;
    } else if (strncmp(trimmed, ".irp", 4U) == 0 &&
               (trimmed[4] == '\0' ||
                isspace((unsigned char)trimmed[4]))) {
        rest = trimmed + 4;
        kind = MINIAS_REPEAT_IRP;
    } else if (strcmp(trimmed, ".endr") == 0) {
        kind = MINIAS_REPEAT_ENDR;
    }

    if (rest != NULL) {
        while (*rest == ' ' || *rest == '\t') {
            ++rest;
        }
        if (argument_size != 0U) {
            int written = snprintf(argument, argument_size, "%s", rest);
            if (written < 0 || (size_t)written >= argument_size) {
                free(copy);
                return -1;
            }
        }
    }

    if(label_prefix_length &&
       kind!=MINIAS_REPEAT_REPT && kind!=MINIAS_REPEAT_IRP)
        *label_prefix_length=0U;
    free(copy);
    return kind;
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
    MiniAsStableSiteId source_site=as->source.current_site;
    size_t segment=0U;
    bool split;

    if (copy == NULL) {
        minias_set_error(as, "out-of-memory:source-line");
        return false;
    }
    strip_comment(copy, as->target);
    split=find_statement_separator(copy)!=NULL;
    cursor = copy;
    while (cursor != NULL && !as->source.source_ended) {
        char *semicolon = find_statement_separator(cursor);
        if(split) {
            const MiniAsSourceSiteKey key={
                .parent=source_site,
                .iteration=segment,
                .kind=MINIAS_SOURCE_SITE_SEGMENT
            };
            if(!minias_source_graph_intern(as,key,&as->source.current_site)) {
                free(copy);
                as->source.current_site=source_site;
                return false;
            }
        }
        if (semicolon != NULL) {
            *semicolon = '\0';
        }
        if (!minias_statement_process(as, cursor, line)) {
            free(copy);
            as->source.current_site=source_site;
            return false;
        }
        cursor = semicolon == NULL ? NULL : semicolon + 1;
        ++segment;
    }
    free(copy);
    as->source.current_site=source_site;
    return true;
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

static bool process_source_range(MiniAs *as,
                                 MiniAsSourceLine *lines,
                                 size_t begin,
                                 size_t end);

static bool process_irp_block(MiniAs *as,
                              MiniAsSourceLine *lines,
                              size_t count,
                              const char *argument,
                              size_t line,
                              MiniAsStableSiteId invocation_site) {
    char *copy = minias_strdup(argument);
    char *comma;
    char *name;
    char *cursor;
    size_t iteration=0U;

    if (copy == NULL) {
        minias_set_error(as, "out-of-memory:irp-argument");
        return false;
    }
    comma = strchr(copy, ',');
    if (comma == NULL) {
        minias_set_error(as, "bad-directive:.irp:line=%zu", line);
        free(copy);
        return false;
    }
    *comma = '\0';
    name = minias_trim(copy);
    if (!valid_irp_parameter_name(name)) {
        minias_set_error(as, "bad-directive:.irp:line=%zu", line);
        free(copy);
        return false;
    }

    cursor = comma + 1;
    for (; !as->source.source_ended;) {
        char *next_comma;
        char *next_space;
        char *next;
        char *value;
        bool comma_delimited;
        MiniAsSourceLine *expanded = NULL;
        size_t i;

        /* GNU GAS .irp accepts BOTH commas and unquoted whitespace
         * between values, even in the same list. Original Linux GHASH:
         *   .irp key, K0, K1, K2, K3, K4 K5
         * must expand K4 and K5 separately. Reuse the shared macro
         * whitespace lexer (which respects quotes and brackets). */
        while (*cursor == ' ' || *cursor == '\t') ++cursor;
        next_comma = strchr(cursor, ',');
        next_space = find_macro_argument_space(cursor);
        comma_delimited = next_comma != NULL &&
                          (next_space == NULL || next_comma < next_space);
        next = comma_delimited ? next_comma : next_space;
        if (next != NULL) *next = '\0';
        value = minias_trim(cursor);
        if (count != 0U) {
            expanded = calloc(count, sizeof(*expanded));
            if (expanded == NULL) {
                minias_set_error(as, "out-of-memory:irp-lines");
                free(copy);
                return false;
            }
        }
        for (i = 0U; i < count; ++i) {
            expanded[i].line = lines[i].line;
            expanded[i].source_segment=lines[i].source_segment;
            expanded[i].text =
                substitute_irp_parameter(as, lines[i].text, name, value);
            if (expanded[i].text == NULL) {
                minias_source_lines_destroy(expanded, count);
                free(copy);
                return false;
            }
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
        if (!minias_source_frame_process(as,&frame)) {
            minias_source_lines_destroy(expanded, count);
            free(copy);
            return false;
        }
        minias_source_lines_destroy(expanded, count);
        ++iteration;
        if (next == NULL) {
            break;
        }
        cursor = next + 1;
        /* Do not manufacture a blank final argument from trailing
         * whitespace. A genuine trailing comma still carries an empty
         * GNU positional value, as it did before this change. */
        if (!comma_delimited) {
            while (*cursor == ' ' || *cursor == '\t') ++cursor;
            if (*cursor == '\0') break;
        }
    }

    free(copy);
    return true;
}

static bool process_source_range(MiniAs *as,
                                 MiniAsSourceLine *lines,
                                 size_t begin,
                                 size_t end) {
    size_t i = begin;

    while (i < end && !as->source.source_ended) {
        MiniAsStableSiteId line_site;
        bool inline_handled=false;
        char macro_argument[1024];
        int macro_kind =
            classify_macro_line(as, lines[i].text,
                                macro_argument,
                                sizeof(macro_argument));
        char argument[256];
        size_t repeat_label_prefix=0U;
        int kind;

        if(!minias_source_graph_line_site(as,&lines[i],0U,&line_site))return false;
        as->source.current_site=line_site;

        /* File and expansion frames must agree on the semicolon-separated
         * GNU inline `.rept/.irp ... .endr` grammar. Dispatch that one
         * lexical special case before collector classification in BOTH. */
        if(!process_inline_repeat_line(as,lines[i].text,lines[i].line,
                                       &inline_handled))return false;
        if(inline_handled) {++i;continue;}

        /* GAS does not lex .macro/.rept/.irp at all in an inactive .if
         * branch. Only nested conditionals can reactivate parsing. In
         * particular, an unterminated .macro and unmatched .endm/.endr in
         * skipped text are harmless source, not collector syntax. */
        if(!as->source.conditional_active) {
            if(!process_source_line(as,lines[i].text,lines[i].line))return false;
            ++i;
            continue;
        }

        if (macro_kind < 0) {
            minias_set_error(as, "out-of-memory:macro-classify");
            return false;
        }
        if (macro_kind == MINIAS_MACRO_END) {
            minias_set_error(as,
                             "unmatched-directive:.endm:line=%zu",
                             lines[i].line);
            return false;
        }
        if (macro_kind == MINIAS_MACRO_BEGIN) {
            size_t j = i + 1U;
            size_t depth = 1U;

            while (j < end && depth != 0U) {
                char nested_argument[1024];
                int nested_kind =
                    classify_macro_line(as, lines[j].text,
                                        nested_argument,
                                        sizeof(nested_argument));
                if (nested_kind < 0) {
                    minias_set_error(as, "out-of-memory:macro-classify");
                    return false;
                }
                if (nested_kind == MINIAS_MACRO_BEGIN) {
                    ++depth;
                } else if (nested_kind == MINIAS_MACRO_END) {
                    --depth;
                }
                if (depth != 0U) {
                    ++j;
                }
            }
            if (depth != 0U || j >= end) {
                minias_set_error(as,
                                 "unterminated-directive:.macro:line=%zu",
                                 lines[i].line);
                return false;
            }
            if (!define_macro(as,
                              macro_argument,
                              lines + i + 1U,
                              j - i - 1U,
                              lines[i].line))return false;
            i = j + 1U;
            continue;
        }

        kind = classify_repeat_line(as, lines[i].text,
                                        argument,
                                        sizeof(argument),
                                        &repeat_label_prefix);
        if (kind < 0) {
            minias_set_error(as, "out-of-memory:repeat-classify");
            return false;
        }
        if (kind == MINIAS_REPEAT_ENDR) {
            minias_set_error(as,
                             "unmatched-directive:.endr:line=%zu",
                             lines[i].line);
            return false;
        }
        if (kind == MINIAS_REPEAT_REPT || kind == MINIAS_REPEAT_IRP) {
            size_t j = i + 1U;
            size_t depth = 1U;
            if(!emit_repeat_leading_label(as,lines[i].text,
                                          repeat_label_prefix,
                                          lines[i].line))return false;

            while (j < end && depth != 0U) {
                char nested_argument[256];
                int nested_kind =
                    classify_repeat_line(as, lines[j].text,
                                         nested_argument,
                                         sizeof(nested_argument),NULL);
                if (nested_kind < 0) {
                    minias_set_error(as, "out-of-memory:repeat-classify");
                    return false;
                }
                if (nested_kind == MINIAS_REPEAT_REPT ||
                    nested_kind == MINIAS_REPEAT_IRP) {
                    ++depth;
                } else if (nested_kind == MINIAS_REPEAT_ENDR) {
                    --depth;
                }
                if (depth != 0U) {
                    ++j;
                }
            }
            if (depth != 0U || j >= end) {
                minias_set_error(as,
                                 "unterminated-directive:%s:line=%zu",
                                 kind == MINIAS_REPEAT_IRP ? ".irp" : ".rept",
                                 lines[i].line);
                return false;
            }
            if (kind == MINIAS_REPEAT_REPT) {
                uint64_t repetitions;
                uint64_t iteration;

                if (!repeat_count_value(as,
                                        argument,
                                        lines[i].line,
                                        &repetitions)) {
                    return false;
                }
                for (iteration = 0U;
                     iteration < repetitions && !as->source.source_ended;
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
                        return false;
                    }
                }
            } else if (!process_irp_block(as,
                                          lines + i + 1U,
                                          j - i - 1U,
                                          argument,
                                          lines[i].line,
                                          line_site)) {
                return false;
            }
            i = j + 1U;
            continue;
        }
        if (!process_source_line(as, lines[i].text, lines[i].line)) {
            return false;
        }
        ++i;
    }
    return true;
}

/* GNU GAS permits semicolon-separated assignments and complete .rept/.irp
 * blocks on ONE physical source line (Linux head_64.S page tables).  Route
 * logical statements back through the existing Common repeat/macro engine;
 * do not recognize repeat syntax a second time in Target parsers. */
static bool process_inline_repeat_line(MiniAs *as,const char *source,
                                       size_t line,bool *handled) {
    MiniAsSourceLine *pieces=NULL;
    size_t count=0U,capacity=0U,i;
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
        char argument[256];
        int kind;
        if(end!=NULL)*end='\0';
        kind=classify_repeat_line(as,cursor,argument,sizeof(argument),NULL);
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
            .invocation_site=as->source.current_site,
            .definition_site=as->source.current_site
        };
        *handled=true;
        ok=minias_source_frame_process(as,&frame);
    }
    for(i=0U;i<count;++i)pieces[i].text=NULL; /* owned by copy */
    free(pieces);
    free(copy);
    return ok;
}

