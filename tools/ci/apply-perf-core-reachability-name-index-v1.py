#!/usr/bin/env python3
"""P03: TU-lifetime assembler-symbol -> first FunctionId hash index.

Applies after the current Linux runtime patch stack. Keeps the original
lookup as an allocation/consistency fallback, preserves first-match semantics,
and does not confuse an absent symbol with a failed index build.
"""
from pathlib import Path

p = Path("src/compiler/compiler.c")
text = p.read_text()

def change(old, new):
    global text
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"P03 anchor count={count}, wanted=1: {old[:130]!r}")
    text = text.replace(old, new, 1)

change(
    "static MinicFunctionId minic_core_named_function_id(\n",
    "static MinicFunctionId minic_core_named_function_id_linear(\n",
)

helper = r'''
/* This index lives for exactly one Core-pruning pass.  It owns only the slot
 * array: symbol bytes remain owned by the current C0Program.  In particular,
 * assembler_name is the canonical key; the parser's name hash is NOT
 * interchangeable with this index. */
typedef struct MinicCoreNameIndex {
    const MinicC0Program *program;
    size_t *slots;
    size_t capacity;
    size_t lookups;
    size_t probes;
    size_t fallback_lookups;
    size_t indexed_functions;
} MinicCoreNameIndex;

static size_t minic_core_name_hash(const char *name, size_t length) {
    uint64_t value = UINT64_C(14695981039346656037);
    size_t index;
    for (index = 0U; index < length; ++index) {
        value ^= (unsigned char)name[index];
        value *= UINT64_C(1099511628211);
    }
    return (size_t)value;
}

static bool minic_core_name_index_build(
    const MinicC0Program *program, MinicCoreNameIndex *index) {
    size_t function_index;
    size_t capacity = 16U;
    size_t entry_count = 0U;

    if (program == NULL || index == NULL ||
        program->function_count > SIZE_MAX / 2U) {
        return false;
    }
    /* Load <= 50%; never allocate on every lookup. */
    while (capacity < program->function_count * 2U) {
        if (capacity > SIZE_MAX / 2U) {
            return false;
        }
        capacity *= 2U;
    }
    if (capacity > SIZE_MAX / sizeof(*index->slots)) {
        return false;
    }
    index->slots = (size_t *)malloc(capacity * sizeof(*index->slots));
    if (index->slots == NULL) {
        return false;
    }
    for (function_index = 0U; function_index < capacity; ++function_index) {
        index->slots[function_index] = SIZE_MAX;
    }
    index->program = program;
    index->capacity = capacity;
    for (function_index = 0U; function_index < program->function_count;
         ++function_index) {
        const MinicFunction *function = &program->functions[function_index];
        const char *symbol = minic_c0_function_symbol_name(function);
        size_t length = function->assembler_name != NULL
                            ? function->assembler_name_length : function->name_length;
        size_t probe;
        size_t slot;
        if (symbol == NULL) {
            continue;
        }
        slot = minic_core_name_hash(symbol, length) & (capacity - 1U);
        for (probe = 0U; probe < capacity; ++probe) {
            size_t previous = index->slots[slot];
            if (previous == SIZE_MAX) {
                index->slots[slot] = function_index;
                entry_count += 1U;
                break;
            }
            if (previous < function_index) {
                const MinicFunction *prior = &program->functions[previous];
                const char *prior_symbol = minic_c0_function_symbol_name(prior);
                size_t prior_length = prior->assembler_name != NULL
                                          ? prior->assembler_name_length : prior->name_length;
                /* Matching aliases must resolve to the FIRST function,
                 * identical to the old forward linear search. */
                if (prior_symbol != NULL && prior_length == length &&
                    memcmp(prior_symbol, symbol, length) == 0) {
                    break;
                }
            }
            slot = (slot + 1U) & (capacity - 1U);
        }
        if (probe == capacity) {
            free(index->slots);
            index->slots = NULL;
            index->capacity = 0U;
            index->program = NULL;
            return false;
        }
    }
    index->indexed_functions = entry_count;
    return true;
}

static MinicFunctionId minic_core_named_function_id_indexed(
    const MinicC0Program *program, MinicCoreNameIndex *index,
    const char *name, size_t name_length) {
    size_t slot;
    size_t probe;
    if (index != NULL) {
        index->lookups += 1U;
    }
    if (program == NULL || name == NULL || name_length == 0U) {
        return MINIC_FUNCTION_INVALID;
    }
    if (index == NULL || index->slots == NULL || index->program != program ||
        index->capacity == 0U) {
        if (index != NULL) {
            index->fallback_lookups += 1U;
        }
        return minic_core_named_function_id_linear(program, name, name_length);
    }
    slot = minic_core_name_hash(name, name_length) & (index->capacity - 1U);
    for (probe = 0U; probe < index->capacity; ++probe) {
        size_t function_index = index->slots[slot];
        index->probes += 1U;
        if (function_index == SIZE_MAX) {
            return MINIC_FUNCTION_INVALID;
        }
        if (function_index >= program->function_count) {
            break;  /* Inconsistent index: use original lookup. */
        }
        {
            const MinicFunction *function = &program->functions[function_index];
            const char *symbol = minic_c0_function_symbol_name(function);
            size_t length = function->assembler_name != NULL
                                ? function->assembler_name_length : function->name_length;
            if (symbol != NULL && length == name_length &&
                memcmp(symbol, name, name_length) == 0) {
                return function_index;
            }
        }
        slot = (slot + 1U) & (index->capacity - 1U);
    }
    index->fallback_lookups += 1U;
    return minic_core_named_function_id_linear(program, name, name_length);
}

static void minic_core_name_index_destroy(MinicCoreNameIndex *index) {
    if (index != NULL) {
        free(index->slots);
        index->slots = NULL;
        index->program = NULL;
        index->capacity = 0U;
    }
}

'''
change("static bool minic_enqueue_core_named_function(\n",
       helper+"static bool minic_enqueue_core_named_function(\n")
change(
    """static bool minic_enqueue_core_named_function(
    const MinicC0Program *program,
    const char *name,""",
    """static bool minic_enqueue_core_named_function(
    const MinicC0Program *program,
    MinicCoreNameIndex *name_index,
    const char *name,""",
)
change(
    "    target = minic_core_named_function_id(program, name, name_length);\n",
    "    target = minic_core_named_function_id_indexed(program, name_index, name, name_length);\n",
)
change(
    """static bool minic_walk_reachable_core_edges(const MinicC0Program *program,
                                            const MinicCoreFunction *core,""",
    """static bool minic_walk_reachable_core_edges(const MinicC0Program *program,
                                            MinicCoreNameIndex *name_index,
                                            const MinicCoreFunction *core,""",
)
count = text.count("minic_enqueue_core_named_function(program,\n")
if count != 2:
    raise SystemExit(f"expected two named-function enqueues, found {count}")
text = text.replace("minic_enqueue_core_named_function(program,\n",
                    "minic_enqueue_core_named_function(program,\n                                                       name_index,\n")
change(
    """    bool success = false;

    if (program == NULL || set == NULL || set->function_count != program->function_count ||""",
    """    bool success = false;
    MinicCoreNameIndex name_index = {0};

    if (program == NULL || set == NULL || set->function_count != program->function_count ||""",
)
change(
    """    if (reachable == NULL || processed == NULL || queue == NULL) {
        goto done;
    }

    /* Preserve the established root policy""",
    """    if (reachable == NULL || processed == NULL || queue == NULL) {
        goto done;
    }
    /* A failed index must NOT be mistaken for a missing function. */
    (void)minic_core_name_index_build(program, &name_index);

    /* Preserve the established root policy""",
)
change(
    """        if (!minic_walk_reachable_core_edges(program,
                                             &set->functions[caller_id],""",
    """        if (!minic_walk_reachable_core_edges(program,
                                             &name_index,
                                             &set->functions[caller_id],""",
)
change(
    """done:
    free(queue);
    free(processed);
    free(reachable);
    return success;
}

""",
    """done:
    if (getenv("MINIC_CORE_NAME_INDEX_PERF") != NULL) {
        (void)fprintf(stderr,
                      "MINIC_CORE_NAME_INDEX calls=%zu probes=%zu fallback=%zu "
                      "entries=%zu capacity=%zu functions=%zu\\n",
                      name_index.lookups, name_index.probes,
                      name_index.fallback_lookups, name_index.indexed_functions,
                      name_index.capacity, program->function_count);
    }
    minic_core_name_index_destroy(&name_index);
    free(queue);
    free(processed);
    free(reachable);
    return success;
}

""",
)
p.write_text(text)
print("MINIC_PERF_CORE_REACHABILITY_NAME_INDEX_V1=APPLIED")
