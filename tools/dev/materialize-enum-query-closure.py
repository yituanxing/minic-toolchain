#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def replace_exact(path, old, new, expected=1):
    file_path = ROOT / path
    text = file_path.read_text()
    count = text.count(old)
    if count != expected:
        raise SystemExit(f"{path}: expected {expected} occurrences, found {count}: {old!r}")
    file_path.write_text(text.replace(old, new))


# Verifier must consume the same program-aware integer semantics as parser/Sema.
replace_exact(
    "src/frontend/ast_verifier.c",
    "minic_target_info_integer_common(target, left->type, right->type, &common_type)",
    "minic_target_info_integer_common_for_program(\n                   target, program, left->type, right->type, &common_type)",
)
replace_exact(
    "src/frontend/ast_verifier.c",
    "minic_target_info_integer_promotion(target, operand->type, &expected_type)",
    "minic_target_info_integer_promotion_for_program(\n                target, program, operand->type, &expected_type)",
)
replace_exact(
    "src/frontend/ast_verifier.c",
    "minic_target_info_integer_common(\n                   target_info, target->type, expression->type, &common_type)",
    "minic_target_info_integer_common_for_program(\n                   target_info, program, target->type, expression->type, &common_type)",
)

# Program-aware target helpers are only necessary for enum identity resolution. Plain integer Core
# IR is intentionally usable without a Semantic AST program, so preserve that standalone contract.
replace_exact(
    "src/target/riscv64/codegen_support.c",
    """bool minic_riscv64_emit_integer_conversion_for_program(FILE *file,
                                                       const MinicC0Program *program,
                                                       MinicType type,
                                                       const char *register_name) {
    MinicType effective_type;

    return minic_c0_type_effective_integer_type(program, type, &effective_type) &&
           minic_riscv64_emit_integer_conversion(file, effective_type, register_name);
}
""",
    """bool minic_riscv64_emit_integer_conversion_for_program(FILE *file,
                                                       const MinicC0Program *program,
                                                       MinicType type,
                                                       const char *register_name) {
    MinicType effective_type;

    if (minic_type_is_enum(type)) {
        if (!minic_c0_type_effective_integer_type(program, type, &effective_type)) {
            return false;
        }
        type = effective_type;
    }
    return minic_riscv64_emit_integer_conversion(file, type, register_name);
}
""",
)

print("ENUM_QUERY_CLOSURE_MATERIALIZED")
