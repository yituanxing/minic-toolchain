#!/usr/bin/env python3
"""Opt-in one-factor CPU performance candidate (do not patch production Git).

Scope: Core instruction reachability CALL / FUNCTION_ADDRESS name resolution.
Before: each incoming symbol searches program->functions linearly, first match.
After: one open-addressed per-pass immutable assembler-symbol name index,
earliest function ID wins. OOM/overflow/bad index falls back to exact old scan.
Do NOT confuse with Parser name hash or Core goto-reentry indexes.
"""
from pathlib import Path

p = Path("src/compiler/compiler.c")
s = p.read_text()

def once(old, new):
    global s
    if s.count(old) != 1:
        raise SystemExit("CORE_NAME_INDEX_V1: anchor mismatch count=%d: %r" %
                         (s.count(old), old[:130]))
    s = s.replace(old, new, 1)

helper = r'''
typedef struct MinicCoreNameIndex {
    MinicFunctionId *slots;
    size_t capacity;
} MinicCoreNameIndex;

/* FNV-1a is only a bucket selector: equality is always exact bytewise. */
static size_t minic_core_name_index_hash(const char *name, size_t len) {
    uint64_t hash = UINT64_C(1469598103934665603);
    size_t i;
    for (i = 0U; i < len; ++i) {
        hash ^= (unsigned char)name[i];
        hash *= UINT64_C(1099511628211);
    }
    return (size_t)hash;
}

/* Built exactly once for each Core pruning pass, before is_referenced mutates.
 * Function/assembler names stay unchanged while this pass executes.
 * The first source function with a matching symbol must win, even if it is
 * only a declaration: later definitions must NOT shadow it here. */
static bool minic_core_name_index_build(
    const MinicC0Program *program, MinicCoreNameIndex *index) {
    size_t desired;
    size_t capacity = 16U;
    size_t i;

    if (program == NULL || index == NULL ||
        index->slots != NULL || index->capacity != 0U ||
        program->function_count > SIZE_MAX / 2U) {
        return false;
    }
    desired = program->function_count * 2U;
    while (capacity < desired) {
        if (capacity > SIZE_MAX / 2U) {
            return false;
        }
        capacity *= 2U;
    }
    if (capacity > SIZE_MAX / sizeof(*index->slots)) {
        return false;
    }
    index->slots = (MinicFunctionId *)malloc(capacity * sizeof(*index->slots));
    if (index->slots == NULL) {
        return false;
    }
    index->capacity = capacity;
    for (i = 0U; i < capacity; ++i) {
        index->slots[i] = MINIC_FUNCTION_INVALID;
    }
    for (i = 0U; i < program->function_count; ++i) {
        const MinicFunction *f = &program->functions[i];
        const char *name = minic_c0_function_symbol_name(f);
        size_t name_length = f->assembler_name != NULL
                                 ? f->assembler_name_length : f->name_length;
        size_t slot;
        size_t steps;
        if (name == NULL || name_length == 0U) {
            continue;
        }
        slot = minic_core_name_index_hash(name, name_length) & (capacity - 1U);
        for (steps = 0U; steps < capacity; ++steps) {
            MinicFunctionId existing = index->slots[slot];
            if (existing == MINIC_FUNCTION_INVALID) {
                index->slots[slot] = (MinicFunctionId)i;
                break;
            }
            if (existing < program->function_count) {
                const MinicFunction *old = &program->functions[existing];
                const char *old_name = minic_c0_function_symbol_name(old);
                size_t old_length = old->assembler_name != NULL
                                        ? old->assembler_name_length : old->name_length;
                if (old_name != NULL && old_length == name_length &&
                    memcmp(old_name, name, name_length) == 0) {
                    /* Same symbol: keep FIRST ID, exactly as linear search. */
                    break;
                }
            }
            slot = (slot + 1U) & (capacity - 1U);
        }
        if (steps == capacity) {
            free(index->slots);
            index->slots = NULL;
            index->capacity = 0U;
            return false;
        }
    }
    return true;
}

static MinicFunctionId minic_core_name_index_lookup(
    const MinicC0Program *program,
    const MinicCoreNameIndex *index,
    const char *name, size_t name_length) {
    size_t slot;
    size_t steps;
    if (program == NULL || index == NULL || index->slots == NULL ||
        index->capacity == 0U || name == NULL || name_length == 0U) {
        return MINIC_FUNCTION_INVALID;
    }
    slot = minic_core_name_index_hash(name, name_length) & (index->capacity - 1U);
    for (steps = 0U; steps < index->capacity; ++steps) {
        MinicFunctionId function_id = index->slots[slot];
        if (function_id == MINIC_FUNCTION_INVALID) {
            return MINIC_FUNCTION_INVALID;
        }
        if (function_id < program->function_count) {
            const MinicFunction *function = &program->functions[function_id];
            const char *symbol = minic_c0_function_symbol_name(function);
            size_t length = function->assembler_name != NULL
                                ? function->assembler_name_length : function->name_length;
            if (symbol != NULL && length == name_length &&
                memcmp(symbol, name, name_length) == 0) {
                return function_id;
            }
        }
        slot = (slot + 1U) & (index->capacity - 1U);
    }
    /* Corrupt/inconsistent table: caller conservatively uses linear lookup. */
    return MINIC_FUNCTION_INVALID;
}

'''
old="static MinicFunctionId minic_core_named_function_id(\n    const MinicC0Program *program, const char *name, size_t name_length) {"
new="static MinicFunctionId minic_core_named_function_id(\n    const MinicC0Program *program, const MinicCoreNameIndex *index,\n    const char *name, size_t name_length) {"
once(old, helper+new)
needle="""    if (program == NULL || name == NULL || name_length == 0U) {
        return MINIC_FUNCTION_INVALID;
    }
    for (function_index = 0U; function_index < program->function_count; ++function_index) {"""
replacement="""    if (program == NULL || name == NULL || name_length == 0U) {
        return MINIC_FUNCTION_INVALID;
    }
    if (index != NULL && index->slots != NULL) {
        /* Valid immutable index: absence is definitive. */
        return minic_core_name_index_lookup(program, index, name, name_length);
    }
    for (function_index = 0U; function_index < program->function_count; ++function_index) {"""
once(needle,replacement)
once("""static bool minic_enqueue_core_named_function(
    const MinicC0Program *program,
    const char *name,""",
"""static bool minic_enqueue_core_named_function(
    const MinicC0Program *program,
    const MinicCoreNameIndex *name_index,
    const char *name,""")
once("    target = minic_core_named_function_id(program, name, name_length);",
     "    target = minic_core_named_function_id(program, name_index, name, name_length);")
once("""static bool minic_walk_reachable_core_edges(const MinicC0Program *program,
                                            const MinicCoreFunction *core,""",
"""static bool minic_walk_reachable_core_edges(const MinicC0Program *program,
                                            const MinicCoreNameIndex *name_index,
                                            const MinicCoreFunction *core,""")
old_call="minic_enqueue_core_named_function(program,\n"
if s.count(old_call)!=2:
    raise SystemExit("CORE_NAME_INDEX_V1: expected two call/address callsites, got %d" % s.count(old_call))
s=s.replace(old_call, "minic_enqueue_core_named_function(program,\n                                                       name_index,\n")
# Apply the last two modifications ONLY within the final pruning function.
start=s.index("static bool minic_prune_inline_specializations_from_core(")
end=s.index("static bool minic_validate_core_functions(",start)
head, pruner, tail = s[:start],s[start:end],s[end:]
old_decl="    bool *noncore_roots = NULL;\n"
if pruner.count(old_decl)!=1: raise SystemExit("bad pruner declaration")
pruner=pruner.replace(old_decl,old_decl+"    MinicCoreNameIndex name_index = {NULL, 0U};\n",1)
old_build="    noncore_roots = minic_build_noncore_root_index(program);\n"
if pruner.count(old_build)!=1: raise SystemExit("bad index build insertion")
pruner=pruner.replace(old_build,old_build+
  "    /* Allocation failure deliberately falls back to the original name scan. */\n"
  "    (void)minic_core_name_index_build(program, &name_index);\n",1)
old_walk="minic_walk_reachable_core_edges(program,\n"
if pruner.count(old_walk)!=1: raise SystemExit("bad reachability callsite")
pruner=pruner.replace(old_walk,old_walk+"                                             &name_index,\n",1)
old_free="done:\n    free(noncore_roots);\n"
if pruner.count(old_free)!=1: raise SystemExit("bad cleanup anchor")
pruner=pruner.replace(old_free,"done:\n    free(name_index.slots);\n    free(noncore_roots);\n",1)
s=head+pruner+tail
p.write_text(s)
print("MINIC_PERF_CORE_REACHABILITY_NAME_INDEX_V1=APPLIED")
