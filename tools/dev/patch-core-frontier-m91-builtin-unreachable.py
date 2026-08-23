#!/usr/bin/env python3
# M91: lower GNU __builtin_unreachable() as an explicit Core CFG terminator.

from pathlib import Path

IR_H = Path("src/core/core_ir.h")
IR_C = Path("src/core/core_ir.c")
LOWER = Path("src/core/core_lower.c")
CODEGEN = Path("src/target/riscv64/core_codegen.c")
MARKER = "M91_BUILTIN_UNREACHABLE_TERMINATOR"


def replace_once(text: str, old: str, new: str, name: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"M91 {name} anchor count={count}")
    return text.replace(old, new, 1)


def patch_ir_h() -> None:
    text = IR_H.read_text()
    if MARKER in text:
        print("M91 core_ir.h already applied")
        return
    old = '''typedef enum MinicCoreTerminatorKind {
    MINIC_CORE_TERMINATOR_RETURN = 0,
    MINIC_CORE_TERMINATOR_BRANCH,
    MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH
} MinicCoreTerminatorKind;
'''
    new = '''/* M91_BUILTIN_UNREACHABLE_TERMINATOR: unreachable is a CFG fact, not a
   value-producing instruction and not an invented target trap. */
typedef enum MinicCoreTerminatorKind {
    MINIC_CORE_TERMINATOR_RETURN = 0,
    MINIC_CORE_TERMINATOR_BRANCH,
    MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH,
    MINIC_CORE_TERMINATOR_UNREACHABLE
} MinicCoreTerminatorKind;
'''
    IR_H.write_text(replace_once(text, old, new, "terminator-enum"))
    print("M91 core_ir.h applied")


def patch_ir_c() -> None:
    text = IR_C.read_text()
    if MARKER in text:
        print("M91 core_ir.c already applied")
        return
    verify_anchor = '''    case MINIC_CORE_TERMINATOR_BRANCH:
        return terminator->branch_target < function->block_count;
'''
    verify_new = '''    case MINIC_CORE_TERMINATOR_UNREACHABLE:
        return terminator->return_value == MINIC_CORE_VALUE_INVALID &&
               terminator->return_object == MINIC_CORE_OBJECT_INVALID;
    case MINIC_CORE_TERMINATOR_BRANCH:
        return terminator->branch_target < function->block_count;
'''
    text = replace_once(text, verify_anchor, verify_new, "terminator-verify")
    dump_anchor = '''    case MINIC_CORE_TERMINATOR_BRANCH:
        return fprintf(output, "  br bb%" PRIu32 "\\n", terminator->branch_target) >= 0;
'''
    dump_new = '''    case MINIC_CORE_TERMINATOR_UNREACHABLE:
        return fprintf(output, "  unreachable\\n") >= 0;
    case MINIC_CORE_TERMINATOR_BRANCH:
        return fprintf(output, "  br bb%" PRIu32 "\\n", terminator->branch_target) >= 0;
'''
    text = replace_once(text, dump_anchor, dump_new, "terminator-dump")
    # The marker is carried by the enum comment in the header; add one here for idempotence.
    marker_anchor = '''static bool verify_terminator(const MinicCoreFunction *function,
'''
    text = replace_once(text, marker_anchor, '''/* M91_BUILTIN_UNREACHABLE_TERMINATOR */
''' + marker_anchor, "ir-marker")
    IR_C.write_text(text)
    print("M91 core_ir.c applied")


def patch_lower() -> None:
    text = LOWER.read_text()
    if MARKER in text:
        print("M91 core_lower.c already applied")
        return
    expr_anchor = '''    /* M83B_CALL_STATEMENT_DISPATCH: CALL ownership lives in lower_expression().
       Statement context only discards the produced value; it must not force an
       indirect call back through the legacy direct-call helper. */
'''
    expr_new = '''    /* M91_BUILTIN_UNREACHABLE_TERMINATOR: GNU C marks this control-flow
       point unreachable. Preserve that fact in Core rather than rejecting the
       void expression or inventing a target-specific trap. */
    if (expression->kind == MINIC_EXPRESSION_BUILTIN_UNREACHABLE) {
        MinicCoreTerminator terminator;

        if (!minic_type_is_void(expression->type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&terminator, 0, sizeof(terminator));
        terminator.kind = MINIC_CORE_TERMINATOR_UNREACHABLE;
        terminator.span = expression->span;
        terminator.return_value = MINIC_CORE_VALUE_INVALID;
        terminator.return_object = MINIC_CORE_OBJECT_INVALID;
        terminator.branch_target = MINIC_CORE_BLOCK_INVALID;
        terminator.conditional.condition = MINIC_CORE_VALUE_INVALID;
        terminator.conditional.when_true = MINIC_CORE_BLOCK_INVALID;
        terminator.conditional.when_false = MINIC_CORE_BLOCK_INVALID;
        return minic_core_function_set_terminator(
                   context->function, context->block_id, &terminator)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }

''' + expr_anchor
    text = replace_once(text, expr_anchor, expr_new, "expression-unreachable")
    switch_anchor = '''            case MINIC_STATEMENT_EXPRESSION:
                status = lower_expression_statement(context, statement);
                break;
'''
    switch_new = '''            case MINIC_STATEMENT_EXPRESSION:
                status = lower_expression_statement(context, statement);
                if (status == MINIC_CORE_LOWER_OK &&
                    context->block_id < context->function->block_count &&
                    context->function->blocks[context->block_id].has_terminator) {
                    statement_terminated = true;
                }
                break;
'''
    text = replace_once(text, switch_anchor, switch_new, "expression-termination")
    LOWER.write_text(text)
    print("M91 core_lower.c applied")


def patch_codegen() -> None:
    text = CODEGEN.read_text()
    if MARKER in text:
        print("M91 core_codegen.c already applied")
        return
    anchor = '''    case MINIC_CORE_TERMINATOR_BRANCH:
        return fprintf(
                   file, "  j .L%s_core_bb%" PRIu32 "\\n", symbol_name, terminator->branch_target) >=
               0;
'''
    new = '''    /* M91_BUILTIN_UNREACHABLE_TERMINATOR: reaching this block is UB; no
       target instruction is required. The Core terminator still prevents
       normal CFG fallthrough from being modeled as a supported continuation. */
    case MINIC_CORE_TERMINATOR_UNREACHABLE:
        return true;
    case MINIC_CORE_TERMINATOR_BRANCH:
        return fprintf(
                   file, "  j .L%s_core_bb%" PRIu32 "\\n", symbol_name, terminator->branch_target) >=
               0;
'''
    CODEGEN.write_text(replace_once(text, anchor, new, "terminator-codegen"))
    print("M91 core_codegen.c applied")


def main() -> int:
    patch_ir_h()
    patch_ir_c()
    patch_lower()
    patch_codegen()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
