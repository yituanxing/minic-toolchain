#!/usr/bin/env python3
from pathlib import Path
import subprocess

# Reuse the already-audited V0 fact-table/CFG machinery, but deliberately
# remove its broad lower_expression() integer-folding hook.  V1 exposes local
# facts only to compile-time control-flow reachability (lower_if); ordinary
# expression code generation remains on the existing lowering path.
subprocess.run(["python3", "tools/ci/apply-local-integer-constants-v0.py"], check=True)

p = Path("src/core/core_lower.c")
text = p.read_text()
old = '''    if (minic_type_is_integer(expression->type) && context->target != NULL) {
        MinicConstValue tracked_constant;
        if (core_const_eval_integer_with_locals(context, expression_id, &tracked_constant) &&
            minic_type_equal(tracked_constant.type, expression->type)) {
            uint64_t tracked_bits = tracked_constant.bits;
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
            instruction.span = expression->span;
            instruction.type = expression->type;
            instruction.result = MINIC_CORE_VALUE_INVALID;
            (void)memcpy(&instruction.value.integer_value, &tracked_bits, sizeof(tracked_bits));
            return minic_core_function_append_value_instruction(
                       context->function, context->block_id, &instruction, value_id)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR;
        }
    }
'''
count = text.count(old)
if count != 1:
    raise SystemExit(f"expected one broad lower_expression fold hook, found {count}")
p.write_text(text.replace(old, "", 1))
print("MINIC_LOCAL_INTEGER_CONDITIONS_V1=APPLIED")
