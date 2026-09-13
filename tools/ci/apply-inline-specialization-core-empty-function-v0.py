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
count = text.count(old)
if count != 1:
    raise SystemExit(f"expected one Core storage guard, found {count}")
text = text.replace(old, new, 1)

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
count = text.count(old)
if count != 1:
    raise SystemExit(f"expected one Core block walk anchor, found {count}")
text = text.replace(old, new, 1)

p.write_text(text)
print("MINIC_INLINE_SPECIALIZATION_CORE_EMPTY_FUNCTION_V0=APPLIED")
