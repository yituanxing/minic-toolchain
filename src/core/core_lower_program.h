#ifndef MINIC_CORE_CORE_LOWER_PROGRAM_H
#define MINIC_CORE_CORE_LOWER_PROGRAM_H

#include "core/core_lower.h"
#include "frontend/ast.h"

#include <stdbool.h>
#include <stddef.h>

/*
 * Retained whole-program view of function-level Core lowering.
 * Core lowering remains function-granular: entries map 1:1 to source program
 * function indices, and statuses are meaningful for defined functions only.
 * Successful entries own their MinicCoreFunction until this object is destroyed.
 *
 * 这是 compiler pipeline 持有的 Core lowering 结果，不是新的 target IR。
 * 它让 shadow validation 与后续 backend selection 复用同一次 lowering。
 */
typedef struct MinicCoreLoweredProgram {
    MinicCoreFunction *functions;
    MinicCoreLowerStatus *statuses;
    size_t function_count;
} MinicCoreLoweredProgram;

void minic_core_lowered_program_initialize(MinicCoreLoweredProgram *program);
void minic_core_lowered_program_destroy(MinicCoreLoweredProgram *program);

bool minic_core_lower_program(const MinicC0Program *source, MinicCoreLoweredProgram *output);

#endif
