from pathlib import Path

root = Path(__file__).resolve().parents[2]

parser_type = root / "src/frontend/parser_type.c"
text = parser_type.read_text()
old = '''bool minic_parser_parse_type_name_preserving_incomplete(MinicParser *parser, MinicType *type) {
    MinicType base_type;

    return parser != NULL && type != NULL &&
           minic_parser_parse_type_specifiers(parser, &base_type) &&
           minic_parser_parse_pointer_declarator(parser, base_type, type);
}
'''
new = '''static bool type_name_starts_parenthesized_function_pointer(const MinicParser *parser) {
    MinicDiagnostic diagnostic;
    MinicLexer lookahead;
    MinicToken token;

    if (parser == NULL || parser->current.kind != MINIC_TOKEN_LPAREN) {
        return false;
    }
    lookahead = parser->lexer;
    (void)memset(&diagnostic, 0, sizeof(diagnostic));
    if (!minic_lexer_next(&lookahead, &token, &diagnostic) || token.kind != MINIC_TOKEN_STAR) {
        return false;
    }
    for (;;) {
        if (!minic_lexer_next(&lookahead, &token, &diagnostic)) {
            return false;
        }
        while (token.kind == MINIC_TOKEN_KW_CONST || token.kind == MINIC_TOKEN_KW_VOLATILE ||
               minic_parser_token_text_equals(parser, token, "restrict") ||
               minic_parser_token_text_equals(parser, token, "__restrict")) {
            if (!minic_lexer_next(&lookahead, &token, &diagnostic)) {
                return false;
            }
        }
        if (token.kind != MINIC_TOKEN_STAR) {
            break;
        }
    }
    return token.kind == MINIC_TOKEN_RPAREN;
}

bool minic_parser_parse_type_name_preserving_incomplete(MinicParser *parser, MinicType *type) {
    MinicType base_type;

    if (parser == NULL || type == NULL || !minic_parser_parse_type_specifiers(parser, &base_type)) {
        return false;
    }
    if (type_name_starts_parenthesized_function_pointer(parser)) {
        MinicParsedFunctionDeclarator declarator;

        if (!minic_parser_parse_parenthesized_function_declarator(
                parser, false, true, &declarator)) {
            return false;
        }
        if (declarator.is_variadic) {
            minic_parser_error(parser, "variadic function-pointer type names are not supported yet");
            return false;
        }
        if (!minic_parser_build_function_declarator_type(parser, base_type, &declarator, type)) {
            minic_parser_error(parser, "cannot build function-pointer type name");
            return false;
        }
        return true;
    }
    return minic_parser_parse_pointer_declarator(parser, base_type, type);
}
'''
if text.count(old) != 1:
    raise SystemExit("parser_type type-name owner shape changed")
parser_type.write_text(text.replace(old, new))

fixture = root / "tests/compiler/c0/shared_function_declarator.c"
text = fixture.read_text()
anchor = '''static int proc_impl(struct ctl_table *ctl, int write, void *buffer,
                     size_t *lenp, loff_t *ppos)
'''
insert = '''struct Rq {
    int value;
};

static void observe_rq(struct Rq *rq)
{
    (void)rq;
}

static void apply_casted_callback(void *raw, struct Rq *rq)
{
    void (*function)(struct Rq *);

    function = (void (*)(struct Rq *))raw;
    function(rq);
}

'''
if text.count(anchor) != 1:
    raise SystemExit("shared declarator fixture anchor changed")
text = text.replace(anchor, insert + anchor)
old_main = '''int main(void)
{
    return (apply(add_one, 41) - 42) + (apply_proc(proc_impl) - 42) +
           (apply_local(41) - 42);
}
'''
new_main = '''int main(void)
{
    struct Rq rq;

    rq.value = 7;
    apply_casted_callback((void *)observe_rq, &rq);
    return (apply(add_one, 41) - 42) + (apply_proc(proc_impl) - 42) +
           (apply_local(41) - 42);
}
'''
if text.count(old_main) != 1:
    raise SystemExit("shared declarator main shape changed")
fixture.write_text(text.replace(old_main, new_main))

runner = root / "tests/compiler/c0/run-shared-function-declarator.sh"
text = runner.read_text()
text = text.replace('test "$jalr_count" -ge 4\n', 'test "$jalr_count" -ge 5\n')
old_msg = '    "PASS compiler/c0/shared_function_declarator contexts=parenthesized-function-typedef,direct-function-typedef,function-pointer-field,extern-function-pointer-object,function-pointer-parameter,local-function-pointer local=void+int ordinary-parenthesized-local=preserved direct-pointer-call=1"\n'
new_msg = '    "PASS compiler/c0/shared_function_declarator contexts=parenthesized-function-typedef,direct-function-typedef,function-pointer-field,extern-function-pointer-object,function-pointer-parameter,local-function-pointer,function-pointer-type-name local=void+int ordinary-parenthesized-local=preserved explicit-cast=1 direct-pointer-call=1"\n'
if text.count(old_msg) != 1:
    raise SystemExit("shared declarator runner message changed")
runner.write_text(text.replace(old_msg, new_msg))
