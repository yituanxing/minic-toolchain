#ifndef MINIC_TARGET_RISCV64_LEGALIZE_H
#define MINIC_TARGET_RISCV64_LEGALIZE_H
#include "core/core_ir.h"
#include "frontend/ast.h"
#include <stdbool.h>
#include <stddef.h>

/* Checked RV64 machine-lowering admissibility: Core instructions, scalar
 * widths, aggregate operations, overflow arithmetic and call psABI.
 * A shared frontend/Core semantic invariant is NOT an ISA capability.
 * Never rewrite an unsupported operation as successful lowering. */
bool minic_riscv64_core_integer_type_is_signed(const MinicCoreFunction *function,
                                        MinicType type,
                                        bool *is_signed);

bool minic_riscv64_core_unsigned_integer_width(const MinicC0Program *program,
                                        const MinicCoreFunction *function,
                                        MinicCoreValueId value_id,
                                        unsigned int *width);

bool minic_riscv64_core_field_address_supported(const MinicCoreInstruction *instruction,
                                         size_t *field_offset);

bool minic_riscv64_core_record_load_supported(const MinicC0Program *program,
                                       const MinicCoreFunction *function,
                                       const MinicCoreInstruction *instruction,
                                       size_t *record_size);

bool minic_riscv64_core_record_copy_supported(const MinicC0Program *program,
                                       const MinicCoreFunction *function,
                                       const MinicCoreInstruction *instruction);

bool minic_riscv64_core_call_frame_address_supported(
    const MinicCoreInstruction *instruction);

bool minic_riscv64_core_scalar_bitcast_supported(const MinicC0Program *program,
                                          const MinicCoreFunction *function,
                                          const MinicCoreInstruction *instruction);

bool minic_riscv64_core_integer_type_range_fits(const MinicC0Program *program,
                                         const MinicCoreFunction *function,
                                         MinicType source_type,
                                         MinicType result_type);

bool minic_riscv64_core_integer_overflow_xlen_scratch_exact(
    const MinicC0Program *program,
    MinicType left_type,
    MinicType right_type,
    size_t result_size);

bool minic_riscv64_core_integer_overflow_supported(const MinicC0Program *program,
                                            const MinicCoreFunction *function,
                                            const MinicCoreInstruction *instruction,
                                            MinicType *result_type,
                                            size_t *result_size,
                                            bool *is_unsigned);

bool minic_riscv64_core_direct_call_supported(const MinicC0Program *program,
                                       const MinicCoreFunction *function,
                                       const MinicCoreInstruction *instruction);

bool minic_riscv64_core_indirect_call_supported(const MinicC0Program *program,
                                         const MinicCoreFunction *function,
                                         const MinicCoreInstruction *instruction);

#endif
