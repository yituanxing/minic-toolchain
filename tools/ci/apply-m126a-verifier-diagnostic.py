#!/usr/bin/env python3
from pathlib import Path

path = Path("src/core/core_ir.c")
source = path.read_text()

# First instrument verify_block, because minic_core_function_verify propagates a
# block failure through its final `return valid;` rather than a direct false return.
old_instruction = """        if (!instruction_is_valid(function, instruction, available_values)) {
            return false;
        }
"""
new_instruction = """        if (!instruction_is_valid(function, instruction, available_values)) {
            (void)fprintf(stderr,
                          "M126A_VERIFY_BLOCK_FAIL block=%" PRIu32 " slot=%zu instruction=%" PRIu32 " kind=%d result=%" PRIu32 " values=%zu asms=%zu\\n",
                          block_id,
                          index,
                          instruction_id,
                          (int)instruction->kind,
                          instruction->result,
                          function->value_count,
                          function->inline_asm_count);
            return false;
        }
"""
if source.count(old_instruction) != 1:
    raise SystemExit("verify_block instruction anchor mismatch")
source = source.replace(old_instruction, new_instruction, 1)

old_terminator = """    return terminator_is_valid(function, &block->terminator, available_values);
}

bool minic_core_function_verify"""
new_terminator = """    if (!terminator_is_valid(function, &block->terminator, available_values)) {
        (void)fprintf(stderr,
                      "M126A_VERIFY_TERMINATOR_FAIL block=%" PRIu32 " kind=%d instructions=%zu\\n",
                      block_id,
                      (int)block->terminator.kind,
                      block->instruction_count);
        return false;
    }
    return true;
}

bool minic_core_function_verify"""
if source.count(old_terminator) != 1:
    raise SystemExit("verify_block terminator anchor mismatch")
source = source.replace(old_terminator, new_terminator, 1)

start_marker = "bool minic_core_function_verify(const MinicCoreFunction *function) {"
end_marker = "bool minic_core_function_dump(FILE *output, const MinicCoreFunction *function) {"
begin = source.find(start_marker)
if begin < 0:
    raise SystemExit("Core verifier start marker not found")
end = source.find(end_marker, begin)
if end < 0:
    raise SystemExit("Core verifier dump marker not found")
body = source[begin:end]
needle = "return false;"
count = body.count(needle)
if count == 0:
    raise SystemExit("Core verifier has no false returns to instrument")
replacement = (
    'do { '
    '(void)fprintf(stderr, "M126A_VERIFY_DIRECT_FAIL line=%d function=%s\\n", '
    '__LINE__, function != NULL && function->name != NULL ? function->name : "?"); '
    'return false; '
    '} while (0);'
)
body = body.replace(needle, replacement)
old_final = """    return valid;
}

"""
new_final = """    if (!valid) {
        (void)fprintf(stderr,
                      "M126A_VERIFY_FINAL_FALSE function=%s blocks=%zu instructions=%zu values=%zu asms=%zu\\n",
                      function->name,
                      function->block_count,
                      function->instruction_count,
                      function->value_count,
                      function->inline_asm_count);
    }
    return valid;
}

"""
if body.count(old_final) != 1:
    raise SystemExit("Core verifier final return anchor mismatch")
body = body.replace(old_final, new_final, 1)
path.write_text(source[:begin] + body + source[end:])
print(f"M126A verifier diagnostic staged direct_false_returns={count}")
