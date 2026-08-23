from pathlib import Path

path = Path('src/core/core_lower.c')
text = path.read_text()

marker = 'M72_NESTED_BREAK_TARGET'
if marker in text:
    print('M72 nested break target already applied')
    raise SystemExit(0)

context_anchor = '''    MinicCoreFunction *function;
    MinicCoreBlockId block_id;
    MinicCoreObjectId *local_objects;
'''
context_replacement = '''    MinicCoreFunction *function;
    MinicCoreBlockId block_id;
    /* M72_NESTED_BREAK_TARGET: nearest active loop/switch exit for a
       semantic break statement. Kept in lowering context so break nested
       below if/compound blocks still targets the enclosing construct. */
    MinicCoreBlockId break_target;
    MinicCoreObjectId *local_objects;
'''
if text.count(context_anchor) != 1:
    raise SystemExit(f'M72 context anchor count={text.count(context_anchor)}')
text = text.replace(context_anchor, context_replacement, 1)

while_decl_anchor = '''    MinicCoreBlockId body_block;
    MinicCoreBlockId condition_block;
    MinicCoreBlockId exit_block;
    MinicCoreBlockId preheader_block;
'''
while_decl_replacement = '''    MinicCoreBlockId body_block;
    MinicCoreBlockId condition_block;
    MinicCoreBlockId exit_block;
    MinicCoreBlockId preheader_block;
    MinicCoreBlockId saved_break_target;
'''
if text.count(while_decl_anchor) != 1:
    raise SystemExit(f'M72 while decl anchor count={text.count(while_decl_anchor)}')
text = text.replace(while_decl_anchor, while_decl_replacement, 1)

while_body_anchor = '''    context->block_id = body_block;
    status = lower_block(context, iteration_source, &body_terminated);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
'''
while_body_replacement = '''    context->block_id = body_block;
    saved_break_target = context->break_target;
    context->break_target = exit_block;
    status = lower_block(context, iteration_source, &body_terminated);
    context->break_target = saved_break_target;
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
'''
if text.count(while_body_anchor) != 1:
    raise SystemExit(f'M72 while body anchor count={text.count(while_body_anchor)}')
text = text.replace(while_body_anchor, while_body_replacement, 1)

switch_decl_anchor = '''        size_t scan;
        bool segment_terminated;
'''
switch_decl_replacement = '''        size_t scan;
        MinicCoreBlockId saved_break_target;
        bool segment_terminated;
'''
if text.count(switch_decl_anchor) != 1:
    raise SystemExit(f'M72 switch decl anchor count={text.count(switch_decl_anchor)}')
text = text.replace(switch_decl_anchor, switch_decl_replacement, 1)

switch_body_anchor = '''        if (segment.statement_count != 0U) {
            status = lower_block(context, &segment, &segment_terminated);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
        }
'''
switch_body_replacement = '''        if (segment.statement_count != 0U) {
            saved_break_target = context->break_target;
            context->break_target = exit_block;
            status = lower_block(context, &segment, &segment_terminated);
            context->break_target = saved_break_target;
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
        }
'''
if text.count(switch_body_anchor) != 1:
    raise SystemExit(f'M72 switch body anchor count={text.count(switch_body_anchor)}')
text = text.replace(switch_body_anchor, switch_body_replacement, 1)

break_anchor = '''            case MINIC_STATEMENT_RETURN:
                status = lower_return(context, statement);
                statement_terminated = status == MINIC_CORE_LOWER_OK;
                break;
            case MINIC_STATEMENT_IF:
'''
break_replacement = '''            case MINIC_STATEMENT_RETURN:
                status = lower_return(context, statement);
                statement_terminated = status == MINIC_CORE_LOWER_OK;
                break;
            case MINIC_STATEMENT_BREAK:
                if (context->break_target == MINIC_CORE_BLOCK_INVALID) {
                    status = MINIC_CORE_LOWER_UNSUPPORTED;
                    break;
                }
                status = set_branch(
                    context, context->block_id, statement->span, context->break_target);
                statement_terminated = status == MINIC_CORE_LOWER_OK;
                break;
            case MINIC_STATEMENT_IF:
'''
if text.count(break_anchor) != 1:
    raise SystemExit(f'M72 break anchor count={text.count(break_anchor)}')
text = text.replace(break_anchor, break_replacement, 1)

init_anchor = '''    context.function = &lowered;
    context.block_id = block_id;
    context.local_objects = local_objects;
'''
init_replacement = '''    context.function = &lowered;
    context.block_id = block_id;
    context.break_target = MINIC_CORE_BLOCK_INVALID;
    context.local_objects = local_objects;
'''
if text.count(init_anchor) != 1:
    raise SystemExit(f'M72 init anchor count={text.count(init_anchor)}')
text = text.replace(init_anchor, init_replacement, 1)

path.write_text(text)
print('M72 nested break target applied')
