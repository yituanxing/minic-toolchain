#ifndef MINIC_CORE_CORE_LOWER_H
#define MINIC_CORE_CORE_LOWER_H

#include "core/core_ir.h"
#include "frontend/function_body.h"

bool minic_core_lower_function(const MinicFunctionBodyView *body, MinicCoreFunction *output);

#endif
