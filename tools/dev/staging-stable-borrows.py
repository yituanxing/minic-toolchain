from pathlib import Path

path = Path("src/frontend/parser_statement.c")
source = path.read_text()

old_normal = """        initializer = minic_c0_program_expression(parser->program, statement.expression);
        if (initializer == NULL ||
            !minic_c0_assignment_compatible(parser->program, local.type, statement.expression)) {
            minic_parser_error(parser, "initializer type does not match local type");
            return false;
        }
        statement.span.end = initializer_span.end;
"""
new_normal = """        initializer = minic_c0_program_expression(parser->program, statement.expression);
        if (initializer == NULL ||
            !minic_c0_assignment_compatible(parser->program, local.type, statement.expression)) {
            minic_parser_error(parser, "initializer type does not match local type");
            return false;
        }
        statement.span.end = initializer->span.end;
"""

old_duplicate = """    MinicSourcePosition begin;
    MinicSourceSpan initializer_span;
    MinicType initializer_type;
    MinicSourceSpan initializer_span;
    MinicType initializer_type;
"""
new_duplicate = """    MinicSourcePosition begin;
    MinicSourceSpan initializer_span;
    MinicType initializer_type;
"""

if old_normal not in source:
    raise SystemExit("normal local initializer fixup anchor not found")
if old_duplicate not in source:
    raise SystemExit("duplicate __auto_type snapshot anchor not found")
source = source.replace(old_normal, new_normal, 1)
source = source.replace(old_duplicate, new_duplicate, 1)
path.write_text(source)
