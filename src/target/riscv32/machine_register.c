#include "target/machine_register.h"

#include <string.h>

bool minic_riscv32_machine_fixed_register_read(const char *name, size_t length) {
    return length == 2U && (memcmp(name, "sp", 2U) == 0 ||
                            memcmp(name, "tp", 2U) == 0);
}

bool minic_riscv32_machine_local_fixed_register(const char *name, size_t length) {
    /* RV32 ILP32 local bindings are separately qualified from RV64;
     * native RV32 emission must not inherit RV64's register aliases. */
    return length == 2U && name[0] == 'a' && name[1] >= '0' && name[1] <= '7';
}
