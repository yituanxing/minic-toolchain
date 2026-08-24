#!/usr/bin/env python3
from pathlib import Path

path = Path("src/core/core_ir.c")
text = path.read_text()
old = '''        if (!instruction_is_valid(function, instruction, available_values)) {
            return false;
        }
'''
new = '''        if (!instruction_is_valid(function, instruction, available_values)) {
            (void)fprintf(stderr,
                          "CORE_VERIFY_DETAIL block=%u instruction=%u kind=%d result=%u\\n",
                          (unsigned int)block_id,
                          (unsigned int)instruction_id,
                          (int)instruction->kind,
                          (unsigned int)instruction->result);
            return false;
        }
'''
if new not in text:
    if text.count(old) != 1:
        raise SystemExit(f"instruction verifier anchor count={text.count(old)}")
    text = text.replace(old, new, 1)
old = '''    return terminator_is_valid(function, &block->terminator, available_values);
}
'''
new = '''    if (!terminator_is_valid(function, &block->terminator, available_values)) {
        (void)fprintf(stderr,
                      "CORE_VERIFY_DETAIL block=%u terminator=%d condition=%u\\n",
                      (unsigned int)block_id,
                      (int)block->terminator.kind,
                      (unsigned int)block->terminator.conditional.condition);
        return false;
    }
    return true;
}
'''
if new not in text:
    if text.count(old) != 1:
        raise SystemExit(f"terminator verifier anchor count={text.count(old)}")
    text = text.replace(old, new, 1)
path.write_text(text)

path = Path("src/core/core_lower.c")
text = path.read_text()
old = '''    return lower_assignment_pair(context, target_id, source_id, statement->span);
}

static MinicCoreLowerStatus lower_scalar_update'''
new = '''    {
        MinicCoreLowerStatus assignment_status;
        const MinicExpression *target_expression;
        const MinicExpression *source_expression;

        assignment_status =
            lower_assignment_pair(context, target_id, source_id, statement->span);
        if (assignment_status == MINIC_CORE_LOWER_ERROR) {
            target_expression = minic_c0_program_expression(context->body->program, target_id);
            source_expression = minic_c0_program_expression(context->body->program, source_id);
            (void)fprintf(stderr,
                          "CORE_ASSIGN_DETAIL function=%s target_kind=%d source_kind=%d "
                          "target_vc=%d source_vc=%d span=%zu:%zu\\n",
                          context->source_function != NULL ? context->source_function->name : "?",
                          target_expression != NULL ? (int)target_expression->kind : -1,
                          source_expression != NULL ? (int)source_expression->kind : -1,
                          target_expression != NULL ? (int)target_expression->value_category : -1,
                          source_expression != NULL ? (int)source_expression->value_category : -1,
                          statement->span.begin.line,
                          statement->span.begin.column);
        }
        return assignment_status;
    }
}

static MinicCoreLowerStatus lower_scalar_update'''
if new not in text:
    if text.count(old) != 1:
        raise SystemExit(f"assignment trace anchor count={text.count(old)}")
    text = text.replace(old, new, 1)
path.write_text(text)
print("CORE_FAST_VERIFY_TRACE_PATCHED")