#!/usr/bin/env python3
from pathlib import Path

p = Path("src/compiler/compiler.c")
text = p.read_text()

old = '''    if (core->blocks == NULL || core->instructions == NULL ||
        core->entry_block >= core->block_count) {
        return false;
    }
'''
new = '''    if (core->blocks == NULL ||
        (core->instruction_count != 0U && core->instructions == NULL) ||
        core->entry_block >= core->block_count) {
        return false;
    }
'''
if text.count(new) == 1 and text.count(old) == 0:
    print("MINIC_INLINE_CORE_EMPTY_V0=STORAGE_ALREADY_CANONICAL")
elif text.count(old) == 1 and text.count(new) == 0:
    text = text.replace(old, new, 1)
else:
    raise SystemExit("Core empty storage guard: missing, duplicate or partial")

old = '''        const MinicCoreBlock *block = &core->blocks[block_id];
        size_t block_instruction_index;

        for (block_instruction_index = 0U;
'''
new = '''        const MinicCoreBlock *block = &core->blocks[block_id];
        size_t block_instruction_index;

        /* A valid block may contain only a terminator and therefore own no
         * instruction-id array.  Require storage only when the block actually
         * contains instructions. */
        if (block->instruction_count != 0U && block->instructions == NULL) {
            goto done;
        }

        for (block_instruction_index = 0U;
'''
if text.count(new) == 1 and text.count(old) == 0:
    print("MINIC_INLINE_CORE_EMPTY_V0=BLOCK_ALREADY_CANONICAL")
elif text.count(old) == 1 and text.count(new) == 0:
    text = text.replace(old, new, 1)
else:
    raise SystemExit("Core empty block walk: missing, duplicate or partial")

p.write_text(text)
print("MINIC_INLINE_SPECIALIZATION_CORE_EMPTY_FUNCTION_V0=APPLIED")
