#!/usr/bin/env python3
from pathlib import Path

p = Path("src/core/core_lower.c")
text = p.read_text()
marker = "RESIDUAL8_CONST_CALL_TRACE_V0"
if marker in text:
    print("MINIC_RESIDUAL8_CONST_CALL_TRACE_V0=ALREADY")
    raise SystemExit(0)
old = '''    success = core_const_eval_integer_with_locals(
        &callee_context, return_expression, value);
    free(facts);
    return success;
}
'''
new = '''    success = core_const_eval_integer_with_locals(
        &callee_context, return_expression, value);
    /* RESIDUAL8_CONST_CALL_TRACE_V0: diagnostic-only focus trace. */
    if (getenv("MINIC_CFG_CONST_TRACE") != NULL &&
        callee->is_integer_specialization) {
        (void)fprintf(stderr,
                      "CFG_CONST_CALLEE name=%.*s source=%zu success=%d known0=%d bits0=%llu result=%llu\\n",
                      callee->name != NULL ? (int)callee->name_length : 0,
                      callee->name != NULL ? callee->name : "",
                      (size_t)callee->specialization_source,
                      success ? 1 : 0,
                      (callee->parameter_count != 0U && callee->specialization_integer_known[0]) ? 1 : 0,
                      (unsigned long long)(callee->parameter_count != 0U
                          ? callee->specialization_integer_bits[0] : 0U),
                      (unsigned long long)(success ? value->bits : 0U));
    }
    free(facts);
    return success;
}
'''
count = text.count(old)
if count != 1:
    raise SystemExit(f"constant callee trace anchor: expected one, found {count}")
p.write_text(text.replace(old, new, 1))
print("MINIC_RESIDUAL8_CONST_CALL_TRACE_V0=APPLIED")
