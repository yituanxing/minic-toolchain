#include "compiler/core_reachability.h"
#include "core/core_ir.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Core and ModuleIR are the entire source-independent ownership graph.
 * Function/global ordinals are stable across lowering and module ingress. */
typedef struct ModuleObjectSymbol {
    const char *name;
    size_t length, id;
} ModuleObjectSymbol;

typedef struct ModuleClosure {
    MinicCoreFunctionSet *set;
    bool reachable_blocks_only;
    bool *functions_live, *functions_seen, *objects_live;
    size_t *function_queue, *object_queue, *owned_heads, *owned_next;
    size_t function_head, function_count, object_head, object_count;
    ModuleObjectSymbol *symbols;
    MinicCoreModuleFunctionNameIndex function_names;
} ModuleClosure;

static int compare_object_symbol(const void *a, const void *b) {
    const ModuleObjectSymbol *left = a, *right = b;
    if (left->length < right->length) return -1;
    if (left->length > right->length) return 1;
    return memcmp(left->name, right->name, left->length);
}

static bool enqueue_object(ModuleClosure *c, size_t id) {
    const MinicCoreModule *m = &c->set->module;
    if (id >= m->global_count) return false;
    if (c->objects_live[id]) return true;
    if (c->object_count >= m->global_count) return false;
    c->objects_live[id] = true;
    c->object_queue[c->object_count++] = id;
    return true;
}

static bool enqueue_owned(ModuleClosure *c, size_t owner) {
    const MinicCoreModule *m = &c->set->module;
    size_t id;
    if (owner >= m->function_count) return false;
    for (id = c->owned_heads[owner]; id != SIZE_MAX; id = c->owned_next[id])
        if (!enqueue_object(c, id)) return false;
    return true;
}

static bool enqueue_function(ModuleClosure *c, size_t id) {
    const MinicCoreModule *m = &c->set->module;
    const MinicCoreModuleFunctionSymbol *f;
    if (id >= m->function_count) return false;
    f = &m->function_symbols[id];
    if (!f->is_defined || c->functions_live[id]) return true;
    if (c->function_count >= m->function_count) return false;
    c->functions_live[id] = true;
    c->function_queue[c->function_count++] = id;
    if (!enqueue_owned(c, id)) return false;
    /* Borrowers retain local statics owned by the canonical Module function.
     * The source implementation may use this generic edge for cloned bodies. */
    return f->storage_owner_function_index == SIZE_MAX ||
        enqueue_owned(c, f->storage_owner_function_index);
}

static bool enqueue_named_function(ModuleClosure *c, const char *name, size_t n) {
    size_t first, last, i;
    if (name == NULL || n == 0U) return false;
    if (!minic_core_module_function_name_index_find(
            &c->function_names, name, n, &first, &last)) return false;
    for (i = first; i < last; ++i)
        if (!enqueue_function(c, c->function_names.entries[i].function_index))
            return false;
    return true; /* External/target-synthesized function. */
}

static bool enqueue_named_object(ModuleClosure *c, const char *name, size_t n) {
    const size_t count = c->set->module.global_count;
    size_t lo = 0U, hi = count, i;
    if (name == NULL || n == 0U) return false;
    /* Name lookup is a range: multiple C declarations may have one assembler
     * identity. Keep exactly those owners, not every unrelated static object
     * in the translation unit whenever any duplicate spelling exists. */
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2U;
        const ModuleObjectSymbol *s = &c->symbols[mid];
        int cmp = n < s->length ? -1 : n > s->length ? 1 :
            memcmp(name, s->name, n);
        if (cmp <= 0) hi = mid;
        else lo = mid + 1U;
    }
    for (i = lo; i < count; ++i) {
        const ModuleObjectSymbol *s = &c->symbols[i];
        if (s->length != n || memcmp(s->name, name, n) != 0) break;
        if (!enqueue_object(c, s->id)) return false;
    }
    return true; /* External/target-synthesized object. */
}

static bool core_asm_id_for_instruction(const MinicCoreInstruction *ins,
                                        MinicCoreInlineAsmId *id) {
    switch (ins->kind) {
    case MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM:
        *id = ins->value.inline_asm_id; return true;
    case MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM:
        *id = ins->value.structured_inline_asm.inline_asm_id; return true;
    default: return false;
    }
}

/* Only a live Core instruction can contribute these edges. Literal GNU ASM
 * text has no C operand identity: conservatively resolve whole assembler
 * tokens, while typed operand references remain exact and source-independent. */
static bool scan_core_inline_asm(ModuleClosure *c,
                                  const MinicCoreInlineAsm *assembly) {
    const MinicCoreModule *m = &c->set->module;
    size_t i;
    for (i = 0U; i < assembly->symbol_reference_count; ++i) {
        const MinicCoreInlineAsmSymbolReference *ref =
            &assembly->symbol_references[i];
        if (ref->kind == MINIC_CORE_INLINE_ASM_SYMBOL_FUNCTION) {
            if (!enqueue_named_function(c, ref->name, ref->name_length)) return false;
        } else if (ref->kind == MINIC_CORE_INLINE_ASM_SYMBOL_OBJECT) {
            if (!enqueue_named_object(c, ref->name, ref->name_length)) return false;
        } else return false;
    }
    if (assembly->preserve_all_static_objects)
        for (i = 0U; i < m->global_count; ++i)
            if (!enqueue_object(c, i)) return false;
    if (assembly->template_text == NULL) return false;
    for (i = 0U; i < m->function_count; ++i) {
        const MinicCoreModuleFunctionSymbol *f = &m->function_symbols[i];
        if (f->name != NULL &&
            minic_core_asm_names_symbol(assembly->template_text,
                assembly->template_length, f->name) &&
            !enqueue_named_function(c, f->name, strlen(f->name))) return false;
    }
    for (i = 0U; i < m->global_count; ++i) {
        const MinicCoreModuleGlobal *g = &m->globals[i];
        if (g->name != NULL &&
            minic_core_asm_names_symbol(assembly->template_text,
                assembly->template_length, g->name) &&
            !enqueue_named_object(c, g->name, strlen(g->name))) return false;
    }
    return true;
}

static bool scan_core_function(ModuleClosure *c, size_t id) {
    const MinicCoreFunction *fn = &c->set->functions[id];
    bool *reach = NULL;
    MinicCoreBlockId *queue = NULL;
    size_t count = 0U, cursor = 0U, i;
    bool ok = false;
    /* A reached lowering failure is reported by the post-closure verifier.
     * It is not license to retain an unrelated dead static body. */
    if (c->set->statuses[id] != MINIC_CORE_LOWER_OK) return true;
    if (fn->block_count == 0U) return true;
    if (fn->blocks == NULL || fn->entry_block >= fn->block_count ||
        (fn->instruction_count != 0U && fn->instructions == NULL) ||
        fn->block_count > SIZE_MAX / sizeof(*queue)) return false;
    reach = calloc(fn->block_count, sizeof(*reach));
    queue = malloc(fn->block_count * sizeof(*queue));
    if (reach == NULL || queue == NULL) goto done;
    if (c->reachable_blocks_only) {
        if (!minic_core_function_mark_reachable_blocks(fn, reach, fn->block_count))
            goto done;
    } else memset(reach, 1, fn->block_count * sizeof(*reach));
    for (i = 0U; i < fn->block_count; ++i)
        if (reach[i]) queue[count++] = (MinicCoreBlockId)i;
    while (cursor < count) {
        const MinicCoreBlock *block = &fn->blocks[queue[cursor++]];
        size_t j;
        if (block->instruction_count != 0U && block->instructions == NULL)
            goto done;
        for (j = 0U; j < block->instruction_count; ++j) {
            MinicCoreInstructionId ins_id = block->instructions[j];
            const MinicCoreInstruction *ins;
            if (ins_id >= fn->instruction_count) goto done;
            ins = &fn->instructions[ins_id];
            if (ins->kind == MINIC_CORE_INSTRUCTION_CALL) {
                MinicCoreCalleeId callee = ins->value.call.callee_id;
                if (callee >= fn->callee_count ||
                    !enqueue_named_function(c, fn->callees[callee].name,
                                            fn->callees[callee].name_length)) goto done;
            } else if (ins->kind == MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS) {
                MinicCoreFunctionSymbolId symbol = ins->value.function_symbol_id;
                if (symbol >= fn->function_symbol_count ||
                    !enqueue_named_function(c, fn->function_symbols[symbol].name,
                                            fn->function_symbols[symbol].name_length)) goto done;
            } else if (ins->kind == MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS) {
                MinicCoreGlobalId global = ins->value.global_id;
                if (global >= fn->global_count ||
                    !enqueue_named_object(c, fn->globals[global].name,
                                          fn->globals[global].name_length)) goto done;
            } else {
                MinicCoreInlineAsmId asm_id;
                if (core_asm_id_for_instruction(ins, &asm_id) &&
                    (asm_id >= fn->inline_asm_count ||
                     !scan_core_inline_asm(c, &fn->inline_asms[asm_id]))) goto done;
            }
            /* BLOCK_ADDRESS denotes a Core-local block, not a TU symbol. */
        }
        if (block->has_terminator &&
            block->terminator.kind == MINIC_CORE_TERMINATOR_INDIRECT_BRANCH) {
            /* Preserve the historical all-block conservative computed-goto
             * closure, even when the backend's CFG filter is narrower. */
            for (i = 0U; i < fn->block_count; ++i)
                if (!reach[i]) {
                    reach[i] = true;
                    queue[count++] = (MinicCoreBlockId)i;
                }
        }
    }
    ok = true;
done:
    free(queue);
    free(reach);
    return ok;
}

static void destroy_closure(ModuleClosure *c) {
    minic_core_module_function_name_index_destroy(&c->function_names);
    free(c->symbols);
    free(c->owned_next);
    free(c->owned_heads);
    free(c->object_queue);
    free(c->function_queue);
    free(c->objects_live);
    free(c->functions_seen);
    free(c->functions_live);
}

static bool initialize_closure(ModuleClosure *c) {
    const MinicCoreModule *m = &c->set->module;
    const size_t nf = m->function_count == 0U ? 1U : m->function_count;
    const size_t no = m->global_count == 0U ? 1U : m->global_count;
    size_t i;
    if (nf > SIZE_MAX / sizeof(size_t) || no > SIZE_MAX / sizeof(size_t) ||
        no > SIZE_MAX / sizeof(*c->symbols)) return false;
    c->functions_live = calloc(nf, sizeof(bool));
    c->functions_seen = calloc(nf, sizeof(bool));
    c->objects_live = calloc(no, sizeof(bool));
    c->function_queue = malloc(nf * sizeof(size_t));
    c->object_queue = malloc(no * sizeof(size_t));
    c->owned_heads = malloc(nf * sizeof(size_t));
    c->owned_next = malloc(no * sizeof(size_t));
    c->symbols = calloc(no, sizeof(*c->symbols));
    if (c->functions_live == NULL || c->functions_seen == NULL ||
        c->objects_live == NULL || c->function_queue == NULL ||
        c->object_queue == NULL || c->owned_heads == NULL ||
        c->owned_next == NULL || c->symbols == NULL) return false;
    if (!minic_core_module_function_name_index_build(
            m, &c->function_names)) return false;
    for (i = 0U; i < m->function_count; ++i) c->owned_heads[i] = SIZE_MAX;
    for (i = 0U; i < m->global_count; ++i) {
        const MinicCoreModuleGlobal *g = &m->globals[i];
        const size_t owner = g->owner_function_index;
        if (g->name == NULL || g->name[0] == '\0') return false;
        c->symbols[i].name = g->name;
        c->symbols[i].length = strlen(g->name);
        c->symbols[i].id = i;
        c->owned_next[i] = SIZE_MAX;
        if (owner != SIZE_MAX) {
            if (owner >= m->function_count) return false;
            c->owned_next[i] = c->owned_heads[owner];
            c->owned_heads[owner] = i;
        }
    }
    qsort(c->symbols, m->global_count, sizeof(*c->symbols), compare_object_symbol);
    return true;
}

bool minic_core_function_set_close_reachability(
    MinicCoreFunctionSet *set, bool emit_reachable_core_blocks_only) {
    ModuleClosure c;
    MinicCoreModule *m;
    bool *canonical_owner_live = NULL;
    size_t i, kept = 0U, pruned = 0U;
    bool success = false;
    if (set == NULL || set->module.finalized ||
        set->module.function_count != set->function_count ||
        (set->function_count != 0U &&
         (set->functions == NULL || set->statuses == NULL ||
          set->module.function_symbols == NULL)) ||
        (set->module.global_count != 0U && set->module.globals == NULL))
        return false;
    memset(&c, 0, sizeof(c));
    c.set = set;
    c.reachable_blocks_only = emit_reachable_core_blocks_only;
    m = &set->module;
    if (!initialize_closure(&c)) goto done;

    for (i = 0U; i < m->function_count; ++i) {
        const MinicCoreModuleFunctionSymbol *f = &m->function_symbols[i];
        if (f->is_reachability_root && !enqueue_function(&c, i)) goto done;
        /* Always-emitted file-scope ASM is a true Module root. Function-body
         * ASM operand references instead close through live Core effects. */
        if (f->is_file_asm_root &&
            !enqueue_named_function(&c, f->name, strlen(f->name))) goto done;
        /* Alias definitions can remain linker-visible without a C callsite. */
        if (f->alias_target_index != SIZE_MAX &&
            !enqueue_function(&c, f->alias_target_index)) goto done;
    }
    for (i = 0U; i < m->global_count; ++i) {
        const MinicCoreModuleGlobal *g = &m->globals[i];
        if ((g->is_file_asm_root ||
             (g->owner_function_index == SIZE_MAX &&
              g->is_file_scope_reachability_root)) &&
            !enqueue_object(&c, i)) goto done;
    }

    while (c.function_head < c.function_count || c.object_head < c.object_count) {
        while (c.object_head < c.object_count) {
            const size_t id = c.object_queue[c.object_head++];
            const MinicCoreModuleGlobal *g = &m->globals[id];
            size_t j;
            if (g->alias_target_index != SIZE_MAX &&
                !enqueue_object(&c, g->alias_target_index)) goto done;
            for (j = 0U; j < g->reference_count; ++j) {
                const MinicCoreModuleReference *ref = &g->references[j];
                if (ref->kind == MINIC_CORE_MODULE_REFERENCE_OBJECT) {
                    if (!enqueue_object(&c, ref->target_index)) goto done;
                } else if (ref->kind == MINIC_CORE_MODULE_REFERENCE_FUNCTION) {
                    const char *name;
                    if (ref->target_index >= m->function_count) goto done;
                    name = m->function_symbols[ref->target_index].name;
                    /* A function pointer relocates by assembler spelling.
                     * Its original Module FunctionId may be only an extern
                     * declaration while an internal definition has the name. */
                    if (name == NULL ||
                        !enqueue_named_function(&c, name, strlen(name))) goto done;
                } else if (ref->kind == MINIC_CORE_MODULE_REFERENCE_OWNER_LABEL) {
                    /* A label relocation specifically retains the canonical
                     * owner's FunctionBody, never a source-sharing clone. */
                    if (!enqueue_function(&c, ref->target_index)) goto done;
                } else goto done;
            }
        }
        if (c.function_head < c.function_count) {
            const size_t id = c.function_queue[c.function_head++];
            if (c.functions_seen[id]) continue;
            c.functions_seen[id] = true;
            if (!scan_core_function(&c, id)) goto done;
        }
    }

    canonical_owner_live = calloc(m->function_count == 0U ? 1U :
                                   m->function_count, sizeof(bool));
    if (canonical_owner_live == NULL) goto done;
    for (i = 0U; i < m->function_count; ++i) {
        const MinicCoreModuleFunctionSymbol *f = &m->function_symbols[i];
        if (!c.functions_live[i]) continue;
        canonical_owner_live[i] = true;
        if (f->storage_owner_function_index != SIZE_MAX)
            canonical_owner_live[f->storage_owner_function_index] = true;
    }
    /* Transactional publication: no C program writes, no partial pruning. */
    for (i = 0U; i < m->function_count; ++i) {
        MinicCoreModuleFunctionSymbol *f = &m->function_symbols[i];
        if (f->is_defined && f->is_internal) f->is_referenced = c.functions_live[i];
        if (f->storage_owner_function_index != SIZE_MAX) {
            if (f->is_referenced) ++kept;
            else ++pruned;
        }
    }
    for (i = 0U; i < m->global_count; ++i) {
        MinicCoreModuleGlobal *g = &m->globals[i];
        if (g->owner_function_index != SIZE_MAX)
            g->emittable = canonical_owner_live[g->owner_function_index] ||
                c.objects_live[i];
        else
            g->emittable = !g->is_internal || c.objects_live[i];
    }
    if (kept != 0U || pruned != 0U)
        (void)fprintf(stderr,
                      "MINIC_INLINE_SPECIALIZATION_CORE_REACHABILITY_V0 kept=%zu pruned=%zu\n",
                      kept, pruned);
    success = true;
done:
    free(canonical_owner_live);
    destroy_closure(&c);
    return success;
}
