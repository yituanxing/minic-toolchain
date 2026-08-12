#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]
path = root / "tools/dev/pr110-materialize-gnu-weak-function-symbol.py"
text = path.read_text()
old_block = '''# Function definitions persist weak after entity creation/reuse.
replace_once(
    "src/frontend/parser_function.c",
    """    if (has_assembler_name &&\\n        !minic_c0_program_set_function_assembler_name(\\n            parser->program, function_id, assembler_name, assembler_name_length)) {\\n        minic_parser_error(parser, \\"conflicting or invalid GNU function asm label\\");\\n""",
    """    if (is_weak && !minic_c0_program_set_function_weak(parser->program, function_id, true)) {\\n        minic_parser_error(parser, \\"conflicting GNU weak function linkage\\");\\n        return false;\\n    }\\n    if (has_assembler_name &&\\n        !minic_c0_program_set_function_assembler_name(\\n            parser->program, function_id, assembler_name, assembler_name_length)) {\\n        minic_parser_error(parser, \\"conflicting or invalid GNU function asm label\\");\\n""",
)
'''
new_block = '''# Function definitions persist weak after entity creation/reuse. The same
# metadata sequence exists in the declaration helper; that first occurrence has
# already been handled above, so patch the second exact occurrence here.
path = ROOT / "src/frontend/parser_function.c"
text = path.read_text()
old_definition_metadata = """    if (has_assembler_name &&\\n        !minic_c0_program_set_function_assembler_name(\\n            parser->program, function_id, assembler_name, assembler_name_length)) {\\n        minic_parser_error(parser, \\"conflicting or invalid GNU function asm label\\");\\n"""
new_definition_metadata = """    if (is_weak && !minic_c0_program_set_function_weak(parser->program, function_id, true)) {\\n        minic_parser_error(parser, \\"conflicting GNU weak function linkage\\");\\n        return false;\\n    }\\n    if (has_assembler_name &&\\n        !minic_c0_program_set_function_assembler_name(\\n            parser->program, function_id, assembler_name, assembler_name_length)) {\\n        minic_parser_error(parser, \\"conflicting or invalid GNU function asm label\\");\\n"""
first = text.find(old_definition_metadata)
second = text.find(old_definition_metadata, first + 1) if first >= 0 else -1
third = text.find(old_definition_metadata, second + 1) if second >= 0 else -1
if first < 0 or second < 0 or third >= 0:
    raise SystemExit(f"parser_function.c: expected exactly two definition-metadata shapes first={first} second={second} third={third}")
text = text[:second] + new_definition_metadata + text[second + len(old_definition_metadata):]
path.write_text(text)
'''
if text.count(old_block) != 1:
    raise SystemExit(f"materializer: expected one definition metadata block, found {text.count(old_block)}")
path.write_text(text.replace(old_block, new_block, 1))
