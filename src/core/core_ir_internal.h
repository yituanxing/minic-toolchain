#ifndef MINIC_CORE_IR_INTERNAL_H
#define MINIC_CORE_IR_INTERNAL_H

#include "core/core_ir.h"

/* Shared Core IR type-admission rules: construction and verification must
 * accept exactly the same signatures and global-symbol shapes. These are
 * private to Core and do not encode any target ABI or machine register. */
bool minic_core_ir_global_addressable_type(MinicType type);
bool minic_core_ir_call_scalar_type(MinicType type);
bool minic_core_ir_call_parameter_type(MinicType type);
bool minic_core_ir_direct_call_return_type(MinicType type);

/* C-language ingress preflight: validates ownership/array/index safety before
 * the source descriptors are imported into the CoreType arena. Full semantic
 * verification runs on the sealed, CoreType-backed function after import. */
bool minic_core_function_verify_source_ingress(const MinicCoreFunction *function);

/* Source-ingress-only C descriptor for a lowered SSA value. The single
 * canonical source type lives on its defining instruction; sealed Core
 * consumers must use the CoreType snapshot instead. Invalid IDs fail closed. */
MinicType minic_core_function_source_value_type(const MinicCoreFunction *function,
                                                MinicCoreValueId value_id);

#endif
