#!/usr/bin/env python3
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, found {count}")
    return text.replace(old, new, 1)


root = Path(__file__).resolve().parents[2]

path = root / "src/frontend/parser_record.c"
text = path.read_text()
nul_anchor = "message[0] == '\x00'"
if text.count(nul_anchor) != 1:
    raise SystemExit(f"parser-record-nul: expected one anchor, found {text.count(nul_anchor)}")
text = text.replace(nul_anchor, "message[0] == '\\0'", 1)
path.write_text(text)

path = root / "src/frontend/ast_verifier.h"
text = path.read_text()
text = replace_once(
    text,
    '#include "frontend/ast.h"\n',
    '#include "frontend/ast.h"\n#include "target/target_info.h"\n',
    "verifier-target-include",
)
text = replace_once(
    text,
    'bool minic_c0_program_verify(const MinicC0Program *program, MinicC0AstForm form);\n',
    '''bool minic_c0_program_verify_target(const MinicC0Program *program,
                                    MinicC0AstForm form,
                                    const MinicTargetInfo *target);
bool minic_c0_program_verify(const MinicC0Program *program, MinicC0AstForm form);
''',
    "verifier-target-api",
)
path.write_text(text)

path = root / "src/frontend/ast_verifier.c"
text = path.read_text()
text = replace_once(
    text,
    '''static bool
verify_expression(const MinicC0Program *program, size_t expression_index, MinicC0AstForm form) {
''',
    '''static bool verify_expression(const MinicC0Program *program,
                              size_t expression_index,
                              MinicC0AstForm form,
                              const MinicTargetInfo *target) {
''',
    "verifier-expression-target",
)
old_sizeof = '''    case MINIC_EXPRESSION_SIZEOF:
        return expression->value_category == MINIC_VALUE_RVALUE &&
               minic_type_equal(expression->type, minic_type_unsigned_long()) &&
               type_is_valid(program, expression->value.sizeof_type) &&
               type_is_complete_object(program, expression->value.sizeof_type);
'''
new_sizeof = '''    case MINIC_EXPRESSION_SIZEOF: {
        size_t measured_size;

        return target != NULL && expression->value_category == MINIC_VALUE_RVALUE &&
               minic_type_equal(expression->type, minic_type_unsigned_long()) &&
               type_is_valid(program, expression->value.sizeof_type) &&
               minic_target_info_sizeof_type(
                   target, program, expression->value.sizeof_type, &measured_size);
    }
'''
text = replace_once(text, old_sizeof, new_sizeof, "verifier-sizeof-target")
text = replace_once(
    text,
    '''bool minic_c0_program_verify(const MinicC0Program *program, MinicC0AstForm form) {
''',
    '''bool minic_c0_program_verify_target(const MinicC0Program *program,
                                    MinicC0AstForm form,
                                    const MinicTargetInfo *target) {
''',
    "verifier-public-target",
)
text = replace_once(
    text,
    '''    if ((form != MINIC_C0_AST_PARSED && form != MINIC_C0_AST_NORMALIZED) ||
        !verify_program_storage(program)) {
''',
    '''    if (target == NULL ||
        (form != MINIC_C0_AST_PARSED && form != MINIC_C0_AST_NORMALIZED) ||
        !verify_program_storage(program)) {
''',
    "verifier-target-required",
)
count = text.count("verify_expression(program, index, form)")
if count != 1:
    raise SystemExit(f"verifier-expression-call: expected one anchor, found {count}")
text = text.replace(
    "verify_expression(program, index, form)",
    "verify_expression(program, index, form, target)",
    1,
)
text += '''

bool minic_c0_program_verify(const MinicC0Program *program, MinicC0AstForm form) {
    return minic_c0_program_verify_target(program, form, minic_default_target_info());
}
'''
path.write_text(text)

path = root / "src/compiler/compiler.c"
text = path.read_text()
text = replace_once(
    text,
    '#include "target/riscv64/codegen.h"\n',
    '#include "target/riscv64/codegen.h"\n#include "target/target_info.h"\n',
    "compiler-target-info-include",
)
text = replace_once(
    text,
    '''    MinicC0Program program;
    bool success;
''',
    '''    MinicC0Program program;
    const MinicTargetInfo *target_info;
    bool success;
''',
    "compiler-target-info-variable",
)
text = replace_once(
    text,
    '''    minic_c0_program_initialize(&program);
    success = minic_parse_c0_program(input_path, buffer.data, buffer.size, &program, diagnostic);
''',
    '''    minic_c0_program_initialize(&program);
    target_info = minic_default_target_info();
    success = minic_parse_c0_program(input_path, buffer.data, buffer.size, &program, diagnostic);
''',
    "compiler-target-info-init",
)
text = text.replace(
    "minic_c0_program_verify(&program, MINIC_C0_AST_PARSED)",
    "minic_c0_program_verify_target(&program, MINIC_C0_AST_PARSED, target_info)",
)
text = text.replace(
    "minic_c0_program_verify(&program, MINIC_C0_AST_NORMALIZED)",
    "minic_c0_program_verify_target(&program, MINIC_C0_AST_NORMALIZED, target_info)",
)
path.write_text(text)

path = root / "Makefile"
text = path.read_text()
text = replace_once(
    text,
    '''AST_CONTRACT_TEST_SOURCES := \\
\tsrc/frontend/ast.c \\
\tsrc/frontend/ast_global.c \\
\tsrc/frontend/ast_verifier.c \\
\tsrc/frontend/cast_normalization.c \\
\tsrc/frontend/type.c \\
\ttests/frontend/ast_contract_test.c
''',
    '''AST_CONTRACT_TEST_SOURCES := \\
\tsrc/frontend/ast.c \\
\tsrc/frontend/ast_global.c \\
\tsrc/frontend/ast_verifier.c \\
\tsrc/frontend/cast_normalization.c \\
\tsrc/frontend/type.c \\
\tsrc/target/data_layout.c \\
\tsrc/target/target_info.c \\
\ttests/frontend/ast_contract_test.c
''',
    "ast-contract-target-info-sources",
)
path.write_text(text)
