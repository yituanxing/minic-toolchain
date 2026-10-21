#ifndef MINIC_CORE_IR_INTERNAL_H
#define MINIC_CORE_IR_INTERNAL_H

#include "core/core_ir.h"

/* Shared Core IR type-admission rules: construction and verification must
 * accept exactly the same signatures and global-symbol shapes. These are
 * private to Core and do not encode any target ABI or machine register. */
bool core_global_addressable_type(MinicType type);
bool core_call_scalar_type(MinicType type);
bool core_call_parameter_type(MinicType type);
bool core_direct_call_return_type(MinicType type);

#endif
