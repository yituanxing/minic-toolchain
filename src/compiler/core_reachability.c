#include "compiler/core_reachability.h"
#include "compiler/inline_specialize_internal.h"
#include "core/core_ir.h"
#include "frontend/ast.h"
#include "frontend/ast_traversal.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* One immutable symbol index for the Core function/object closure.  A Core
 * GLOBAL_ADDRESS is a symbol name, not a frontend object ID, so resolve by
 * the same stable linker-visible name on every target. */
typedef struct MinicStaticObjectSymbol {
    const char *name;
    size_t name_length;
    MinicGlobalObjectId object_id;
} MinicStaticObjectSymbol;

typedef struct MinicStaticObjectGraph {
    const MinicStaticObjectSymbol *symbols;
    size_t symbol_count;
    bool *reachable;
    MinicGlobalObjectId *queue;
    size_t queue_count;
} MinicStaticObjectGraph;

typedef struct MinicAsmObjectRootWalk {
    MinicC0Program *program;
    MinicStaticObjectGraph *objects;
    unsigned int depth;
} MinicAsmObjectRootWalk;

static int minic_static_object_symbol_compare(const void *left_opaque,
                                              const void *right_opaque) {
    const MinicStaticObjectSymbol *left = left_opaque;
    const MinicStaticObjectSymbol *right = right_opaque;
    if (left->name_length < right->name_length) return -1;
    if (left->name_length > right->name_length) return 1;
    return memcmp(left->name, right->name, left->name_length);
}

static bool minic_static_object_enqueue(MinicStaticObjectGraph *graph,
                                        MinicGlobalObjectId object_id) {
    if (graph == NULL || object_id >= graph->symbol_count) return false;
    if (graph->reachable[object_id]) return true;
    if (graph->queue_count >= graph->symbol_count) return false;
    graph->reachable[object_id] = true;
    graph->queue[graph->queue_count++] = object_id;
    return true;
}

static bool minic_static_object_enqueue_named(MinicStaticObjectGraph *graph,
                                              const char *name, size_t name_length) {
    size_t begin = 0U;
    size_t end;
    if (graph == NULL || name == NULL) return false;
    end = graph->symbol_count;
    while (begin < end) {
        size_t middle = begin + (end - begin) / 2U;
        const MinicStaticObjectSymbol *symbol = &graph->symbols[middle];
        int comparison;
        if (name_length < symbol->name_length) comparison = -1;
        else if (name_length > symbol->name_length) comparison = 1;
        else comparison = memcmp(name, symbol->name, name_length);
        if (comparison < 0) end = middle;
        else if (comparison > 0) begin = middle + 1U;
        else return minic_static_object_enqueue(graph, symbol->object_id);
    }
    /* An external or target-synthesized symbol has no Program object edge. */
    return true;
}

static bool minic_static_asm_operand_visit(MinicExpressionId *id_opaque,
                                           void *context_opaque) {
    MinicAsmObjectRootWalk *walk = (MinicAsmObjectRootWalk *)context_opaque;
    MinicExpression *expression;
    bool result;

    /* Source GNU asm operands may encode a static address entirely as an
     * immediate `%c0`/`%0`, without a Core GLOBAL_ADDRESS instruction. The
     * shared AST child walker is the sole owner of nested expression edges;
     * do not try to parse target assembly spelling here. All source asm
     * operand trees are conservative roots (including unreachable asm). */
    if (walk == NULL || id_opaque == NULL || walk->program == NULL ||
        walk->objects == NULL || *id_opaque >= walk->program->expression_count ||
        walk->depth >= 64U) return false;
    expression = &walk->program->expressions[*id_opaque];
    if (expression->kind == MINIC_EXPRESSION_GLOBAL_OBJECT) {
        return minic_static_object_enqueue(walk->objects,
                                            expression->value.global_object_id);
    }
    /* A compound literal or statement-expression can hide global references
     * in initializer/statement blocks, outside ordinary child expressions.
     * Unsupported shapes fail closed and retain all original objects. */
    if (expression->kind == MINIC_EXPRESSION_COMPOUND_LITERAL ||
        expression->kind == MINIC_EXPRESSION_STATEMENT) return false;
    walk->depth++;
    result = minic_c0_expression_visit_child_id_refs(
        expression, minic_static_asm_operand_visit, walk);
    walk->depth--;
    return result;
}

/* Preserve original CFG-root and specialization pruning behavior,
 * including non-Core relocation roots and allocation-failure fallback. */
static MinicFunctionId minic_core_named_function_id(
    const MinicC0Program *program, const char *name, size_t name_length) {
    size_t function_index;

    if (program == NULL || name == NULL || name_length == 0U) {
        return MINIC_FUNCTION_INVALID;
    }
    for (function_index = 0U; function_index < program->function_count; ++function_index) {
        const MinicFunction *function = &program->functions[function_index];
        const char *symbol_name = minic_c0_function_symbol_name(function);
        size_t symbol_name_length;

        if (symbol_name == NULL) {
            continue;
        }
        symbol_name_length = function->assembler_name != NULL
                                 ? function->assembler_name_length
                                 : function->name_length;
        if (symbol_name_length == name_length &&
            memcmp(symbol_name, name, name_length) == 0) {
            return function_index;
        }
    }
    return MINIC_FUNCTION_INVALID;
}

static bool minic_enqueue_core_function(
    const MinicC0Program *program, MinicFunctionId target,
    bool *reachable,
    MinicFunctionId *queue,
    size_t *queue_count) {
    if (program == NULL || reachable == NULL || queue == NULL || queue_count == NULL) {
        return false;
    }
    if (target == MINIC_FUNCTION_INVALID || target >= program->function_count ||
        !program->functions[target].is_defined || reachable[target]) {
        return true;
    }
    if (*queue_count >= program->function_count) {
        return false;
    }
    reachable[target] = true;
    queue[(*queue_count)++] = target;
    return true;
}

static bool minic_enqueue_core_named_function(
    const MinicC0Program *program, const char *name, size_t name_length,
    bool *reachable, MinicFunctionId *queue, size_t *queue_count) {
    return minic_enqueue_core_function(
        program, minic_core_named_function_id(program, name, name_length),
        reachable, queue, queue_count);
}

static bool minic_enqueue_core_block(MinicCoreBlockId block_id,
                                     size_t block_count,
                                     bool *reachable,
                                     MinicCoreBlockId *queue,
                                     size_t *queue_count) {
    if (reachable == NULL || queue == NULL || queue_count == NULL ||
        block_id >= block_count) {
        return false;
    }
    if (reachable[block_id]) {
        return true;
    }
    if (*queue_count >= block_count) {
        return false;
    }
    reachable[block_id] = true;
    queue[(*queue_count)++] = block_id;
    return true;
}

static bool minic_walk_reachable_core_edges(const MinicC0Program *program,
                                            const MinicCoreFunction *core,
                                            bool *function_reachable,
                                            MinicFunctionId *function_queue,
                                            size_t *function_queue_count,
                                            bool emit_reachable_core_blocks_only,
                                            MinicStaticObjectGraph *objects) {
    bool *block_reachable = NULL;
    MinicCoreBlockId *block_queue = NULL;
    size_t block_queue_count = 0U;
    size_t block_queue_cursor = 0U;
    bool success = false;

    if (program == NULL || core == NULL || function_reachable == NULL ||
        function_queue == NULL || function_queue_count == NULL) {
        return false;
    }
    if (core->block_count == 0U) {
        return true;
    }
    if (core->blocks == NULL ||
        (core->instruction_count != 0U && core->instructions == NULL) ||
        core->entry_block >= core->block_count) {
        return false;
    }
    block_reachable = (bool *)calloc(core->block_count, sizeof(*block_reachable));
    block_queue = (MinicCoreBlockId *)malloc(core->block_count * sizeof(*block_queue));
    if (block_reachable == NULL || block_queue == NULL) {
        goto done;
    }
    /* Match the selected target's actual ELF reference universe. The two
     * RISC-V emitters filter Core blocks with the shared CFG root algorithm,
     * including source labels. Other backends still write every block; keep
     * their conservative all-block reference traversal unchanged. */
    if (emit_reachable_core_blocks_only) {
        size_t block_index;
        if (!minic_core_function_mark_reachable_blocks(
                core, block_reachable, core->block_count)) {
            goto done;
        }
        for (block_index = 0U; block_index < core->block_count; ++block_index) {
            if (block_reachable[block_index]) {
                block_queue[block_queue_count++] = (MinicCoreBlockId)block_index;
            }
        }
    } else {
        size_t root_block;
        for (root_block = 0U; root_block < core->block_count; ++root_block) {
            if (!minic_enqueue_core_block((MinicCoreBlockId)root_block,
                                          core->block_count,
                                          block_reachable,
                                          block_queue,
                                          &block_queue_count)) {
                goto done;
            }
        }
    }

    while (block_queue_cursor < block_queue_count) {
        MinicCoreBlockId block_id = block_queue[block_queue_cursor++];
        const MinicCoreBlock *block = &core->blocks[block_id];
        size_t block_instruction_index;

        /* A valid block may contain only a terminator and therefore own no
         * instruction-id array.  Require storage only when the block actually
         * contains instructions. */
        if (block->instruction_count != 0U && block->instructions == NULL) {
            goto done;
        }

        for (block_instruction_index = 0U;
             block_instruction_index < block->instruction_count;
             ++block_instruction_index) {
            MinicCoreInstructionId instruction_id =
                block->instructions[block_instruction_index];
            const MinicCoreInstruction *instruction;

            if (instruction_id >= core->instruction_count) {
                goto done;
            }
            instruction = &core->instructions[instruction_id];
            /* FIXME(MC-064): opaque inline ASM may refer to a static
             * function solely through a GNU `i` symbolic operand; lowering
             * preserves only text, so no FUNCTION_ADDRESS edge is visited. */
            if (instruction->kind == MINIC_CORE_INSTRUCTION_CALL) {
                MinicCoreCalleeId callee_id = instruction->value.call.callee_id;
                const MinicCoreCallee *callee;
                if (callee_id >= core->callee_count) {
                    goto done;
                }
                callee = &core->callees[callee_id];
                if (!minic_enqueue_core_named_function(program,
                                                       callee->name,
                                                       callee->name_length,
                                                       function_reachable,
                                                       function_queue,
                                                       function_queue_count)) {
                    goto done;
                }
            } else if (instruction->kind == MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS) {
                MinicCoreFunctionSymbolId symbol_id = instruction->value.function_symbol_id;
                const MinicCoreFunctionSymbol *symbol;
                if (symbol_id >= core->function_symbol_count) {
                    goto done;
                }
                symbol = &core->function_symbols[symbol_id];
                if (!minic_enqueue_core_named_function(program,
                                                       symbol->name,
                                                       symbol->name_length,
                                                       function_reachable,
                                                       function_queue,
                                                       function_queue_count)) {
                    goto done;
                }
            } else if (objects != NULL &&
                       instruction->kind == MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS) {
                MinicCoreGlobalId global_id = instruction->value.global_id;
                if (global_id >= core->global_count ||
                    !minic_static_object_enqueue_named(
                        objects, core->globals[global_id].name,
                        core->globals[global_id].name_length)) {
                    goto done;
                }
            }
        }

        if (!block->has_terminator) {
            continue;
        }
        switch (block->terminator.kind) {
            case MINIC_CORE_TERMINATOR_RETURN:
            case MINIC_CORE_TERMINATOR_UNREACHABLE:
                break;
            case MINIC_CORE_TERMINATOR_BRANCH:
                if (!minic_enqueue_core_block(block->terminator.branch_target,
                                             core->block_count,
                                             block_reachable,
                                             block_queue,
                                             &block_queue_count)) {
                    goto done;
                }
                break;
            case MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH:
                if (!minic_enqueue_core_block(block->terminator.conditional.when_true,
                                             core->block_count,
                                             block_reachable,
                                             block_queue,
                                             &block_queue_count) ||
                    !minic_enqueue_core_block(block->terminator.conditional.when_false,
                                             core->block_count,
                                             block_reachable,
                                             block_queue,
                                             &block_queue_count)) {
                    goto done;
                }
                break;
            case MINIC_CORE_TERMINATOR_INDIRECT_BRANCH: {
                size_t target_block;
                /* The Core value feeding a computed goto can select any
                 * address-taken label. Conservatively retaining all blocks is
                 * correct; ordinary direct/conditional CFG stays precise. */
                for (target_block = 0U; target_block < core->block_count; ++target_block) {
                    if (!minic_enqueue_core_block((MinicCoreBlockId)target_block,
                                                 core->block_count,
                                                 block_reachable,
                                                 block_queue,
                                                 &block_queue_count)) {
                        goto done;
                    }
                }
                break;
            }
            default:
                goto done;
        }
    }
    success = true;

done:
    free(block_queue);
    free(block_reachable);
    return success;
}


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
        if (!minic_c0_program_global_object_emittable(program, object))
            continue;
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

/* Core pruning asks the same non-Core-root question for many functions.
 * Collect the immutable entry/force-emit/alias/relocation facts once for this
 * pass. Core pruning only changes is_referenced, not these root inputs.
 * Allocation failure retains the previous per-function scan. */
static bool *minic_build_noncore_root_index(const MinicC0Program *program) {
    bool *roots;
    bool *owner_live;
    size_t function_index;
    size_t object_index;

    if (program == NULL) {
        return NULL;
    }
    roots = (bool *)calloc(program->function_count == 0U ? 1U : program->function_count,
                           sizeof(*roots));
    if (roots == NULL) {
        return NULL;
    }
    owner_live = minic_c0_program_build_global_owner_live_map(program);
    for (function_index = 0U; function_index < program->function_count; ++function_index) {
        MinicFunctionId alias_target = program->functions[function_index].alias_target;
        if ((MinicFunctionId)function_index == program->entry_function ||
            program->functions[function_index].force_emit) {
            roots[function_index] = true;
        }
        if (alias_target < program->function_count) {
            roots[alias_target] = true;
        }
    }
    for (object_index = 0U; object_index < program->global_object_count; ++object_index) {
        const MinicGlobalObject *object = &program->global_objects[object_index];
        size_t relocation_index;
        if (!minic_c0_program_global_object_emittable_with_map(
                program, object, owner_live))
            continue;
        for (relocation_index = 0U; relocation_index < object->relocation_count;
             ++relocation_index) {
            const MinicGlobalRelocation *relocation = &object->relocations[relocation_index];
            MinicFunctionId target = (MinicFunctionId)relocation->target_id;
            if (relocation->target_kind == MINIC_GLOBAL_RELOCATION_FUNCTION &&
                target < program->function_count) {
                roots[target] = true;
            }
        }
    }
    free(owner_live);
    return roots;
}

static bool minic_inline_source_is_core_reachable_only(
    const MinicC0Program *program, MinicFunctionId source_id,
    const bool *noncore_roots) {
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
           !(noncore_roots != NULL
                 ? noncore_roots[source_id]
                 : minic_inline_source_has_noncore_root(program, source_id));
}

bool minic_prune_inline_specializations_from_core(
    MinicC0Program *program, const MinicCoreFunctionSet *set,
    bool emit_reachable_core_blocks_only) {
    bool *reachable = NULL;
    bool *processed = NULL;
    bool *noncore_roots = NULL;
    MinicFunctionId *queue = NULL;
    size_t queue_count = 0U;
    size_t queue_cursor = 0U;
    size_t function_index;
    size_t kept = 0U;
    size_t pruned = 0U;
    bool success = false;

    if (program == NULL || set == NULL || set->function_count != program->function_count ||
        (set->function_count != 0U &&
         (set->functions == NULL || set->statuses == NULL))) {
        return false;
    }
    reachable = (bool *)calloc(program->function_count == 0U ? 1U : program->function_count,
                               sizeof(*reachable));
    processed = (bool *)calloc(program->function_count == 0U ? 1U : program->function_count,
                               sizeof(*processed));
    queue = (MinicFunctionId *)malloc(
        (program->function_count == 0U ? 1U : program->function_count) * sizeof(*queue));
    if (reachable == NULL || processed == NULL || queue == NULL) {
        goto done;
    }

    /* All inputs to non-Core-root classification are stable throughout this
     * pass: only is_referenced changes below. If this small index cannot be
     * allocated, preserve the previous conservative per-query scan. */
    noncore_roots = minic_build_noncore_root_index(program);

    /* Preserve the established root policy for ordinary functions. Synthetic
     * specialization clones are never roots: they survive only through an
     * actual instruction in a CFG-reachable lowered Core block. */
    for (function_index = 0U; function_index < program->function_count; ++function_index) {
        const MinicFunction *function = &program->functions[function_index];
        if (function->is_integer_specialization || !function->is_defined ||
            (function->is_internal && !function->is_referenced)) {
            continue;
        }
        /* Parser-level references are only a discovery hint for internal
         * functions.  The final lowered Core graph is the authoritative
         * executable reachability source; non-Core roots stay conservative. */
        if (minic_inline_source_is_core_reachable_only(
                program, (MinicFunctionId)function_index, noncore_roots)) {
            continue;
        }
        reachable[function_index] = true;
        queue[queue_count++] = function_index;
    }

    while (queue_cursor < queue_count) {
        MinicFunctionId caller_id = queue[queue_cursor++];

        if (caller_id >= set->function_count || processed[caller_id]) {
            continue;
        }
        processed[caller_id] = true;
        if (set->statuses[caller_id] != MINIC_CORE_LOWER_OK) {
            continue;
        }
        if (!minic_walk_reachable_core_edges(program,
                                             &set->functions[caller_id],
                                             reachable,
                                             queue,
                                             &queue_count,
                                             emit_reachable_core_blocks_only,
                                             NULL)) {
            goto done;
        }
    }

    for (function_index = 0U; function_index < program->function_count; ++function_index) {
        MinicFunction *function = &program->functions[function_index];
        if (!function->is_integer_specialization) {
            if (minic_inline_source_is_core_reachable_only(
                    program, (MinicFunctionId)function_index, noncore_roots)) {
                function->is_referenced = reachable[function_index];
            }
            continue;
        }
        function->is_referenced = reachable[function_index];
        if (function->is_referenced) {
            kept += 1U;
        } else {
            pruned += 1U;
        }
    }
    if (kept != 0U || pruned != 0U) {
        (void)fprintf(stderr,
                      "MINIC_INLINE_SPECIALIZATION_CORE_REACHABILITY_V0 kept=%zu pruned=%zu\n",
                      kept,
                      pruned);
    }
    success = true;

done:
    free(noncore_roots);
    free(queue);
    free(processed);
    free(reachable);
    return success;
}

/* Only the final lowered Core graph can distinguish a live callback table
 * from a CONFIG-disabled static table whose address appeared solely in a
 * discarded C branch.  This pass runs after the established function pruning
 * pass and before the shared program emitter.  Its decisions are deliberately
 * monotonic and fail closed: incomplete Core, an ambiguous symbol, or an
 * allocation failure leaves ALL objects with their previous emission policy.
 *
 * Functions and global objects form one joint closure.  The existing Core
 * edge walker owns CALL/FUNCTION_ADDRESS/CFG semantics; this pass adds object
 * GLOBAL_ADDRESS edges and initializer object/function relocation edges. */
bool minic_prune_dead_file_scope_static_objects_from_core(
    MinicC0Program *program, const MinicCoreFunctionSet *set,
    bool emit_reachable_core_blocks_only) {
    MinicStaticObjectGraph graph;
    MinicStaticObjectSymbol *symbols = NULL;
    bool *function_reachable = NULL;
    bool *function_processed = NULL;
    MinicFunctionId *function_queue = NULL;
    size_t function_queue_count = 0U;
    size_t function_cursor = 0U;
    size_t object_cursor = 0U;
    size_t i;
    bool safe_to_prune = false;

    if (program == NULL || set == NULL ||
        set->function_count != program->function_count ||
        (set->function_count != 0U &&
         (set->functions == NULL || set->statuses == NULL))) return false;
    (void)memset(&graph, 0, sizeof(graph));
    graph.symbol_count = program->global_object_count;
    /* OOM is not a correctness failure: conservatively keep all storage. */
    symbols = (MinicStaticObjectSymbol *)calloc(
        program->global_object_count == 0U ? 1U : program->global_object_count,
        sizeof(*symbols));
    graph.reachable = (bool *)calloc(
        program->global_object_count == 0U ? 1U : program->global_object_count,
        sizeof(*graph.reachable));
    graph.queue = (MinicGlobalObjectId *)malloc(
        (program->global_object_count == 0U ? 1U : program->global_object_count) *
        sizeof(*graph.queue));
    function_reachable = (bool *)calloc(
        program->function_count == 0U ? 1U : program->function_count,
        sizeof(*function_reachable));
    function_processed = (bool *)calloc(
        program->function_count == 0U ? 1U : program->function_count,
        sizeof(*function_processed));
    function_queue = (MinicFunctionId *)malloc(
        (program->function_count == 0U ? 1U : program->function_count) *
        sizeof(*function_queue));
    if (symbols == NULL || graph.reachable == NULL || graph.queue == NULL ||
        function_reachable == NULL || function_processed == NULL || function_queue == NULL)
        goto done;

    for (i = 0U; i < program->global_object_count; ++i) {
        symbols[i].name = program->global_objects[i].name;
        symbols[i].name_length = program->global_objects[i].name_length;
        symbols[i].object_id = i;
        if (symbols[i].name == NULL || symbols[i].name_length == 0U) goto done;
    }
    qsort(symbols, program->global_object_count, sizeof(*symbols),
          minic_static_object_symbol_compare);
    for (i = 1U; i < program->global_object_count; ++i) {
        if (minic_static_object_symbol_compare(&symbols[i - 1U], &symbols[i]) == 0)
            goto done; /* Multiple ELF identities: cannot disambiguate. */
    }
    graph.symbols = symbols;

    for (i = 0U; i < program->function_count; ++i) {
        const MinicFunction *function = &program->functions[i];
        bool root;
        if (function->is_integer_specialization || !function->is_defined) continue;
        /* Core may retain parse-only static inline definitions that the
         * backend would not emit. An opaque ASM opcode sharing their name
         * (notably `cpuid`) cannot resurrect those dead source bodies. */
        if (function->is_internal && !function->is_referenced &&
            !function->force_emit && !function->is_constructor) continue;
        root = !function->is_internal || function->force_emit ||
               function->is_constructor ||
               (function->is_referenced &&
                (function->section_name != NULL ||
                 function->assembler_name != NULL ||
                 function->visibility != MINIC_SYMBOL_VISIBILITY_DEFAULT)) ||
               (MinicFunctionId)i == program->entry_function ||
               minic_program_has_opaque_asm_symbol(
                   program, function->name, function->name_length);
        if (root && !minic_enqueue_core_function(
                        program, (MinicFunctionId)i, function_reachable,
                        function_queue, &function_queue_count)) goto done;
        /* Alias ELF symbols can outlive their source lexical references. */
        if (function->alias_target < program->function_count &&
            !minic_enqueue_core_function(
                program, function->alias_target, function_reachable,
                function_queue, &function_queue_count)) goto done;
    }

    for (i = 0U; i < program->global_object_count; ++i) {
        const MinicGlobalObject *object = &program->global_objects[i];
        bool root = !object->is_internal || object->force_emit ||
                    object->section_name != NULL ||
                    object->alias_target < program->global_object_count ||
                    object->is_weak || object->is_thread_local ||
                    object->visibility != MINIC_SYMBOL_VISIBILITY_DEFAULT ||
                    minic_program_has_opaque_asm_symbol(
                        program, object->name, object->name_length);
        /* Function-local statics retain the previous owner policy.  Until a
         * separate owner graph is proven, even the parser's conservative
         * is_referenced value is a safe root for those initializers. */
        if (object->owner_function != MINIC_FUNCTION_INVALID) {
            MinicFunctionId owner = object->owner_function;
            root = owner < program->function_count &&
                   program->functions[owner].is_referenced;
        }
        if (root && !minic_static_object_enqueue(&graph, i)) goto done;
    }

    /* GNU asm `i` operands can materialize a global symbol directly as text
     * (including asm-goto and architecture-specific deferred forms). Such
     * relocation roots do not necessarily survive as Core GLOBAL_ADDRESS
     * instructions. Preserve every static object explicitly mentioned by a
     * source asm operand expression; unsupported nested shapes fail closed. */
    for (i = 0U; i < program->inline_asm_count; ++i) {
        const MinicInlineAsm *source_asm = &program->inline_asms[i];
        MinicAsmObjectRootWalk walk;
        size_t operand_index;
        if ((source_asm->input_count != 0U && source_asm->inputs == NULL) ||
            (source_asm->output_count != 0U && source_asm->outputs == NULL)) goto done;
        walk.program = program;
        walk.objects = &graph;
        walk.depth = 0U;
        for (operand_index = 0U; operand_index < source_asm->input_count;
             ++operand_index) {
            MinicExpressionId id = source_asm->inputs[operand_index].expression;
            if (!minic_static_asm_operand_visit(&id, &walk)) goto done;
        }
        for (operand_index = 0U; operand_index < source_asm->output_count;
             ++operand_index) {
            MinicExpressionId id = source_asm->outputs[operand_index].expression;
            if (!minic_static_asm_operand_visit(&id, &walk)) goto done;
        }
    }

    while (function_cursor < function_queue_count ||
           object_cursor < graph.queue_count) {
        while (object_cursor < graph.queue_count) {
            MinicGlobalObjectId object_id = graph.queue[object_cursor++];
            const MinicGlobalObject *object = &program->global_objects[object_id];
            size_t relocation_index;
            if (object->alias_target < program->global_object_count &&
                !minic_static_object_enqueue(&graph, object->alias_target)) goto done;
            for (relocation_index = 0U;
                 relocation_index < object->relocation_count; ++relocation_index) {
                const MinicGlobalRelocation *relocation =
                    &object->relocations[relocation_index];
                if (relocation->target_kind == MINIC_GLOBAL_RELOCATION_OBJECT) {
                    if (!minic_static_object_enqueue(
                            &graph, (MinicGlobalObjectId)relocation->target_id)) goto done;
                } else if (relocation->target_kind == MINIC_GLOBAL_RELOCATION_FUNCTION) {
                    if (!minic_enqueue_core_function(
                            program, (MinicFunctionId)relocation->target_id,
                            function_reachable, function_queue,
                            &function_queue_count)) goto done;
                }
            }
        }
        if (function_cursor < function_queue_count) {
            MinicFunctionId function_id = function_queue[function_cursor++];
            if (function_id >= set->function_count || function_processed[function_id])
                continue;
            function_processed[function_id] = true;
            if (set->statuses[function_id] != MINIC_CORE_LOWER_OK) goto done;
            if (!minic_walk_reachable_core_edges(
                    program, &set->functions[function_id], function_reachable,
                    function_queue, &function_queue_count,
                    emit_reachable_core_blocks_only, &graph)) goto done;
        }
    }
    safe_to_prune = true;

done:
    if (safe_to_prune) {
        for (i = 0U; i < program->global_object_count; ++i) {
            MinicGlobalObject *object = &program->global_objects[i];
            if (object->owner_function == MINIC_FUNCTION_INVALID && object->is_internal &&
                !graph.reachable[i]) object->is_emission_pruned = true;
        }
    }
    free(function_queue);
    free(function_processed);
    free(function_reachable);
    free(graph.queue);
    free(graph.reachable);
    free(symbols);
    return true;
}

