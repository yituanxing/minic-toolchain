#!/usr/bin/env python3
from pathlib import Path

internal = Path("src/frontend/parser_internal.h")
text = internal.read_text()
if "#include <stdint.h>" not in text:
    text = text.replace("#include <stdbool.h>\n", "#include <stdbool.h>\n#include <stdint.h>\n", 1)
old = "    int case_values[MINIC_PARSER_MAX_SWITCH_CASES];\n"
new = "    int64_t case_values[MINIC_PARSER_MAX_SWITCH_CASES];\n"
if text.count(old) != 1:
    raise SystemExit("switch context case_values anchor mismatch")
internal.write_text(text.replace(old, new, 1))

statement = Path("src/frontend/parser_statement.c")
text = statement.read_text()
text = text.replace(
    "                                        int *value) {\n    const MinicExpression *expression;\n    int left;\n    int right;\n",
    "                                        int64_t *value) {\n    const MinicExpression *expression;\n    int64_t left;\n    int64_t right;\n",
    1,
)
text = text.replace(
    "            if (right < 0 || right >= 31 || left < 0) {\n                return false;\n            }\n            *value = (int)((unsigned int)left << (unsigned int)right);\n",
    "            if (right < 0 || right >= 63 || left < 0) {\n                return false;\n            }\n            *value = (int64_t)((uint64_t)left << (unsigned int)right);\n",
    1,
)
text = text.replace(
    "            if (right < 0 || right >= 31) {\n",
    "            if (right < 0 || right >= 63) {\n",
    1,
)
old = "    int value;\n    size_t index;\n\n    context = current_switch_context(parser);\n"
new = "    int64_t value;\n    size_t index;\n\n    context = current_switch_context(parser);\n"
if text.count(old) != 1:
    raise SystemExit("parse_case value anchor mismatch")
statement.write_text(text.replace(old, new, 1))

codegen = Path("src/target/riscv64/codegen_statement.c")
text = codegen.read_text()
if "#include <inttypes.h>" not in text:
    text = text.replace("#include <string.h>\n", "#include <inttypes.h>\n#include <string.h>\n", 1)
old = '''            fprintf(file,
                    "  li t1, %d\\n"
                    "  beq t0, t1, .Lswitch_case_%zu\\n",
                    case_expression->value.integer_value,
                    (size_t)case_id) < 0) {
'''
new = '''            fprintf(file,
                    "  li t1, %" PRId64 "\\n"
                    "  beq t0, t1, .Lswitch_case_%zu\\n",
                    case_expression->value.integer_value,
                    (size_t)case_id) < 0) {
'''
if text.count(old) != 1:
    raise SystemExit("RV64 switch literal format anchor mismatch")
codegen.write_text(text.replace(old, new, 1))

print("staged 64-bit switch case folding and RV64 emission")
