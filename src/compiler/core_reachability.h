#ifndef MINIC_COMPILER_CORE_REACHABILITY_H
#define MINIC_COMPILER_CORE_REACHABILITY_H
#include "compiler/core_function_set.h"
#include <stdbool.h>

/* Whole-program Core CFG traversal after lowering: source labels and
 * indirect branches are conservative roots; global relocations and
 * function aliases remain non-Core roots. Fail closed on bad Core IDs. */
bool minic_core_function_set_close_reachability(
    MinicCoreFunctionSet *set, bool emit_reachable_core_blocks_only);
#endif
