#!/usr/bin/env python3
"""Use one statement-ID map per translation unit during Core lowering.

The map is initialized once. Each function records only the statement IDs it
writes and clears precisely those entries before the next function. The public
single-function API retains its original allocation semantics.
"""
from pathlib import Path


def patch_once(path, edits):
    p = Path(path)
    text = p.read_text()
    for old, new in edits:
        n = text.count(old)
        if n != 1:
            raise SystemExit(f"{path}: expected one anchor, found {n}: {old[:100]!r}")
        text = text.replace(old, new, 1)
    p.write_text(text)


h = "src/core/core_lower.h"
patch_once(h, [(
    "MinicCoreLowerStatus minic_core_lower_function(const MinicFunctionBodyView *body,\n",
    """typedef struct MinicCoreLowerWorkspace {
    MinicCoreBlockId *statement_blocks;
    MinicStatementId *touched_statements;
    size_t statement_count;
    size_t touched_count;
} MinicCoreLowerWorkspace;

bool minic_core_lower_workspace_initialize(MinicCoreLowerWorkspace *workspace,
                                           size_t statement_count);
void minic_core_lower_workspace_destroy(MinicCoreLowerWorkspace *workspace);
MinicCoreLowerStatus minic_core_lower_function_with_workspace(
    const MinicFunctionBodyView *body,
    const MinicTargetInfo *target,
    MinicCoreFunction *output,
    MinicCoreLowerWorkspace *workspace);

MinicCoreLowerStatus minic_core_lower_function(const MinicFunctionBodyView *body,
"""
)])

internal = "src/core/core_lower_internal.h"
patch_once(internal, [(
    "    MinicCoreBlockId *statement_blocks;\n    size_t statement_block_count;\n",
    "    MinicCoreBlockId *statement_blocks;\n    size_t statement_block_count;\n    MinicCoreLowerWorkspace *workspace;\n"
)])

source = "src/core/core_lower.c"
helpers = """bool minic_core_lower_workspace_initialize(MinicCoreLowerWorkspace *workspace,
                                           size_t statement_count) {
    size_t index;
    if (workspace == NULL ||
        statement_count > SIZE_MAX / sizeof(*workspace->statement_blocks) ||
        statement_count > SIZE_MAX / sizeof(*workspace->touched_statements)) {
        return false;
    }
    (void)memset(workspace, 0, sizeof(*workspace));
    workspace->statement_count = statement_count;
    if (statement_count == 0U) {
        return true;
    }
    workspace->statement_blocks =
        (MinicCoreBlockId *)malloc(statement_count * sizeof(*workspace->statement_blocks));
    workspace->touched_statements =
        (MinicStatementId *)malloc(statement_count * sizeof(*workspace->touched_statements));
    if (workspace->statement_blocks == NULL || workspace->touched_statements == NULL) {
        minic_core_lower_workspace_destroy(workspace);
        return false;
    }
    for (index = 0U; index < statement_count; ++index) {
        workspace->statement_blocks[index] = MINIC_CORE_BLOCK_INVALID;
    }
    return true;
}

void minic_core_lower_workspace_destroy(MinicCoreLowerWorkspace *workspace) {
    if (workspace == NULL) {
        return;
    }
    free(workspace->statement_blocks);
    free(workspace->touched_statements);
    (void)memset(workspace, 0, sizeof(*workspace));
}

static void minic_core_lower_workspace_reset(MinicCoreLowerWorkspace *workspace) {
    size_t index;
    if (workspace == NULL) {
        return;
    }
    for (index = 0U; index < workspace->touched_count; ++index) {
        workspace->statement_blocks[workspace->touched_statements[index]] =
            MINIC_CORE_BLOCK_INVALID;
    }
    workspace->touched_count = 0U;
}

static bool core_store_statement_block(MinicCoreLowerContext *context,
                                      MinicStatementId statement_id,
                                      MinicCoreBlockId block_id) {
    if (context == NULL || context->statement_blocks == NULL ||
        statement_id >= context->statement_block_count ||
        block_id == MINIC_CORE_BLOCK_INVALID ||
        context->statement_blocks[statement_id] != MINIC_CORE_BLOCK_INVALID) {
        return false;
    }
    if (context->workspace != NULL) {
        MinicCoreLowerWorkspace *workspace = context->workspace;
        if (workspace->touched_count >= workspace->statement_count) {
            return false;
        }
        workspace->touched_statements[workspace->touched_count++] = statement_id;
    }
    context->statement_blocks[statement_id] = block_id;
    return true;
}

"""
patch_once(source, [
    ("MinicCoreLowerStatus ensure_statement_block(MinicCoreLowerContext *context,",
     helpers + "MinicCoreLowerStatus ensure_statement_block(MinicCoreLowerContext *context,"),
    ("        context->statement_blocks[statement_id] = mapped;\n",
     """        if (!core_store_statement_block(context, statement_id, mapped)) {
            return MINIC_CORE_LOWER_ERROR;
        }
"""),
    ("            context->statement_blocks[normalized_do_while_continue] = single_iteration_exit;\n",
     """            if (!core_store_statement_block(
                    context, normalized_do_while_continue, single_iteration_exit)) {
                return MINIC_CORE_LOWER_ERROR;
            }
"""),
    ("        context->statement_blocks[continue_label_statement] =\n            for_update != NULL ? update_block : condition_block;\n",
     """        if (!core_store_statement_block(
                context, continue_label_statement,
                for_update != NULL ? update_block : condition_block)) {
            return MINIC_CORE_LOWER_ERROR;
        }
"""),
    ("MinicCoreLowerStatus minic_core_lower_function(const MinicFunctionBodyView *body,\n                                               const MinicTargetInfo *target,\n                                               MinicCoreFunction *output) {",
     """MinicCoreLowerStatus minic_core_lower_function_with_workspace(
    const MinicFunctionBodyView *body,
    const MinicTargetInfo *target,
    MinicCoreFunction *output,
    MinicCoreLowerWorkspace *workspace) {"""),
    ("""    if (body->program->statement_count > SIZE_MAX / sizeof(*statement_blocks)) { free(local_objects); return MINIC_CORE_LOWER_ERROR; }
    statement_blocks = body->program->statement_count == 0U ? NULL : (MinicCoreBlockId *)malloc(body->program->statement_count * sizeof(*statement_blocks));
    if (body->program->statement_count != 0U && statement_blocks == NULL) { free(local_objects); return MINIC_CORE_LOWER_ERROR; }
    for (statement_index = 0U; statement_index < body->program->statement_count; ++statement_index) statement_blocks[statement_index] = MINIC_CORE_BLOCK_INVALID;
""",
     """    if (workspace != NULL) {
        if (workspace->statement_count != body->program->statement_count ||
            workspace->touched_count != 0U ||
            (workspace->statement_count != 0U &&
             (workspace->statement_blocks == NULL || workspace->touched_statements == NULL))) {
            free(local_objects);
            return MINIC_CORE_LOWER_ERROR;
        }
        statement_blocks = workspace->statement_blocks;
    } else {
        if (body->program->statement_count > SIZE_MAX / sizeof(*statement_blocks)) {
            free(local_objects);
            return MINIC_CORE_LOWER_ERROR;
        }
        statement_blocks = body->program->statement_count == 0U
                               ? NULL
                               : (MinicCoreBlockId *)malloc(
                                     body->program->statement_count * sizeof(*statement_blocks));
        if (body->program->statement_count != 0U && statement_blocks == NULL) {
            free(local_objects);
            return MINIC_CORE_LOWER_ERROR;
        }
        for (statement_index = 0U; statement_index < body->program->statement_count;
             ++statement_index) {
            statement_blocks[statement_index] = MINIC_CORE_BLOCK_INVALID;
        }
    }
"""),
    ("        free(statement_blocks); free(local_objects); minic_core_function_destroy(&lowered); return MINIC_CORE_LOWER_ERROR;\n",
     """        if (workspace == NULL) {
            free(statement_blocks);
        }
        free(local_objects);
        minic_core_function_destroy(&lowered);
        return MINIC_CORE_LOWER_ERROR;
"""),
    ("    context.statement_block_count = body->program->statement_count;\n",
     "    context.statement_block_count = body->program->statement_count;\n    context.workspace = workspace;\n"),
    ("    free(statement_blocks); free(local_objects);\n",
     """    if (workspace != NULL) {
        minic_core_lower_workspace_reset(workspace);
    } else {
        free(statement_blocks);
    }
    free(local_objects);
"""),
])
p = Path(source)
text = p.read_text()
text += """
/* Preserve the stable standalone Core-lowering entry point for other callers. */
MinicCoreLowerStatus minic_core_lower_function(const MinicFunctionBodyView *body,
                                               const MinicTargetInfo *target,
                                               MinicCoreFunction *output) {
    return minic_core_lower_function_with_workspace(body, target, output, NULL);
}
"""
p.write_text(text)

compiler = "src/compiler/compiler.c"
patch_once(compiler, [
    ("""    for (function_index = 0U; function_index < set.function_count; ++function_index) {
        const MinicFunction *function;
        MinicFunctionBodyView body;
""",
     """    MinicCoreLowerWorkspace lowering_workspace;
    if (!minic_core_lower_workspace_initialize(
            &lowering_workspace, program->statement_count)) {
        minic_core_function_set_destroy(&set);
        return false;
    }
    for (function_index = 0U; function_index < set.function_count; ++function_index) {
        const MinicFunction *function;
        MinicFunctionBodyView body;
"""),
    ("            minic_core_lower_function(&body, target, &set.functions[function_index]);\n",
     """            minic_core_lower_function_with_workspace(
                &body, target, &set.functions[function_index], &lowering_workspace);
"""),
    ("    minic_core_function_set_destroy(output);\n    *output = set;\n",
     "    minic_core_lower_workspace_destroy(&lowering_workspace);\n    minic_core_function_set_destroy(output);\n    *output = set;\n"),
])
print("MINIC_PERF_CORE_LOWER_STATEMENT_WORKSPACE_V1=APPLIED")
