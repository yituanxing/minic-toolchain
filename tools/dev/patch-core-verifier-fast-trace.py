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
print("CORE_FAST_VERIFY_TRACE_PATCHED")
