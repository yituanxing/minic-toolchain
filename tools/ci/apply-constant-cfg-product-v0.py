#!/usr/bin/env python3
from pathlib import Path

LOWER = Path("src/core/core_lower.c")
CODEGEN = Path("src/target/riscv64/core_codegen.c")
MARKER = "M177_CONSTANT_SWITCH_CFG_OWNER"


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one anchor, found {count}")
    return text.replace(old, new, 1)


def patch_lower() -> bool:
    text = LOWER.read_text()
    if MARKER in text:
        return False

    function_anchor = (
        "static MinicCoreLowerStatus\n"
        "lower_switch(MinicCoreLowerContext *context, const MinicStatement *statement, bool *terminated) {"
    )
    pos = text.find(function_anchor)
    if pos < 0:
        raise SystemExit("lower_switch anchor not found")
    prefix, tail = text[:pos], text[pos:]
    old = """    default_target = default_label == SIZE_MAX ? exit_block : labels[default_label].body_block;\n    dispatch_target =\n        first_case_label == SIZE_MAX ? default_target : labels[first_case_label].test_block;\n"""
    new = """    default_target = default_label == SIZE_MAX ? exit_block : labels[default_label].body_block;\n    dispatch_target =\n        first_case_label == SIZE_MAX ? default_target : labels[first_case_label].test_block;\n\n    /* M177_CONSTANT_SWITCH_CFG_OWNER: an integer constant-expression selector\n       has exactly one switch-entry edge. Keep case-segment fallthrough intact,\n       but bypass the synthetic comparison chain when every case is a simple\n       scalar case. Range cases stay on the established generic dispatch path. */\n    {\n        MinicConstValue selector_constant;\n        MinicConstValue selector_converted;\n\n        if (minic_const_eval_integer(context->body->program,\n                                     context->target,\n                                     statement->expression,\n                                     &selector_constant) &&\n            minic_const_value_convert_integer(context->body->program,\n                                              context->target,\n                                              &selector_constant,\n                                              selector_type,\n                                              &selector_converted)) {\n            size_t constant_target = SIZE_MAX;\n            size_t dispatch_index;\n            bool simple_cases = true;\n\n            for (dispatch_index = 0U; dispatch_index < label_count; ++dispatch_index) {\n                const MinicStatement *case_statement = labels[dispatch_index].statement;\n                MinicConstValue case_constant;\n                MinicConstValue case_converted;\n\n                if (case_statement->kind != MINIC_STATEMENT_CASE) {\n                    continue;\n                }\n                if (case_statement->target_expression != MINIC_EXPRESSION_INVALID ||\n                    !minic_const_eval_integer(context->body->program,\n                                              context->target,\n                                              case_statement->expression,\n                                              &case_constant) ||\n                    !minic_const_value_convert_integer(context->body->program,\n                                                       context->target,\n                                                       &case_constant,\n                                                       selector_type,\n                                                       &case_converted)) {\n                    simple_cases = false;\n                    break;\n                }\n                if (constant_target == SIZE_MAX &&\n                    case_converted.bits == selector_converted.bits) {\n                    constant_target = dispatch_index;\n                }\n            }\n            if (simple_cases) {\n                dispatch_target = constant_target == SIZE_MAX\n                                      ? default_target\n                                      : labels[constant_target].body_block;\n            }\n        }\n    }\n"""
    tail = replace_once(tail, old, new, "flat switch dispatch")
    LOWER.write_text(prefix + tail)
    return True


def patch_codegen() -> bool:
    text = CODEGEN.read_text()
    if MARKER in text:
        return False

    text = replace_once(
        text,
        """    size_t *object_offsets;\n    size_t *value_offsets;\n    size_t value_base_offset;\n""",
        """    size_t *object_offsets;\n    size_t *value_offsets;\n    /* M177_CONSTANT_SWITCH_CFG_OWNER: suppress target emission of Core blocks\n       that have no executable predecessor after semantic CFG pruning. */\n    bool *reachable_blocks;\n    size_t value_base_offset;\n""",
        "frame reachable field",
    )
    text = replace_once(
        text,
        """    frame->object_offsets = NULL;\n    frame->value_offsets = NULL;\n    frame->value_slot_count = 0U;\n""",
        """    frame->object_offsets = NULL;\n    frame->value_offsets = NULL;\n    frame->reachable_blocks = NULL;\n    frame->value_slot_count = 0U;\n""",
        "frame init",
    )
    text = replace_once(
        text,
        """    free(frame->value_offsets);\n    frame->value_offsets = NULL;\n    free(frame->object_offsets);\n    frame->object_offsets = NULL;\n""",
        """    free(frame->value_offsets);\n    frame->value_offsets = NULL;\n    free(frame->object_offsets);\n    frame->object_offsets = NULL;\n    free(frame->reachable_blocks);\n    frame->reachable_blocks = NULL;\n""",
        "frame destroy",
    )

    emit_anchor = """static bool emit_core_function_with_symbol(FILE *file,\n                                                    const MinicC0Program *program,\n                                                    const MinicCoreFunction *function,\n                                                    const MinicRiscv64FunctionSymbol *symbol) {\n"""
    helper = r'''/* M177_CONSTANT_SWITCH_CFG_OWNER: user C labels are valid re-entry
   roots for goto/asm-goto/computed-goto, so seed them conservatively alongside
   the function entry and close over explicit Core terminator edges. Synthetic
   switch blocks with no executable predecessor remain unmarked. */
static bool core_frame_mark_reachable_blocks(const MinicCoreFunction *function,
                                             MinicRiscv64CoreFrame *frame) {
    MinicCoreBlockId *queue;
    size_t queue_begin;
    size_t queue_end;
    size_t block_index;

    if (function == NULL || frame == NULL || function->block_count == 0U ||
        function->entry_block >= function->block_count ||
        function->block_count > SIZE_MAX / sizeof(*queue)) {
        return false;
    }
    frame->reachable_blocks =
        (bool *)calloc(function->block_count, sizeof(*frame->reachable_blocks));
    queue = (MinicCoreBlockId *)malloc(function->block_count * sizeof(*queue));
    if (frame->reachable_blocks == NULL || queue == NULL) {
        free(queue);
        return false;
    }
    queue_begin = 0U;
    queue_end = 0U;
#define MINIC_CORE_REACHABLE_ENQUEUE(block_id_)                                      \
    do {                                                                             \
        MinicCoreBlockId reach_id_ = (block_id_);                                    \
        if (reach_id_ >= function->block_count) {                                    \
            free(queue);                                                              \
            return false;                                                             \
        }                                                                             \
        if (!frame->reachable_blocks[reach_id_]) {                                   \
            frame->reachable_blocks[reach_id_] = true;                               \
            queue[queue_end++] = reach_id_;                                           \
        }                                                                             \
    } while (0)

    MINIC_CORE_REACHABLE_ENQUEUE(function->entry_block);
    for (block_index = 0U; block_index < function->block_count; ++block_index) {
        if (function->blocks[block_index].source_label_id != SIZE_MAX) {
            MINIC_CORE_REACHABLE_ENQUEUE((MinicCoreBlockId)block_index);
        }
    }
    while (queue_begin < queue_end) {
        const MinicCoreBlock *block = &function->blocks[queue[queue_begin++]];

        if (!block->has_terminator) {
            free(queue);
            return false;
        }
        switch (block->terminator.kind) {
        case MINIC_CORE_TERMINATOR_RETURN:
        case MINIC_CORE_TERMINATOR_UNREACHABLE:
        case MINIC_CORE_TERMINATOR_INDIRECT_BRANCH:
            break;
        case MINIC_CORE_TERMINATOR_BRANCH:
            MINIC_CORE_REACHABLE_ENQUEUE(block->terminator.branch_target);
            break;
        case MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH:
            MINIC_CORE_REACHABLE_ENQUEUE(block->terminator.conditional.when_true);
            MINIC_CORE_REACHABLE_ENQUEUE(block->terminator.conditional.when_false);
            break;
        default:
            free(queue);
            return false;
        }
    }
#undef MINIC_CORE_REACHABLE_ENQUEUE
    free(queue);
    return true;
}

'''
    text = replace_once(text, emit_anchor, helper + emit_anchor, "emit helper anchor")
    text = replace_once(
        text,
        """    if (!core_frame_initialize(program, function, &frame)) {\n        return false;\n    }\n""",
        """    if (!core_frame_initialize(program, function, &frame)) {\n        return false;\n    }\n    if (!core_frame_mark_reachable_blocks(function, &frame)) {\n        return core_frame_fail(&frame);\n    }\n""",
        "frame reachability call",
    )
    text = replace_once(
        text,
        """        block = &function->blocks[block_index];\n        if (!emit_block_label(file, symbol_name, (MinicCoreBlockId)block_index)) {\n""",
        """        block = &function->blocks[block_index];\n        if (frame.reachable_blocks != NULL && !frame.reachable_blocks[block_index]) {\n            continue;\n        }\n        if (!emit_block_label(file, symbol_name, (MinicCoreBlockId)block_index)) {\n""",
        "block emission filter",
    )
    CODEGEN.write_text(text)
    return True


changed_lower = patch_lower()
changed_codegen = patch_codegen()
if changed_lower != changed_codegen:
    raise SystemExit("partial M177 product state detected")
print("M177_PATCH_APPLIED=1" if changed_lower else "M177_PATCH_APPLIED=0")
