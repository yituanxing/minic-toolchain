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
    /* RESIDUAL8_CONST_CALL_TRACE_V0: diagnostic-only focus trace.  Emit only
       failed integer-specialization evaluation so normal successful helpers do
       not flood stderr. */
    if (!success && callee->is_integer_specialization) {
        (void)fprintf(stderr,
                      "CFG_CONST_CALLEE_FAIL name=%.*s source=%zu known0=%d bits0=%llu\\n",
                      callee->name != NULL ? (int)callee->name_length : 0,
                      callee->name != NULL ? callee->name : "",
                      (size_t)callee->specialization_source,
                      (callee->parameter_count != 0U && callee->specialization_integer_known[0]) ? 1 : 0,
                      (unsigned long long)(callee->parameter_count != 0U
                          ? callee->specialization_integer_bits[0] : 0U));
    }
    free(facts);
    return success;
}
'''
count = text.count(old)
if count != 1:
    raise SystemExit(f"constant callee trace anchor: expected one, found {count}")
p.write_text(text.replace(old, new, 1))
print("MINIC_RESIDUAL8_CONST_CALL_TRACE_V0=APPLIED failed_only=1")
