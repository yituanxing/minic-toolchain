#!/usr/bin/env python3
from pathlib import Path

p = Path("src/compiler/compiler.c")
text = p.read_text()
old = '''    if (block_reachable == NULL || block_queue == NULL ||
        !minic_enqueue_core_block(core->entry_block,
                                 core->block_count,
                                 block_reachable,
                                 block_queue,
                                 &block_queue_count)) {
        goto done;
    }

    while (block_queue_cursor < block_queue_count) {
'''
new = '''    if (block_reachable == NULL || block_queue == NULL ||
        !minic_enqueue_core_block(core->entry_block,
                                 core->block_count,
                                 block_reachable,
                                 block_queue,
                                 &block_queue_count)) {
        goto done;
    }
    /* M180_CORE_LABEL_REENTRY_ROOTS: match the backend's reachability policy.
     * A source C label is a legal re-entry root for goto/asm-goto. If pruning
     * ignores these roots while codegen preserves them, a call emitted from a
     * label block can target a specialization clone that was incorrectly
     * dropped here. */
    {
        size_t root_block;
        for (root_block = 0U; root_block < core->block_count; ++root_block) {
            if (core->blocks[root_block].source_label_id != SIZE_MAX &&
                !minic_enqueue_core_block((MinicCoreBlockId)root_block,
                                         core->block_count,
                                         block_reachable,
                                         block_queue,
                                         &block_queue_count)) {
                goto done;
            }
        }
    }

    while (block_queue_cursor < block_queue_count) {
'''
count = text.count(old)
if count != 1:
    raise SystemExit(f"expected one Core entry-root anchor, found {count}")
p.write_text(text.replace(old, new, 1))
print("M180_CORE_LABEL_REENTRY_ROOTS=APPLIED")
