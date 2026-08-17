#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]
frontend = root / "src/frontend"

# Declarator-suffix creation sites: keep the exact source position of the token
# following the [] suffix.
path = frontend / "parser_declarator.c"
text = path.read_text()
old_include = '''#include <limits.h>\n#include <string.h>\n'''
new_include = '''#include <limits.h>\n#include <stdio.h>\n#include <string.h>\n'''
if text.count(old_include) != 1:
    raise SystemExit(f"declarator include anchor count={text.count(old_include)}")
text = text.replace(old_include, new_include, 1)
old = '''        if (dimension == 0U && outermost_incomplete) {\n            if (!minic_c0_program_add_incomplete_array_type(parser->program, type, &type)) {\n                minic_parser_error(parser, "cannot build incomplete array declarator type");\n                return false;\n            }\n'''
new = '''        if (dimension == 0U && outermost_incomplete) {\n            if (!minic_c0_program_add_incomplete_array_type(parser->program, type, &type)) {\n                minic_parser_error(parser, "cannot build incomplete array declarator type");\n                return false;\n            }\n            (void)fprintf(stderr,\n                          "ARRAY_CREATE site=declarator id=%zu call_line=0 next_line=%zu next_col=%zu "\n                          "next_offset=%zu element_base=%d element_ptr=%u\\n",\n                          type.array_type_id,\n                          parser->current.span.begin.line,\n                          parser->current.span.begin.column,\n                          parser->current.span.begin.offset,\n                          (int)parser->program->array_types[type.array_type_id].element_type.base_kind,\n                          parser->program->array_types[type.array_type_id].element_type.pointer_depth);\n'''
if text.count(old) != 1:
    raise SystemExit(f"declarator array creation anchor count={text.count(old)}")
path.write_text(text.replace(old, new, 1))

# Trace every parser file that bypasses the shared declarator-suffix helper and
# constructs an incomplete array descriptor directly. This remains diagnostics only:
# the macro preserves the original call exactly and records its caller/source token.
symbol = "minic_c0_program_add_incomplete_array_type("
for path in sorted(frontend.glob("parser_*.c")):
    if path.name == "parser_declarator.c":
        continue
    text = path.read_text()
    if symbol not in text:
        continue
    if '#include <stdio.h>\n' not in text:
        anchors = ('#include <stdlib.h>\n', '#include <stdint.h>\n', '#include <limits.h>\n', '#include <string.h>\n')
        for anchor in anchors:
            if anchor in text:
                text = text.replace(anchor, anchor + '#include <stdio.h>\n', 1)
                break
        else:
            raise SystemExit(f"{path.name} include anchor missing")
    insertion_anchor = '\nstatic bool '
    pos = text.find(insertion_anchor)
    if pos < 0:
        raise SystemExit(f"{path.name} static helper anchor missing")
    site = path.stem.removeprefix("parser_")
    helper = f'''\nstatic bool minic_trace_add_incomplete_array_type(MinicParser *parser,\n                                                  MinicType element_type,\n                                                  MinicType *result_type,\n                                                  size_t call_line) {{\n    bool ok;\n\n    ok = minic_c0_program_add_incomplete_array_type(parser->program, element_type, result_type);\n    if (ok) {{\n        (void)fprintf(stderr,\n                      "ARRAY_CREATE site={site} id=%zu call_line=%zu next_line=%zu next_col=%zu "\n                      "next_offset=%zu element_base=%d element_ptr=%u\\n",\n                      result_type->array_type_id,\n                      call_line,\n                      parser->current.span.begin.line,\n                      parser->current.span.begin.column,\n                      parser->current.span.begin.offset,\n                      (int)element_type.base_kind,\n                      element_type.pointer_depth);\n    }}\n    return ok;\n}}\n\n#define minic_c0_program_add_incomplete_array_type(program_arg, element_arg, result_arg) \\\n    minic_trace_add_incomplete_array_type(parser, element_arg, result_arg, __LINE__)\n'''
    text = text[:pos] + helper + text[pos:]
    path.write_text(text)
