#!/usr/bin/env python3
"""P14: cache per-CoreValue uses once; remove per-OBJECT_ADDRESS full IR rescans.

Candidate is exact with legacy per-address scan: index allocation/traversal
failures fall back to the original search. All object eligibility, intervals,
use-count comparisons and generated assembly semantics remain unchanged.
"""
from pathlib import Path
p=Path("src/target/riscv64/core_codegen.c")
s=p.read_text()
def once(old,new):
    global s
    n=s.count(old)
    if n!=1:
        raise SystemExit(f"P14 unique anchor count={n}: {old[:100]!r}")
    s=s.replace(old,new,1)

helpers=r'''
/* CORE_OBJECT_ADDRESS_USE_INDEX_V1:
 * Whole-function ValueId use counts and direct address ranges. */
typedef struct CoreObjectAddressUse {
    size_t total;
    size_t direct;
    size_t block;
    size_t first;
    size_t last;
    bool cross_block;
} CoreObjectAddressUse;

typedef struct CoreObjectAddressUseBuild {
    CoreObjectAddressUse *entries;
    size_t count;
} CoreObjectAddressUseBuild;

static CoreObjectAddressUse *core_object_address_use_index;

static bool core_object_address_note_use(MinicCoreValueId id, void *opaque) {
    CoreObjectAddressUseBuild *build = (CoreObjectAddressUseBuild *)opaque;
    if (build == NULL || id >= build->count ||
        build->entries[id].total == SIZE_MAX) {
        return false;
    }
    ++build->entries[id].total;
    return true;
}

static bool core_object_address_record_direct(
    CoreObjectAddressUseBuild *build, MinicCoreValueId id,
    size_t block, size_t position) {
    CoreObjectAddressUse *use;
    if (id >= build->count) {
        return false;
    }
    use = &build->entries[id];
    if (use->direct == SIZE_MAX) {
        return false;
    }
    if (use->direct == 0U) {
        use->block = block;
        use->first = position;
        use->last = position;
    } else if (use->block != block) {
        use->cross_block = true;
    } else {
        if (position < use->first) use->first = position;
        if (position > use->last) use->last = position;
    }
    ++use->direct;
    return true;
}

/* Called once per sufficiently large function.  NULL means original scan. */
static bool core_object_address_build_use_index(const MinicCoreFunction *function) {
    CoreObjectAddressUseBuild build;
    size_t block_index;
    if (function == NULL || function->value_count == 0U ||
        function->value_count > SIZE_MAX / sizeof(*build.entries)) {
        return false;
    }
    build.count = function->value_count;
    build.entries = (CoreObjectAddressUse *)calloc(
        build.count, sizeof(*build.entries));
    if (build.entries == NULL) {
        return false;
    }
    for (block_index = 0U; block_index < function->block_count; ++block_index) {
        const MinicCoreBlock *block = &function->blocks[block_index];
        size_t position;
        for (position = 0U; position < block->instruction_count; ++position) {
            MinicCoreInstructionId id = block->instructions[position];
            const MinicCoreInstruction *instruction;
            if (id >= function->instruction_count) goto unavailable;
            instruction = &function->instructions[id];
            if (!core_visit_instruction_value_uses(
                    function, instruction, core_object_address_note_use, &build)) {
                goto unavailable;
            }
            if (instruction->kind == MINIC_CORE_INSTRUCTION_LOAD &&
                !core_object_address_record_direct(
                    &build, instruction->value.load.address, block_index, position)) {
                goto unavailable;
            }
            if (instruction->kind == MINIC_CORE_INSTRUCTION_STORE &&
                !core_object_address_record_direct(
                    &build, instruction->value.store.address, block_index, position)) {
                goto unavailable;
            }
        }
        if (block->has_terminator &&
            !core_visit_terminator_value_uses(
                &block->terminator, core_object_address_note_use, &build)) {
            goto unavailable;
        }
    }
    core_object_address_use_index = build.entries;
    return true;
unavailable:
    free(build.entries);
    return false;
}

'''
once("static bool core_scalar_object_interval(const MinicC0Program *program,\n",helpers+"static bool core_scalar_object_interval(const MinicC0Program *program,\n")

start=s.index("static bool core_scalar_object_interval(const MinicC0Program *program,")
end=s.index("static bool core_scalar_object_intervals_overlap(", start)
fragment=s[start:end]
old="""                total_uses.target = address_value;
                total_uses.count = 0U;
                for (use_block = 0U; use_block < function->block_count; ++use_block) {"""
new="""                if (core_object_address_use_index != NULL) {
                    const CoreObjectAddressUse *use =
                        &core_object_address_use_index[address_value];
                    if (use->cross_block ||
                        (use->direct != 0U &&
                         (use->block != block_index ||
                          !core_scalar_object_note_position(
                              interval, block_index, use->first) ||
                          !core_scalar_object_note_position(
                              interval, block_index, use->last))) ||
                        use->total != use->direct) {
                        interval->reusable = false;
                        return true;
                    }
                    continue;
                }
                /* Allocation or malformed-index fallback: original full scan. */
                total_uses.target = address_value;
                total_uses.count = 0U;
                for (use_block = 0U; use_block < function->block_count; ++use_block) {"""
if fragment.count(old)!=1:raise SystemExit(f"P14 address scanning anchor count={fragment.count(old)}")
fragment=fragment.replace(old,new,1)
s=s[:start]+fragment+s[end:]
once("""static void core_scalar_object_reuse_cache_clear(void) {
    free(core_scalar_object_reuse_cache.slots);""",
"""static void core_scalar_object_reuse_cache_clear(void) {
    free(core_object_address_use_index);
    core_object_address_use_index = NULL;
    free(core_scalar_object_reuse_cache.slots);""")
start=s.index("static bool core_scalar_object_reuse_layout(const MinicC0Program *program,")
end=s.index("static bool core_frame_initialize(",start)
fragment=s[start:end]
old="""            for (object_index = 0U; object_index < function->object_count; ++object_index) {
                core_scalar_object_reuse_cache.slots[object_index] = SIZE_MAX;"""
new="""            /* Building the index costs O(I); avoid it on small Core functions
             * where a few legacy scans are usually cheaper.  Never fail
             * compilation merely because the optional index is unavailable. */
            if (function->object_count >= 8U &&
                function->instruction_count >= 100U) {
                (void)core_object_address_build_use_index(function);
            }
"""+old
if fragment.count(old)!=1:raise SystemExit(f"P14 index prepare anchor count={fragment.count(old)}")
fragment=fragment.replace(old,new,1)
s=s[:start]+fragment+s[end:]
p.write_text(s)
print("CORE_OBJECT_ADDRESS_USE_INDEX_V1=APPLIED")
