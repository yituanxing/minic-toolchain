#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]
path = root / "src/frontend/parser_declarator.c"
text = path.read_text()

old_include = '''#include <limits.h>\n#include <string.h>\n'''
new_include = '''#include <limits.h>\n#include <stdio.h>\n#include <string.h>\n'''
if text.count(old_include) != 1:
    raise SystemExit(f"include anchor count={text.count(old_include)}")
text = text.replace(old_include, new_include, 1)

old = '''        if (dimension == 0U && outermost_incomplete) {\n            if (!minic_c0_program_add_incomplete_array_type(parser->program, type, &type)) {\n                minic_parser_error(parser, "cannot build incomplete array declarator type");\n                return false;\n            }\n'''
new = '''        if (dimension == 0U && outermost_incomplete) {\n            if (!minic_c0_program_add_incomplete_array_type(parser->program, type, &type)) {\n                minic_parser_error(parser, "cannot build incomplete array declarator type");\n                return false;\n            }\n            (void)fprintf(stderr,\n                          "ARRAY_CREATE id=%zu next_line=%zu next_col=%zu next_offset=%zu "\n                          "element_base=%d element_ptr=%u\\n",\n                          type.array_type_id,\n                          parser->current.span.begin.line,\n                          parser->current.span.begin.column,\n                          parser->current.span.begin.offset,\n                          (int)parser->program->array_types[type.array_type_id].element_type.base_kind,\n                          parser->program->array_types[type.array_type_id].element_type.pointer_depth);\n'''
if text.count(old) != 1:
    raise SystemExit(f"array creation anchor count={text.count(old)}")
path.write_text(text.replace(old, new, 1))
