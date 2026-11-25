#include "target/machine_register.h"
#include "target/riscv/register_identity.h"

bool minic_riscv_machine_fixed_register_read(const char *name, size_t length) {
    return length == 2U && (memcmp(name, "sp", 2U) == 0 ||
                            memcmp(name, "tp", 2U) == 0);
}

bool minic_riscv32_machine_local_fixed_register(const char *name, size_t length) {
    /* RV32 ILP32 local bindings are separately qualified from RV64;
     * native RV32 emission must not inherit RV64's register aliases. */
    return length == 2U && name[0] == 'a' && name[1] >= '0' && name[1] <= '7';
}

bool minic_riscv64_machine_local_fixed_register(const char *name, size_t length) {
    unsigned int physical_id;
    return minic_riscv_gpr_id(name, length, &physical_id) &&
           physical_id >= 10U && physical_id <= 17U;
}
