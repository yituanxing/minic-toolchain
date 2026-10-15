#!/usr/bin/env python3
"""Experimental opt-in conservative cross-block CoreObject slot reuse.

Only an address-nonescaping <=8-byte scalar whose ALL direct accesses are in
one basic block (the existing Runtime proof) and whose FIRST actual access in
that block DEFINES the value can share with another such object from a
different block. The block may re-execute in a loop; each visit rewrites
before its first read. Objects used across blocks, address escapes, and
READ-first objects keep the existing exclusive stack storage.

This remains an experimental backend transformation until runtime verified.
Apply strictly AFTER the normal Runtime profile and its CFG safety hotfix.
"""
from pathlib import Path

path = Path("src/target/riscv64/core_codegen.c")
s = path.read_text()
if "CORE_OBJECT_SLOT_REUSE_V0" not in s or "IRQ_FRAME_REUSE_V0" not in s:
    raise SystemExit("P1_DEFINED_BEFORE_USE=ERROR missing runtime reuse prerequisites")
# The exact previously QEMU P1-proven rule is integrated into native C.
# Preserve this stage as a strict source-verification entrypoint.
if "P1_DEFINED_BEFORE_USE_CROSSBLOCK_V1" in s:
    expected_counts = {
        "/* P1_DEFINED_BEFORE_USE_CROSSBLOCK_V1:": 1,
        "bool can_share_across_blocks;": 1,
        "static bool core_scalar_object_first_access_defines(": 1,
        # Native perf-indexed fast path adds the same proven first-access
        # recomputation alongside initialization and the original slow scan.
        # Three assignments are required once the indexed path is canonical.
        "interval->can_share_across_blocks =": 3,
        "static bool p1_crossblock_current_function_enabled = true;": 1,
        "p1_crossblock_current_function_enabled &&": 1,
        "strcmp(focus, symbol_name) == 0;": 1,
    }
    if any(s.count(marker) != count for marker, count in expected_counts.items()):
        raise SystemExit("P1_DEFINED_BEFORE_USE=ERROR partial canonical source")
    print("P1_DEFINED_BEFORE_USE_CROSSBLOCK_V1=ALREADY_CANONICAL")
    raise SystemExit(0)

def one(old: str, new: str, label: str) -> None:
    global s
    if s.count(old) != 1:
        raise SystemExit(f"P1_DEFINED_BEFORE_USE=ERROR {label} matches={s.count(old)}")
    s = s.replace(old, new, 1)

one(
    "typedef struct CoreScalarObjectInterval {\n"
    "    bool reusable;\n"
    "    size_t block_index;",
    "typedef struct CoreScalarObjectInterval {\n"
    "    bool reusable;\n"
    "    bool can_share_across_blocks;\n"
    "    size_t block_index;",
    "interval_field",
)
one(
    "    interval->reusable = false;\n"
    "    interval->block_index = SIZE_MAX;",
    "    interval->reusable = false;\n"
    "    interval->can_share_across_blocks = false;\n"
    "    interval->block_index = SIZE_MAX;",
    "interval_initialization",
)
before = "static bool core_scalar_object_interval(const MinicC0Program *program,"
helper = r'''/* P1_DEFINED_BEFORE_USE_CROSSBLOCK_V1: prove the first real read/write
   of a scalar object in its *sole* owner block is a write. Reaching this
   predicate never weakens the existing no-address-escape analysis.
   The IR verifier already requires a Core SSA value to be available only
   within its defining basic block. A direct LOAD/STORE using an
   OBJECT_ADDRESS result therefore identifies a local access. */
static bool core_scalar_object_first_access_defines(
    const MinicCoreFunction *function,
    MinicCoreObjectId object_id, size_t block_index) {
    const MinicCoreBlock *block;
    size_t *first_producer_plus_one;
    size_t position;
    bool first_access_defines = false;

    if (function == NULL || block_index >= function->block_count ||
        function->value_count > SIZE_MAX / sizeof(size_t)) {
        return false;
    }
    block = &function->blocks[block_index];

    /* PERF: the original implementation searched the entire block for
       OBJECT_ADDRESS on EVERY LOAD/STORE of EVERY eligible CoreObject.
       For large, unrolled Linux crypto functions this is quadratic in block
       length per object (lib/crypto/blake2s-generic.c: 33m for one TU).
       Index matching local address producers once, then inspect each
       memory access exactly once. A value id is a dense Core IR index.
       The stored position is 1-based so calloc's 0 means "unseen". */
    first_producer_plus_one = (size_t *)calloc(
        function->value_count != 0U ? function->value_count : 1U,
        sizeof(*first_producer_plus_one));
    if (first_producer_plus_one == NULL) {
        /* Failing to allocate an optimization-only index must never
           license sharing a live object across basic blocks. */
        return false;
    }
    for (position = 0U; position < block->instruction_count; ++position) {
        MinicCoreInstructionId id = block->instructions[position];
        const MinicCoreInstruction *inst;
        MinicCoreValueId result;
        if (id >= function->instruction_count || position == SIZE_MAX - 1U) {
            goto finish;
        }
        inst = &function->instructions[id];
        if (inst->kind != MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS ||
            inst->value.object_id != object_id) {
            continue;
        }
        result = inst->result;
        if (result != MINIC_CORE_VALUE_INVALID &&
            result < function->value_count &&
            first_producer_plus_one[result] == 0U) {
            /* Preserve the original helper's earliest producer semantics
               even if malformed IR repeats the same result id. */
            first_producer_plus_one[result] = position + 1U;
        }
    }
    for (position = 0U; position < block->instruction_count; ++position) {
        MinicCoreInstructionId id = block->instructions[position];
        const MinicCoreInstruction *inst = &function->instructions[id];
        MinicCoreValueId address = MINIC_CORE_VALUE_INVALID;
        if (inst->kind == MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT &&
            inst->value.parameter_object.object_id == object_id) {
            first_access_defines = true;
            break;
        }
        if (inst->kind == MINIC_CORE_INSTRUCTION_LOAD) {
            address = inst->value.load.address;
        } else if (inst->kind == MINIC_CORE_INSTRUCTION_STORE) {
            address = inst->value.store.address;
        } else {
            continue;
        }
        if (address < function->value_count &&
            first_producer_plus_one[address] != 0U) {
            first_access_defines =
                inst->kind == MINIC_CORE_INSTRUCTION_STORE &&
                first_producer_plus_one[address] - 1U < position;
            break;
        }
    }
finish:
    free(first_producer_plus_one);
    return first_access_defines;
}

'''
one(before,helper+before,"helper_position")
end_anchor = (
    "    return true;\n"
    "}\n\n"
    "static bool core_scalar_object_intervals_overlap("
)
end_new = (
    "    if (interval->reusable) {\n"
    "        interval->can_share_across_blocks =\n"
    "            core_scalar_object_first_access_defines(\n"
    "                function, object_id, interval->block_index);\n"
    "    }\n"
    "    return true;\n"
    "}\n\n"
    "static bool core_scalar_object_intervals_overlap("
)
one(end_anchor,end_new,"interval_completion")
one(
    "    if (left->block_index != right->block_index) {\n"
    "        return true;\n"
    "    }\n",
    "    if (left->block_index != right->block_index) {\n"
    "        return !(left->can_share_across_blocks &&\n"
    "                 right->can_share_across_blocks);\n"
    "    }\n",
    "cross_block_interference",
)
# Function scope is mandatory for the next isolated experiment. Default
# preserves the historical all-functions opt-in behaviour.
one(
    "static bool core_scalar_object_intervals_overlap(",
    "static bool p1_crossblock_current_function_enabled = true;\n\n"
    "static bool core_scalar_object_intervals_overlap(",
    "function_select_declaration",
)
one(
    "        return !(left->can_share_across_blocks &&\n"
    "                 right->can_share_across_blocks);",
    "        return !(p1_crossblock_current_function_enabled &&\n"
    "                 left->can_share_across_blocks &&\n"
    "                 right->can_share_across_blocks);",
    "function_select_guard",
)
one(
    "    if (!core_frame_initialize(program, function, &frame)) {",
    "    /* Opt-in P1 test changes only the selected function's frame. */\n"
    "    {\n"
    "        const char *focus = getenv(\"MINIC_P1_CROSSBLOCK_FUNCTION\");\n"
    "        p1_crossblock_current_function_enabled =\n"
    "            focus == NULL || focus[0] == 0 ||\n"
    "            strcmp(focus, symbol_name) == 0;\n"
    "    }\n"
    "    if (!core_frame_initialize(program, function, &frame)) {",
    "function_scope_set_before_frame",
)

path.write_text(s)
print("P1_DEFINED_BEFORE_USE_CROSSBLOCK_V1=APPLIED opt_in=true")
