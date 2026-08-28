#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"M45 {label}: expected one anchor, found {count}")
    p.write_text(text.replace(old, new, 1))


# Core lowering: break is a control-flow edge to the nearest breakable scope.
# Keep the target in lowering context so nested loops/switches naturally save/restore it.
replace_once(
    "src/core/core_lower.c",
    """    MinicCoreFunction *function;
    MinicCoreBlockId block_id;
    MinicCoreObjectId *local_objects;
    MinicCoreBlockId *label_blocks;
    size_t label_block_count;
} MinicCoreLowerContext;
""",
    """    MinicCoreFunction *function;
    MinicCoreBlockId block_id;
    MinicCoreBlockId break_target;
    MinicCoreObjectId *local_objects;
    MinicCoreBlockId *label_blocks;
    size_t label_block_count;
} MinicCoreLowerContext;
""",
    "break target context field",
)

replace_once(
    "src/core/core_lower.c",
    """    MinicCoreBlockId body_block;
    MinicCoreBlockId condition_block;
    MinicCoreBlockId exit_block;
    MinicCoreBlockId preheader_block;
    MinicCoreTerminator terminator;
""",
    """    MinicCoreBlockId body_block;
    MinicCoreBlockId condition_block;
    MinicCoreBlockId exit_block;
    MinicCoreBlockId preheader_block;
    MinicCoreBlockId previous_break_target;
    MinicCoreTerminator terminator;
""",
    "while break target local",
)

replace_once(
    "src/core/core_lower.c",
    """    context->block_id = body_block;
    status = lower_block(context, iteration_source, &body_terminated);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
""",
    """    context->block_id = body_block;
    previous_break_target = context->break_target;
    context->break_target = exit_block;
    status = lower_block(context, iteration_source, &body_terminated);
    context->break_target = previous_break_target;
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
""",
    "while break target scope",
)

replace_once(
    "src/core/core_lower.c",
    """    MinicCoreBlockId default_target;
    MinicCoreBlockId dispatch_target;
    MinicCoreBlockId exit_block;
    MinicCoreObjectId selector_object;
""",
    """    MinicCoreBlockId default_target;
    MinicCoreBlockId dispatch_target;
    MinicCoreBlockId exit_block;
    MinicCoreBlockId previous_break_target;
    MinicCoreObjectId selector_object;
""",
    "switch break target local",
)

replace_once(
    "src/core/core_lower.c",
    """    for (source_index = 0U; source_index < label_count; ++source_index) {
        MinicBlock segment;
        MinicCoreBlockId fallthrough_target;
""",
    """    previous_break_target = context->break_target;
    context->break_target = exit_block;
    for (source_index = 0U; source_index < label_count; ++source_index) {
        MinicBlock segment;
        MinicCoreBlockId fallthrough_target;
""",
    "switch break target enter",
)

replace_once(
    "src/core/core_lower.c",
    """    }

    context->block_id = exit_block;
    *terminated = false;
    return MINIC_CORE_LOWER_OK;
}

static MinicCoreLowerStatus
lower_block""",
    """    }
    context->break_target = previous_break_target;

    context->block_id = exit_block;
    *terminated = false;
    return MINIC_CORE_LOWER_OK;
}

static MinicCoreLowerStatus
lower_block""",
    "switch break target restore",
)

replace_once(
    "src/core/core_lower.c",
    """            case MINIC_STATEMENT_INLINE_ASM:
                status = lower_opaque_inline_asm(context, statement);
                break;
            case MINIC_STATEMENT_RETURN:
""",
    """            case MINIC_STATEMENT_INLINE_ASM:
                status = lower_opaque_inline_asm(context, statement);
                break;
            case MINIC_STATEMENT_BREAK:
                if (context->break_target == MINIC_CORE_BLOCK_INVALID) {
                    return MINIC_CORE_LOWER_UNSUPPORTED;
                }
                status =
                    set_branch(context, context->block_id, statement->span, context->break_target);
                statement_terminated = status == MINIC_CORE_LOWER_OK;
                break;
            case MINIC_STATEMENT_RETURN:
""",
    "generic break lowering",
)

replace_once(
    "src/core/core_lower.c",
    """    context.function = &lowered;
    context.block_id = block_id;
    context.local_objects = local_objects;
""",
    """    context.function = &lowered;
    context.block_id = block_id;
    context.break_target = MINIC_CORE_BLOCK_INVALID;
    context.local_objects = local_objects;
""",
    "initialize break target",
)

print("M45_PATCH_APPLIED")
