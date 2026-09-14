#!/usr/bin/env python3
from pathlib import Path

p = Path("src/compiler/compiler.c")
text = p.read_text()

anchor = '''static bool minic_prune_inline_specializations_from_core(\n'''
helper = r'''
static bool minic_inline_source_has_noncore_root(
    const MinicC0Program *program, MinicFunctionId source_id) {
    size_t function_index;
    size_t object_index;

    if (program == NULL || source_id >= program->function_count) {
        return true;
    }
    if (program->entry_function == source_id || program->functions[source_id].force_emit) {
        return true;
    }
    for (function_index = 0U; function_index < program->function_count; ++function_index) {
        if (program->functions[function_index].alias_target == source_id) {
            return true;
        }
    }
    for (object_index = 0U; object_index < program->global_object_count; ++object_index) {
        const MinicGlobalObject *object = &program->global_objects[object_index];
        size_t relocation_index;
        for (relocation_index = 0U; relocation_index < object->relocation_count;
             ++relocation_index) {
            const MinicGlobalRelocation *relocation = &object->relocations[relocation_index];
            if (relocation->target_kind == MINIC_GLOBAL_RELOCATION_FUNCTION &&
                (MinicFunctionId)relocation->target_id == source_id) {
                return true;
            }
        }
    }
    return false;
}

static bool minic_inline_source_is_core_reachable_only(
    const MinicC0Program *program, MinicFunctionId source_id) {
    const MinicFunction *source;

    if (program == NULL || source_id >= program->function_count) {
        return false;
    }
    source = &program->functions[source_id];
    /* Internal function bodies are implementation details, not emission roots.
     * Parser-level is_referenced can stay set after every executable call was
     * rewritten, specialized, or folded away.  Let the lowered Core graph pull
     * the body back in when a reachable CALL or FUNCTION_ADDRESS still needs
     * it; preserve only entry/force-emit/alias/global-relocation roots that live
     * outside that graph.  This applies to ordinary static functions as well as
     * static inline helpers so dead CONFIG-disabled helper subgraphs disappear. */
    return source->is_defined && source->is_internal &&
           !minic_inline_source_has_noncore_root(program, source_id);
}

'''
if text.count(anchor) != 1:
    raise SystemExit("expected one Core specialization-prune anchor")
text = text.replace(anchor, helper + anchor, 1)

old_root = '''        if (function->is_integer_specialization || !function->is_defined ||
            (function->is_internal && !function->is_referenced)) {
            continue;
        }
        reachable[function_index] = true;
        queue[queue_count++] = function_index;
'''
new_root = '''        if (function->is_integer_specialization || !function->is_defined ||
            (function->is_internal && !function->is_referenced)) {
            continue;
        }
        /* Parser-level references are only a discovery hint for internal
         * functions.  The final lowered Core graph is the authoritative
         * executable reachability source; non-Core roots stay conservative. */
        if (minic_inline_source_is_core_reachable_only(
                program, (MinicFunctionId)function_index)) {
            continue;
        }
        reachable[function_index] = true;
        queue[queue_count++] = function_index;
'''
if text.count(old_root) != 1:
    raise SystemExit("expected one Core root loop")
text = text.replace(old_root, new_root, 1)

old_entry = '''    if (block_reachable == NULL || block_queue == NULL ||
        !minic_enqueue_core_block(core->entry_block,
                                 core->block_count,
                                 block_reachable,
                                 block_queue,
                                 &block_queue_count)) {
        goto done;
    }

    while (block_queue_cursor < block_queue_count) {
'''
new_entry = '''    if (block_reachable == NULL || block_queue == NULL ||
        !minic_enqueue_core_block(core->entry_block,
                                 core->block_count,
                                 block_reachable,
                                 block_queue,
                                 &block_queue_count)) {
        goto done;
    }
    /* M180_CORE_LABEL_REENTRY_ROOTS: a source C label is a legal re-entry root
     * for goto/asm-goto. Backend emission preserves such blocks, so final Core
     * reachability must start from them as well or it can prune a specialization
     * clone that is still called after label re-entry. */
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
if text.count(old_entry) != 1:
    raise SystemExit("expected one Core entry-root block")
text = text.replace(old_entry, new_entry, 1)

old_final = '''    for (function_index = 0U; function_index < program->function_count; ++function_index) {
        MinicFunction *function = &program->functions[function_index];
        if (!function->is_integer_specialization) {
            continue;
        }
        function->is_referenced = reachable[function_index];
'''
new_final = '''    for (function_index = 0U; function_index < program->function_count; ++function_index) {
        MinicFunction *function = &program->functions[function_index];
        if (!function->is_integer_specialization) {
            if (minic_inline_source_is_core_reachable_only(
                    program, (MinicFunctionId)function_index)) {
                function->is_referenced = reachable[function_index];
            }
            continue;
        }
        function->is_referenced = reachable[function_index];
'''
if text.count(old_final) != 1:
    raise SystemExit("expected one Core final reachability loop")
text = text.replace(old_final, new_final, 1)

p.write_text(text)
print("MINIC_INLINE_SPECIALIZATION_ORIGINAL_REACHABILITY_V0=APPLIED all_internal=1 label_roots=1")
