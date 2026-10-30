#ifndef MINIC_COMPILER_CORE_REACHABILITY_H
#define MINIC_COMPILER_CORE_REACHABILITY_H
#include "compiler/core_function_set.h"
#include "frontend/ast.h"
#include <stdbool.h>

/* Whole-program Core CFG traversal after lowering: source labels and
 * indirect branches are conservative roots; global relocations and
 * function aliases remain non-Core roots. Fail closed on bad Core IDs. */
bool minic_prune_inline_specializations_from_core(
    MinicC0Program *program, const MinicCoreFunctionSet *set,
    bool emit_reachable_core_blocks_only);
bool minic_prune_dead_file_scope_static_objects_from_core(
    MinicC0Program *program, const MinicCoreFunctionSet *set,
    bool emit_reachable_core_blocks_only);
#endif
