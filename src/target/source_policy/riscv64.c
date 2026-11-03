#include "target/source_policy/source_policy.h"
#include "target/machine_register.h"
#include "target/riscv/register_identity.h"

static bool rv64_register_clobber(const char *name, size_t length) {
    unsigned int physical_id;
    return minic_riscv_gpr_id(name, length, &physical_id) &&
           minic_riscv_gpr_caller_saved(physical_id);
}

static bool rv64_immediate(const char *constraint, size_t length, int64_t value) {
    return length == 1U && constraint[0] == 'I' && value >= -2048 && value <= 2047;
}

const MinicTargetAsmSourcePolicy minic_riscv64_asm_source_policy = {
    .fixed_register_read = minic_riscv64_machine_fixed_register_read,
    .local_fixed_register = minic_riscv64_machine_local_fixed_register,
    .register_clobber = rv64_register_clobber,
    .immediate_constraint = rv64_immediate,
    .immediate_diagnostic = "GNU asm 'I' input requires a signed 12-bit integer constant",
};
