#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]

# Declarator-suffix creation sites: keep the exact source position of the token
# following the [] suffix.
path = root / "src/frontend/parser_declarator.c"
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

# Some legacy top-level parsers construct incomplete array descriptors directly.
# Wrap those calls without changing their semantics so the exact call-site line and
# source token can be correlated with an ownerless descriptor reported by verifier.
for filename, site in (("parser_function.c", "function"), ("parser_global.c", "global")):
    path = root / "src/frontend" / filename
    text = path.read_text()
    if '#include <stdio.h>\n' not in text:
        anchor = '#include <stdlib.h>\n' if '#include <stdlib.h>\n' in text else '#include <stdint.h>\n'
        if text.count(anchor) != 1:
            raise SystemExit(f"{filename} include anchor count={text.count(anchor)}")
        text = text.replace(anchor, anchor + '#include <stdio.h>\n', 1)
    insertion_anchor = '\nstatic bool '
    pos = text.find(insertion_anchor)
    if pos < 0:
        raise SystemExit(f"{filename} static helper anchor missing")
    helper = f'''\nstatic bool minic_trace_add_incomplete_array_type(MinicParser *parser,\n                                                  MinicType element_type,\n                                                  MinicType *result_type,\n                                                  size_t call_line) {{\n    bool ok;\n\n    ok = minic_c0_program_add_incomplete_array_type(parser->program, element_type, result_type);\n    if (ok) {{\n        (void)fprintf(stderr,\n                      "ARRAY_CREATE site={site} id=%zu call_line=%zu next_line=%zu next_col=%zu "\n                      "next_offset=%zu element_base=%d element_ptr=%u\\n",\n                      result_type->array_type_id,\n                      call_line,\n                      parser->current.span.begin.line,\n                      parser->current.span.begin.column,\n                      parser->current.span.begin.offset,\n                      (int)element_type.base_kind,\n                      element_type.pointer_depth);\n    }}\n    return ok;\n}}\n\n#define minic_c0_program_add_incomplete_array_type(program_arg, element_arg, result_arg) \\\n    minic_trace_add_incomplete_array_type(parser, element_arg, result_arg, __LINE__)\n'''
    text = text[:pos] + helper + text[pos:]
    path.write_text(text)
