#!/usr/bin/env python3
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]


def read(path):
    return (ROOT / path).read_text()


def write(path, text):
    (ROOT / path).write_text(text)


def replace_exact(text, old, new, expected=1, label="replacement"):
    count = text.count(old)
    if count != expected:
        raise RuntimeError(f"{label}: expected {expected} matches, found {count}")
    return text.replace(old, new)


def replace_regex(text, pattern, replacement, expected=1, label="regex replacement"):
    text, count = re.subn(pattern, replacement, text, flags=re.S)
    if count != expected:
        raise RuntimeError(f"{label}: expected {expected} matches, found {count}")
    return text


def function_region(text, start_marker):
    start = text.index(start_marker)
    next_static = text.find("\nstatic ", start + len(start_marker))
    next_public = text.find("\nbool ", start + len(start_marker))
    ends = [value for value in (next_static, next_public) if value != -1]
    if not ends:
        raise RuntimeError(f"cannot find end of region starting {start_marker!r}")
    return start, min(ends)


# Intrinsic MinicType remains target-independent. Remove the two helpers whose
# answers depend on target integer ranges; TargetInfo is now their sole owner.
path = "src/frontend/type.h"
text = read(path)
text = replace_exact(
    text,
    "bool minic_type_integer_promotion(MinicType type, MinicType *result);\n"
    "bool minic_type_integer_common(MinicType left, MinicType right, MinicType *result);\n",
    "",
    label="remove target-dependent type declarations",
)
write(path, text)

path = "src/frontend/type.c"
text = read(path)
text = replace_regex(
    text,
    r"\nbool minic_type_integer_promotion\(MinicType type, MinicType \*result\) \{.*?"
    r"\nstatic bool minic_type_void_object_pointer_compatible",
    "\nstatic bool minic_type_void_object_pointer_compatible",
    label="remove target-dependent type implementations",
)
write(path, text)

# The frontend/type unit test now tests only intrinsic type identity and
# compatibility. Target-dependent promotion/common-type coverage lives with
# TargetInfo (including the synthetic 16-bit-int model).
path = "tests/frontend/type_test.c"
text = read(path)
text = replace_exact(text, "    MinicType promoted_type;\n", "", label="drop promoted_type local")
text = replace_exact(text, "    MinicType common_type;\n", "", label="drop common_type local")
text = replace_exact(
    text,
    "        !minic_type_cast_compatible(double_type, integer_type) ||\n"
    "        !minic_type_cast_compatible(integer_type, double_type) ||\n"
    "        minic_type_integer_promotion(double_type, &promoted_type)) {",
    "        !minic_type_cast_compatible(double_type, integer_type) ||\n"
    "        !minic_type_cast_compatible(integer_type, double_type)) {",
    label="remove target promotion negative check",
)
text = replace_regex(
    text,
    r"\n    if \(!minic_type_integer_promotion\(unsigned_char_type, &promoted_type\).*?"
    r"return fail\(\"common integer type\"\);\n    \}\n",
    "\n",
    label="remove target-dependent type unit blocks",
)
write(path, text)

# Conditional-expression result typing is an expression-semantic operation:
# it needs both the semantic AST (for null-pointer constants) and TargetInfo
# (for integer usual arithmetic conversions). Keep that dependency out of the
# AST storage/ownership module so small AST-only tests remain target-independent.
path = "src/frontend/ast.h"
text = read(path)
text = replace_exact(
    text,
    "bool minic_c0_conditional_result_type(const MinicC0Program *program,\n"
    "                                      MinicExpressionId when_true_expression_id,\n"
    "                                      MinicExpressionId when_false_expression_id,\n"
    "                                      MinicType *result);\n",
    "",
    label="move conditional result declaration out of ast",
)
write(path, text)

path = "src/frontend/ast.c"
text = read(path)
text = replace_regex(
    text,
    r"\nstatic bool\nminic_c0_conditional_type_only\(.*?"
    r"\n\}\n\nbool minic_c0_assignment_compatible",
    "\nbool minic_c0_assignment_compatible",
    label="move conditional result implementation out of ast",
)
write(path, text)

write(
    "src/frontend/expression_semantics.h",
    """#ifndef MINIC_FRONTEND_EXPRESSION_SEMANTICS_H\n"
    "#define MINIC_FRONTEND_EXPRESSION_SEMANTICS_H\n\n"
    "#include \"frontend/ast.h\"\n"
    "#include \"target/target_info.h\"\n\n"
    "bool minic_c0_conditional_result_type(const MinicC0Program *program,\n"
    "                                      const MinicTargetInfo *target,\n"
    "                                      MinicExpressionId when_true_expression_id,\n"
    "                                      MinicExpressionId when_false_expression_id,\n"
    "                                      MinicType *result);\n\n"
    "#endif\n""".replace('"\n    "', ""),
)

write(
    "src/frontend/expression_semantics.c",
    """#include \"frontend/expression_semantics.h\"\n\n"
    "static bool conditional_type_only(const MinicTargetInfo *target,\n"
    "                                  MinicType when_true,\n"
    "                                  MinicType when_false,\n"
    "                                  MinicType *result) {\n"
    "    bool has_double_operand;\n"
    "    bool has_numeric_operands;\n\n"
    "    if (target == NULL || result == NULL) {\n"
    "        return false;\n"
    "    }\n"
    "    if (minic_type_equal(when_true, when_false)) {\n"
    "        *result = when_true;\n"
    "        return true;\n"
    "    }\n"
    "    if (minic_type_conditional_pointer_common(when_true, when_false, result)) {\n"
    "        return true;\n"
    "    }\n"
    "    if (minic_type_is_integer(when_true) && minic_type_is_integer(when_false)) {\n"
    "        return minic_target_info_integer_common(target, when_true, when_false, result);\n"
    "    }\n"
    "    has_double_operand = minic_type_is_double(when_true) || minic_type_is_double(when_false);\n"
    "    has_numeric_operands =\n"
    "        (minic_type_is_double(when_true) || minic_type_is_integer(when_true)) &&\n"
    "        (minic_type_is_double(when_false) || minic_type_is_integer(when_false));\n"
    "    if (has_double_operand && has_numeric_operands) {\n"
    "        *result = minic_type_double();\n"
    "        return true;\n"
    "    }\n"
    "    return false;\n"
    "}\n\n"
    "bool minic_c0_conditional_result_type(const MinicC0Program *program,\n"
    "                                      const MinicTargetInfo *target,\n"
    "                                      MinicExpressionId when_true_expression_id,\n"
    "                                      MinicExpressionId when_false_expression_id,\n"
    "                                      MinicType *result) {\n"
    "    const MinicExpression *when_true;\n"
    "    const MinicExpression *when_false;\n\n"
    "    if (program == NULL || target == NULL || result == NULL) {\n"
    "        return false;\n"
    "    }\n"
    "    when_true = minic_c0_program_expression(program, when_true_expression_id);\n"
    "    when_false = minic_c0_program_expression(program, when_false_expression_id);\n"
    "    if (when_true == NULL || when_false == NULL) {\n"
    "        return false;\n"
    "    }\n"
    "    if (minic_type_is_pointer(when_true->type) &&\n"
    "        minic_c0_expression_is_null_pointer_constant_v0(program, when_false_expression_id)) {\n"
    "        *result = when_true->type;\n"
    "        return true;\n"
    "    }\n"
    "    if (minic_c0_expression_is_null_pointer_constant_v0(program, when_true_expression_id) &&\n"
    "        minic_type_is_pointer(when_false->type)) {\n"
    "        *result = when_false->type;\n"
    "        return true;\n"
    "    }\n"
    "    return conditional_type_only(target, when_true->type, when_false->type, result);\n"
    "}\n""".replace('"\n    "', ""),
)

# Build topology mirrors the semantic boundary: AST-only ownership tests keep
# linking ast.c alone, while the compiler and AST contract verifier link the
# target-aware expression-semantics module.
path = "Makefile"
text = read(path)
text = replace_exact(
    text,
    "\tsrc/frontend/ast.c \\\n\tsrc/frontend/ast_traversal.c \\\n",
    "\tsrc/frontend/ast.c \\\n\tsrc/frontend/expression_semantics.c \\\n\tsrc/frontend/ast_traversal.c \\\n",
    label="add expression semantics to compiler sources",
)
text = replace_exact(
    text,
    "AST_CONTRACT_TEST_SOURCES := \\\n\tsrc/frontend/ast.c \\\n\tsrc/frontend/ast_global.c \\\n",
    "AST_CONTRACT_TEST_SOURCES := \\\n\tsrc/frontend/ast.c \\\n\tsrc/frontend/expression_semantics.c \\\n\tsrc/frontend/ast_global.c \\\n",
    label="add expression semantics to AST contract test",
)
write(path, text)

# Parser expression typing already owns an explicit target. Thread it through
# the one targetless helper and use the model everywhere else in this file.
path = "src/frontend/parser_expression.c"
text = read(path)
text = replace_exact(
    text,
    '#include "frontend/parser_internal.h"\n\n',
    '#include "frontend/parser_internal.h"\n#include "frontend/expression_semantics.h"\n\n',
    label="parser expression includes semantic helper",
)
text = replace_exact(
    text,
    "static bool binary_result_type(const MinicC0Program *program,\n",
    "static bool binary_result_type(const MinicTargetInfo *target,\n"
    "                               const MinicC0Program *program,\n",
    label="binary result target parameter",
)
start, end = function_region(text, "static bool binary_result_type(")
region = text[start:end]
old_count = region.count("minic_type_integer_common(")
if old_count != 2:
    raise RuntimeError(f"binary_result_type: expected 2 common-type uses, found {old_count}")
region = region.replace(
    "minic_type_integer_common(", "minic_target_info_integer_common(target, "
)
text = text[:start] + region + text[end:]
outside_count = text.count("minic_type_integer_common(")
if outside_count == 0:
    raise RuntimeError("parser_expression: expected common-type consumers outside binary_result_type")
text = text.replace(
    "minic_type_integer_common(",
    "minic_target_info_integer_common(parser->target_info, ",
)
text = replace_exact(
    text,
    "        } else if (!binary_result_type(parser->program,\n",
    "        } else if (!binary_result_type(parser->target_info,\n"
    "                                       parser->program,\n",
    label="binary result caller passes target",
)
text = replace_exact(
    text,
    "                parser->program, when_true, when_false, &conditional.type)) {",
    "                parser->program, parser->target_info, when_true, when_false, &conditional.type)) {",
    label="conditional parser passes target",
)
write(path, text)

# Statement-level legacy compound operations are still direct-AST migration
# debt, but while they exist they must use the same target integer semantics.
path = "src/frontend/parser_statement.c"
text = read(path)
count = text.count("minic_type_integer_common(")
if count != 5:
    raise RuntimeError(f"parser_statement: expected 5 common-type uses, found {count}")
text = text.replace(
    "minic_type_integer_common(",
    "minic_target_info_integer_common(parser->target_info, ",
)
write(path, text)

# Verifier checks must be target-consistent with construction. The expression
# verifier already receives TargetInfo; thread it into the two local helpers.
path = "src/frontend/ast_verifier.c"
text = read(path)
text = replace_exact(
    text,
    '#include "frontend/ast_verifier.h"\n\n',
    '#include "frontend/ast_verifier.h"\n#include "frontend/expression_semantics.h"\n\n',
    label="verifier includes expression semantics",
)
text = replace_exact(
    text,
    "static bool verify_binary_type(const MinicC0Program *program,\n"
    "                               const MinicExpression *expression,",
    "static bool verify_binary_type(const MinicC0Program *program,\n"
    "                               const MinicTargetInfo *target,\n"
    "                               const MinicExpression *expression,",
    label="binary verifier target parameter",
)
start, end = function_region(text, "static bool verify_binary_type(")
region = text[start:end]
region = replace_exact(
    region,
    "minic_type_integer_promotion(",
    "minic_target_info_integer_promotion(target, ",
    1,
    "binary verifier promotion",
)
region = replace_exact(
    region,
    "minic_type_integer_common(",
    "minic_target_info_integer_common(target, ",
    1,
    "binary verifier common type",
)
text = text[:start] + region + text[end:]

start, end = function_region(text, "static bool verify_expression(")
region = text[start:end]
region = replace_exact(
    region,
    "minic_type_integer_promotion(",
    "minic_target_info_integer_promotion(target, ",
    1,
    "expression verifier promotion",
)
region = replace_exact(
    region,
    "minic_type_integer_common(",
    "minic_target_info_integer_common(target, ",
    1,
    "expression verifier common type",
)
region = replace_exact(
    region,
    "               verify_binary_type(program, expression, left, right, form);",
    "               verify_binary_type(program, target, expression, left, right, form);",
    label="expression verifier passes target to binary verifier",
)
region = replace_exact(
    region,
    "               minic_c0_conditional_result_type(program,\n"
    "                                                expression->value.conditional.when_true,",
    "               minic_c0_conditional_result_type(program,\n"
    "                                                target,\n"
    "                                                expression->value.conditional.when_true,",
    label="conditional verifier passes target",
)
text = text[:start] + region + text[end:]

text = replace_exact(
    text,
    "static bool verify_statement(const MinicC0Program *program, const MinicStatement *statement) {",
    "static bool verify_statement(const MinicC0Program *program,\n"
    "                             const MinicTargetInfo *target_info,\n"
    "                             const MinicStatement *statement) {",
    label="statement verifier target parameter",
)
start, end = function_region(text, "static bool verify_statement(")
region = text[start:end]
region = replace_exact(
    region,
    "minic_type_integer_common(",
    "minic_target_info_integer_common(target_info, ",
    1,
    "statement verifier common type",
)
text = text[:start] + region + text[end:]
text = replace_exact(
    text,
    "        if (!verify_statement(program, &program->statements[index])) {",
    "        if (!verify_statement(program, target, &program->statements[index])) {",
    label="program verifier passes target to statements",
)
write(path, text)

# Typed ConstEval already receives TargetInfo. Its comparison/shift folding must
# use exactly the same common-type/promotion model as Parser and Verifier.
path = "src/frontend/const_eval.c"
text = read(path)
count = text.count("minic_type_integer_common(")
if count != 2:
    raise RuntimeError(f"const_eval: expected 2 common-type uses, found {count}")
text = text.replace(
    "minic_type_integer_common(",
    "minic_target_info_integer_common(target, ",
)
write(path, text)

# The current textual RV64 backend still re-derives common operand types in a
# few direct-AST paths. Make that target dependency explicit. Core IR will
# eventually remove this semantic revalidation from the physical emitter.
path = "src/target/riscv64/codegen_expression.c"
text = read(path)
count = text.count("minic_type_integer_common(")
if count != 2:
    raise RuntimeError(f"codegen_expression: expected 2 common-type uses, found {count}")
text = text.replace(
    "minic_type_integer_common(",
    "minic_target_info_integer_common(minic_default_target_info(), ",
)
write(path, text)

path = "src/target/riscv64/codegen_statement.c"
text = read(path)
text = replace_exact(
    text,
    '#include "target/riscv64/codegen_internal.h"\n\n',
    '#include "target/riscv64/codegen_internal.h"\n#include "target/target_info.h"\n\n',
    label="statement emitter includes target info",
)
count = text.count("minic_type_integer_common(")
if count != 1:
    raise RuntimeError(f"codegen_statement: expected 1 common-type use, found {count}")
text = text.replace(
    "minic_type_integer_common(",
    "minic_target_info_integer_common(minic_default_target_info(), ",
)
write(path, text)

# Prove the old targetless semantic entry points have no remaining production
# or test consumers. This is intentionally a migration assertion, not a
# permanent source-specific test.
remaining = []
for root_name in ("include", "src", "tests", "tools/minic"):
    root = ROOT / root_name
    if not root.exists():
        continue
    for candidate in root.rglob("*"):
        if candidate.suffix not in (".c", ".h"):
            continue
        data = candidate.read_text()
        if "minic_type_integer_promotion(" in data or "minic_type_integer_common(" in data:
            remaining.append(str(candidate.relative_to(ROOT)))
if remaining:
    raise RuntimeError("targetless integer semantic consumers remain: " + ", ".join(remaining))

print("PASS target integer consumer migration")
