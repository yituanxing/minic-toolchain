#!/usr/bin/env python3
# Route while/normalized-for conditions through the shared Core condition branch owner.

from pathlib import Path

MARKER = "M84_SHARED_LOOP_CONDITION_BRANCH"
LOWER = Path("src/core/core_lower.c")


def replace_once(text: str, old: str, new: str, name: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"M84 {name} anchor count={count}")
    return text.replace(old, new, 1)


def main() -> int:
    text = LOWER.read_text()
    if MARKER in text:
        print("M84 shared loop condition branch already applied")
        return 0

    text = replace_once(
        text,
        '''    MinicCoreBlockId preheader_block;
    MinicCoreBlockId saved_break_target;
    MinicCoreTerminator terminator;
    MinicCoreValueId condition;
    MinicCoreLowerStatus status;
''',
        '''    MinicCoreBlockId preheader_block;
    MinicCoreBlockId saved_break_target;
    MinicCoreLowerStatus status;
''',
        "remove-duplicate-condition-state",
    )

    text = replace_once(
        text,
        '''        if (condition_expression == NULL || !minic_type_is_integer(condition_expression->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
''',
        '''        if (condition_expression == NULL ||
            (!minic_type_is_integer(condition_expression->type) &&
             !minic_type_is_pointer(condition_expression->type))) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
''',
        "allow-pointer-condition",
    )

    text = replace_once(
        text,
        '''    } else {
        status = lower_expression(context, statement->expression, &condition);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (!minic_type_is_integer(context->function->values[condition].type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&terminator, 0, sizeof(terminator));
        terminator.kind = MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH;
        terminator.span = statement->span;
        terminator.return_value = MINIC_CORE_VALUE_INVALID;
        terminator.conditional.condition = condition;
        terminator.conditional.when_true = body_block;
        terminator.conditional.when_false = exit_block;
        if (!minic_core_function_set_terminator(
                context->function, condition_block, &terminator)) {
            return MINIC_CORE_LOWER_ERROR;
        }
    }
''',
        '''    } else {
        /* M84_SHARED_LOOP_CONDITION_BRANCH: if/while/normalized-for share one
           scalar-condition owner. This admits pointer truth values and keeps
           !/&&/|| short-circuit CFG construction out of the loop lowering. */
        status = lower_condition_branch(context,
                                        statement->expression,
                                        statement->span,
                                        body_block,
                                        exit_block);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
    }
''',
        "shared-condition-branch",
    )

    LOWER.write_text(text)
    print("M84 shared loop condition branch applied")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
