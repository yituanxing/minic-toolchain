#!/usr/bin/env python3
from pathlib import Path

p = Path("src/core/core_lower_asm.c")
text = p.read_text()
old = '''    /* All seven instructions must belong to the same source assignment. */
    for (index = 1U; index < 7U; ++index) {
        const MinicCoreInstruction *candidate = &context->function->instructions[ids[index]];
        if (candidate->span.begin.line != constant->span.begin.line) {
            return false;
        }
    }
    block->instruction_count -= 7U;
    if (getenv("MINIC_LOCAL_FACT_TRACE") != NULL) {
        (void)fprintf(stderr,
                      "RK_EARLY_REWIND function=%s local=%zu object=%u removed=7\\n",
                      context->source_function->name != NULL ? context->source_function->name : "?",
                      (size_t)local_id,
                      (unsigned int)local_object);
    }
    return true;
'''
new = '''    /* All seven instructions must belong to the same source assignment. */
    for (index = 1U; index < 7U; ++index) {
        const MinicCoreInstruction *candidate = &context->function->instructions[ids[index]];
        if (candidate->span.begin.line != constant->span.begin.line) {
            return false;
        }
    }
    /* Core verification requires every global instruction/value to be owned by
       a block.  Therefore removing a block tail cannot leave global tombstones.
       This transform is allowed only when the seven instructions are also the
       exact global instruction tail and every produced value forms the exact
       global value tail.  Objects deliberately remain monotonic: unreferenced
       temporary objects are verifier-valid, while the output local object must
       retain its stable ID. */
    if (context->function->instruction_count < 7U) {
        return false;
    }
    {
        size_t global_begin = context->function->instruction_count - 7U;
        size_t produced_values = 0U;
        size_t expected_value;
        for (index = 0U; index < 7U; ++index) {
            const MinicCoreInstruction *candidate;
            if ((size_t)ids[index] != global_begin + index) {
                return false;
            }
            candidate = &context->function->instructions[ids[index]];
            if (candidate->result != MINIC_CORE_VALUE_INVALID) {
                produced_values += 1U;
            }
        }
        if (produced_values > context->function->value_count) {
            return false;
        }
        expected_value = context->function->value_count - produced_values;
        for (index = 0U; index < 7U; ++index) {
            const MinicCoreInstruction *candidate =
                &context->function->instructions[ids[index]];
            if (candidate->result != MINIC_CORE_VALUE_INVALID) {
                if ((size_t)candidate->result != expected_value ||
                    context->function->values[candidate->result].definition != ids[index]) {
                    return false;
                }
                expected_value += 1U;
            }
        }
        if (expected_value != context->function->value_count) {
            return false;
        }
        block->instruction_count -= 7U;
        context->function->instruction_count -= 7U;
        context->function->value_count -= produced_values;
        if (getenv("MINIC_LOCAL_FACT_TRACE") != NULL) {
            (void)fprintf(stderr,
                          "RK_EARLY_REWIND function=%s local=%zu object=%u removed_insts=7 removed_values=%zu\\n",
                          context->source_function->name != NULL ? context->source_function->name : "?",
                          (size_t)local_id,
                          (unsigned int)local_object,
                          produced_values);
        }
    }
    return true;
'''
if text.count(old) != 1:
    raise SystemExit(f"expected one rK rewind tail body, found {text.count(old)}")
p.write_text(text.replace(old, new, 1))
print("MINIC_INLINE_ASM_RK_TAIL_ROLLBACK_V0=APPLIED")
