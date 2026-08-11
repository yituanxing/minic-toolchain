#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(path):
    return (ROOT / path).read_text()


def write(path, content):
    (ROOT / path).write_text(content)


def one(content, old, new, label):
    count = content.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 match, found {count}")
    return content.replace(old, new, 1)


p = "src/frontend/parser_internal.h"
s = read(p)
s = one(
    s,
    '''bool minic_parser_parse_parenthesized_function_declarator(
    MinicParser *parser,
    bool require_name,
    bool require_pointer,
    MinicParsedFunctionDeclarator *declarator);
''',
    '''bool minic_parser_parse_function_parameter_suffix(MinicParser *parser,
                                                  MinicParsedFunctionDeclarator *declarator);
bool minic_parser_parse_parenthesized_function_declarator(
    MinicParser *parser,
    bool require_name,
    bool require_pointer,
    MinicParsedFunctionDeclarator *declarator);
''',
    "function suffix prototype",
)
write(p, s)

p = "src/frontend/parser_declarator.c"
s = read(p)
s = one(
    s,
    '''#include <string.h>

bool minic_parser_parse_parenthesized_function_declarator(
''',
    '''#include <string.h>

bool minic_parser_parse_function_parameter_suffix(MinicParser *parser,
                                                  MinicParsedFunctionDeclarator *declarator) {
    if (parser == NULL || declarator == NULL) {
        return false;
    }
    declarator->parameter_count = 0U;
    declarator->is_variadic = false;
    return minic_parser_expect(
               parser, MINIC_TOKEN_LPAREN, "expected '(' before function parameter list") &&
           minic_parser_parse_parameter_list(parser,
                                             NULL,
                                             declarator->parameter_types,
                                             &declarator->parameter_count,
                                             false,
                                             &declarator->is_variadic) &&
           minic_parser_expect(
               parser, MINIC_TOKEN_RPAREN, "expected ')' after function parameter list");
}

bool minic_parser_parse_parenthesized_function_declarator(
''',
    "shared function suffix helper",
)
s = one(
    s,
    '''    if (!minic_parser_expect(
            parser, MINIC_TOKEN_RPAREN, "expected ')' after function declarator") ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_LPAREN, "expected '(' before function parameter list") ||
        !minic_parser_parse_parameter_list(parser,
                                           NULL,
                                           declarator->parameter_types,
                                           &declarator->parameter_count,
                                           false,
                                           &declarator->is_variadic) ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_RPAREN, "expected ')' after function parameter list")) {
        return false;
    }
    return true;
''',
    '''    return minic_parser_expect(
               parser, MINIC_TOKEN_RPAREN, "expected ')' after function declarator") &&
           minic_parser_parse_function_parameter_suffix(parser, declarator);
''',
    "parenthesized declarator suffix reuse",
)
write(p, s)

p = "src/frontend/parser_typedef.c"
s = read(p)
s = one(s, "static bool parse_function_pointer_typedef(", "static bool parse_parenthesized_function_typedef(", "typedef helper rename")
s = one(
    s,
    '''    bool is_function_pointer;

    bound_count = 0U;
    is_function_pointer = false;
''',
    '''    bool is_function_declarator;

    bound_count = 0U;
    is_function_declarator = false;
''',
    "function declarator state",
)
s = one(
    s,
    '''        if (parser->current.kind == MINIC_TOKEN_LPAREN) {
            if (!parse_function_pointer_typedef(parser, aliased_type, &name_span, &aliased_type)) {
                return false;
            }
            is_function_pointer = true;
        }
    }
    if (minic_type_is_void(aliased_type)) {
        minic_parser_error(parser, "typedef cannot name bare void");
        return false;
    }
    if (!is_function_pointer) {
        if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
            minic_parser_error(parser, "expected typedef name");
            return false;
        }
        name_span = parser->current.span;
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }
''',
    '''        if (parser->current.kind == MINIC_TOKEN_LPAREN) {
            if (!parse_parenthesized_function_typedef(
                    parser, aliased_type, &name_span, &aliased_type)) {
                return false;
            }
            is_function_declarator = true;
        } else {
            MinicParsedFunctionDeclarator declarator;

            if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
                minic_parser_error(parser, "expected typedef name");
                return false;
            }
            name_span = parser->current.span;
            if (!minic_parser_advance(parser)) {
                return false;
            }
            if (parser->current.kind == MINIC_TOKEN_LPAREN) {
                (void)memset(&declarator, 0, sizeof(declarator));
                declarator.name_span = name_span;
                declarator.has_name = true;
                if (!minic_parser_parse_function_parameter_suffix(parser, &declarator)) {
                    return false;
                }
                if (declarator.is_variadic) {
                    minic_parser_error(parser, "variadic function typedefs are not supported yet");
                    return false;
                }
                if (!minic_parser_build_function_declarator_type(
                        parser, aliased_type, &declarator, &aliased_type)) {
                    minic_parser_error(parser, "cannot build function typedef type");
                    return false;
                }
                is_function_declarator = true;
            }
        }
    }
    if (minic_type_is_void(aliased_type)) {
        minic_parser_error(parser, "typedef cannot name bare void");
        return false;
    }
''',
    "direct function typedef dispatch",
)
s = s.replace("is_function_pointer", "is_function_declarator")
s = s.replace("function pointer typedef arrays are not supported yet", "function typedef array declarators are not supported yet")
write(p, s)

p = "tests/compiler/c0/shared_function_declarator.c"
s = read(p)
s = one(
    s,
    '''typedef int (UnaryFunction)(int);

struct Ops {
''',
    '''typedef int (UnaryFunction)(int);

struct ctl_table;
typedef unsigned long size_t;
typedef long loff_t;
/* Linux sysctl.h shape: a typedef names the function type itself. */
typedef int proc_handler(struct ctl_table *ctl, int write, void *buffer,
                         size_t *lenp, loff_t *ppos);

struct ProcOps {
    proc_handler *handler;
};

struct Ops {
''',
    "linux direct function typedef fixture",
)
s = one(
    s,
    '''static int apply(int (*function)(int), int value)
{
    return function(value);
}

int main(void)
{
    return apply(add_one, 41) - 42;
}
''',
    '''static int apply(int (*function)(int), int value)
{
    return function(value);
}

static int proc_impl(struct ctl_table *ctl, int write, void *buffer,
                     size_t *lenp, loff_t *ppos)
{
    (void)ctl;
    (void)buffer;
    (void)lenp;
    (void)ppos;
    return write;
}

static int apply_proc(proc_handler *handler)
{
    return handler((struct ctl_table *)0, 42, (void *)0, (size_t *)0, (loff_t *)0);
}

int main(void)
{
    return (apply(add_one, 41) - 42) + (apply_proc(proc_impl) - 42);
}
''',
    "direct function typedef use",
)
write(p, s)

p = "tests/compiler/c0/run-shared-function-declarator.sh"
s = read(p)
s = one(
    s,
    '''grep -F "  jalr" "$work/shared_function_declarator.s" >/dev/null
printf '%s\\n' \\
    "PASS compiler/c0/shared_function_declarator contexts=typedef,function-pointer-field,extern-function-pointer-object,function-pointer-parameter"
''',
    '''jalr_count=$(grep -c -F "  jalr" "$work/shared_function_declarator.s")
test "$jalr_count" -ge 2
printf '%s\\n' \\
    "PASS compiler/c0/shared_function_declarator contexts=parenthesized-function-typedef,direct-function-typedef,function-pointer-field,extern-function-pointer-object,function-pointer-parameter direct-pointer-call=1"
''',
    "shared declarator runner",
)
write(p, s)
