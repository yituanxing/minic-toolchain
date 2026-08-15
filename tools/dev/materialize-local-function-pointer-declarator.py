#!/usr/bin/env python3
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 match, found {count}")
    return text.replace(old, new, 1)


path = Path("src/frontend/parser_statement.c")
text = path.read_text()
old = r'''    (void)memset(&attributes, 0, sizeof(attributes));
    attributes.cleanup_function = MINIC_FUNCTION_INVALID;
    if (!minic_parser_parse_pointer_declarator(parser, base_type, &declared_type)) {
        return false;
    }
    if (minic_type_is_void(declared_type)) {
        minic_parser_error(parser, "local object cannot have void type");
        return false;
    }
    if (!minic_parser_parse_direct_declarator_name(parser, &local.name_span)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "expected local name");
        }
        return false;
    }

'''
new = r'''    (void)memset(&attributes, 0, sizeof(attributes));
    attributes.cleanup_function = MINIC_FUNCTION_INVALID;
    if (!minic_parser_parse_pointer_declarator(parser, base_type, &declared_type)) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_LPAREN) {
        MinicParsedFunctionDeclarator declarator;

        if (!minic_parser_parse_parenthesized_function_declarator(
                parser, true, true, &declarator)) {
            return false;
        }
        if (declarator.is_variadic) {
            minic_parser_error(parser,
                               "variadic direct local function pointers are not supported yet");
            return false;
        }
        if (!minic_parser_build_function_declarator_type(
                parser, declared_type, &declarator, &declared_type)) {
            minic_parser_error(parser, "cannot build local function pointer type");
            return false;
        }
        local.name_span = declarator.name_span;
    } else if (!minic_parser_parse_direct_declarator_name(parser, &local.name_span)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "expected local name");
        }
        return false;
    }
    if (minic_type_is_void(declared_type)) {
        minic_parser_error(parser, "local object cannot have void type");
        return false;
    }

'''
path.write_text(replace_once(text, old, new, "local declarator dispatch"))

path = Path("tests/compiler/c0/shared_function_declarator.c")
text = path.read_text()
old = r'''static int apply(int (*function)(int), int value)
{
    return function(value);
}

'''
new = r'''static int apply(int (*function)(int), int value)
{
    return function(value);
}

static void discard(int value)
{
    (void)value;
}

static int apply_local(int value)
{
    void (*notify)(int);
    int (*transform)(int);

    notify = discard;
    transform = add_one;
    notify(value);
    return transform(value);
}

'''
text = replace_once(text, old, new, "shared declarator local fixture")
old = r'''int main(void)
{
    return (apply(add_one, 41) - 42) + (apply_proc(proc_impl) - 42);
}
'''
new = r'''int main(void)
{
    return (apply(add_one, 41) - 42) + (apply_proc(proc_impl) - 42) +
           (apply_local(41) - 42);
}
'''
path.write_text(replace_once(text, old, new, "shared declarator main"))

path = Path("tests/compiler/c0/run-shared-function-declarator.sh")
text = path.read_text()
old = r'''jalr_count=$(grep -c -F "  jalr" "$work/shared_function_declarator.s")
test "$jalr_count" -ge 2
printf '%s\n' \
    "PASS compiler/c0/shared_function_declarator contexts=parenthesized-function-typedef,direct-function-typedef,function-pointer-field,extern-function-pointer-object,function-pointer-parameter direct-pointer-call=1"
'''
new = r'''jalr_count=$(grep -c -F "  jalr" "$work/shared_function_declarator.s")
test "$jalr_count" -ge 4
printf '%s\n' \
    "PASS compiler/c0/shared_function_declarator contexts=parenthesized-function-typedef,direct-function-typedef,function-pointer-field,extern-function-pointer-object,function-pointer-parameter,local-function-pointer local=void+int direct-pointer-call=1"
'''
path.write_text(replace_once(text, old, new, "shared declarator runner"))
