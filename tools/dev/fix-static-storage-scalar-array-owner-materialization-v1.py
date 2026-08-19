from pathlib import Path

root = Path(__file__).resolve().parents[2]
function_path = root / "src/frontend/parser_function.c"
statement_path = root / "src/frontend/parser_statement.c"

text = function_path.read_text()
if "\x00" not in text:
    raise SystemExit("expected materialized NUL diagnostic literal not found")
function_path.write_text(text.replace("\x00", "\\0"))

text = statement_path.read_text()
helper_start = text.index("static bool parse_inferred_static_local_array(")
brace_start = text.index("    if (parser->current.kind == MINIC_TOKEN_LBRACE) {\n", helper_start)
brace_end = text.index(
    '    minic_parser_error(parser,\n                       "inferred static local array requires a string or brace initializer");\n',
    brace_start,
)
block = text[brace_start:brace_end]
block = block.replace("        MinicType object_type;\n        MinicGlobalObjectId object_id;\n", "", 1)
text = text[:brace_start] + block + text[brace_end:]
statement_path.write_text(text)
print("fixed static-storage scalar-array materialization hygiene")
