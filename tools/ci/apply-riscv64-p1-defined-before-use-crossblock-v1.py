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
if "P1_DEFINED_BEFORE_USE_CROSSBLOCK_V1" in s:
    raise SystemExit("P1_DEFINED_BEFORE_USE=ERROR duplicate patch")

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
    size_t position;

    if (function == NULL || block_index >= function->block_count) {
        return false;
    }
    block = &function->blocks[block_index];
    for (position = 0U; position < block->instruction_count; ++position) {
        MinicCoreInstructionId instruction_id = block->instructions[position];
        const MinicCoreInstruction *inst;
        MinicCoreValueId address = MINIC_CORE_VALUE_INVALID;
        size_t producer_position;
        if (instruction_id >= function->instruction_count) {
            return false;
        }
        inst = &function->instructions[instruction_id];
        if (inst->kind == MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT &&
            inst->value.parameter_object.object_id == object_id) {
            return true;
        }
        if (inst->kind == MINIC_CORE_INSTRUCTION_LOAD) {
            address = inst->value.load.address;
        } else if (inst->kind == MINIC_CORE_INSTRUCTION_STORE) {
            address = inst->value.store.address;
        } else {
            continue;
        }
        for (producer_position = 0U;
             producer_position < block->instruction_count; ++producer_position) {
            MinicCoreInstructionId producer_id =
                block->instructions[producer_position];
            const MinicCoreInstruction *producer;
            if (producer_id >= function->instruction_count) {
                return false;
            }
            producer = &function->instructions[producer_id];
            if (producer->kind == MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS &&
                producer->value.object_id == object_id &&
                producer->result == address) {
                /* The first observation is a direct local memory access.
                   Fail closed when it reads an older value, which could
                   remain live across predecessors or loop backedges. */
                return inst->kind == MINIC_CORE_INSTRUCTION_STORE &&
                       producer_position < position;
            }
        }
    }
    return false;
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
    "            focus == NULL || focus[0] == '\0' ||\n"
    "            strcmp(focus, symbol_name) == 0;\n"
    "    }\n"
    "    if (!core_frame_initialize(program, function, &frame)) {",
    "function_scope_set_before_frame",
)

path.write_text(s)
print("P1_DEFINED_BEFORE_USE_CROSSBLOCK_V1=APPLIED opt_in=true")
