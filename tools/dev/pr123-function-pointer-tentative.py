from pathlib import Path

path = Path("src/frontend/parser_function.c")
text = path.read_text()
old = '''        if (is_extern_declaration && name_probe.current.kind == MINIC_TOKEN_STAR) {
            MinicParsedFunctionDeclarator declarator;

            if (!minic_parser_parse_parenthesized_function_declarator(
                    parser, true, true, &declarator)) {
                return false;
            }
            if (declarator.is_variadic) {
                minic_parser_error(
                    parser, "variadic extern function pointer objects are not supported yet");
                return false;
            }
            if (!minic_parser_build_function_declarator_type(
                    parser, return_type, &declarator, &return_type)) {
                minic_parser_error(parser, "cannot build extern function pointer object type");
                return false;
            }
            name_span = declarator.name_span;
            is_function_pointer_object = true;
'''
new = '''        if (name_probe.current.kind == MINIC_TOKEN_STAR) {
            MinicParsedFunctionDeclarator declarator;

            if (!minic_parser_parse_parenthesized_function_declarator(
                    parser, true, true, &declarator)) {
                return false;
            }
            if (declarator.is_variadic) {
                minic_parser_error(parser, "variadic function pointer objects are not supported yet");
                return false;
            }
            if (!minic_parser_build_function_declarator_type(
                    parser, return_type, &declarator, &return_type)) {
                minic_parser_error(parser, "cannot build function pointer object type");
                return false;
            }
            name_span = declarator.name_span;
            is_function_pointer_object = true;
'''
if text.count(old) != 1:
    raise SystemExit(f"function-pointer object declarator anchor mismatch: {text.count(old)}")
path.write_text(text.replace(old, new, 1))
