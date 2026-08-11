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

old_auto = """        statement.kind = MINIC_STATEMENT_ASSIGN;
        statement.span.begin = begin;
        statement.span.end = initializer->span.end;
        statement.target_expression = target_id;
"""
new_auto = """        statement.kind = MINIC_STATEMENT_ASSIGN;
        statement.span.begin = begin;
        statement.span.end = initializer_span.end;
        statement.target_expression = target_id;
"""

if old_normal not in source:
    raise SystemExit("normal local initializer fixup anchor not found")
if old_auto not in source:
    raise SystemExit("__auto_type initializer fixup anchor not found")
source = source.replace(old_normal, new_normal, 1)
source = source.replace(old_auto, new_auto, 1)
path.write_text(source)
