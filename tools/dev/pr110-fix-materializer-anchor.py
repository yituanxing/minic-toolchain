#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]
path = root / "tools/dev/pr110-materialize-gnu-weak-function-symbol.py"
text = path.read_text()
old_block = '''replace_once(
    "src/frontend/parser_function.c",
    """    if (has_assembler_name &&\\n        !minic_c0_program_set_function_assembler_name(\\n            parser->program, function_id, assembler_name, assembler_name_length)) {\\n""",
    """    if (is_weak && !minic_c0_program_set_function_weak(parser->program, function_id, true)) {\\n        minic_parser_error(parser, \\"conflicting GNU weak function linkage\\");\\n        return false;\\n    }\\n    if (has_assembler_name &&\\n        !minic_c0_program_set_function_assembler_name(\\n            parser->program, function_id, assembler_name, assembler_name_length)) {\\n""",
)
'''
new_block = '''path = ROOT / "src/frontend/parser_function.c"
text = path.read_text()
old_declaration_metadata = """    if (has_assembler_name &&\\n        !minic_c0_program_set_function_assembler_name(\\n            parser->program, function_id, assembler_name, assembler_name_length)) {\\n"""
new_declaration_metadata = """    if (is_weak && !minic_c0_program_set_function_weak(parser->program, function_id, true)) {\\n        minic_parser_error(parser, \\"conflicting GNU weak function linkage\\");\\n        return false;\\n    }\\n    if (has_assembler_name &&\\n        !minic_c0_program_set_function_assembler_name(\\n            parser->program, function_id, assembler_name, assembler_name_length)) {\\n"""
if text.count(old_declaration_metadata) != 2:
    raise SystemExit(
        f"parser_function.c: expected declaration+definition metadata anchors, found {text.count(old_declaration_metadata)}"
    )
path.write_text(text.replace(old_declaration_metadata, new_declaration_metadata, 1))
'''
if text.count(old_block) != 1:
    raise SystemExit(f"materializer: expected one ambiguous metadata block, found {text.count(old_block)}")
path.write_text(text.replace(old_block, new_block, 1))
