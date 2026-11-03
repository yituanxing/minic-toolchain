#include "target/machine_register.h"
#include "target/riscv/register_identity.h"

#include <string.h>

bool minic_riscv64_machine_fixed_register_read(const char *name, size_t length) {
    return length == 2U && (memcmp(name, "sp", 2U) == 0 ||
                            memcmp(name, "tp", 2U) == 0);
}

bool minic_riscv64_machine_local_fixed_register(const char *name, size_t length) {
    unsigned int physical_id;
    return minic_riscv_gpr_id(name, length, &physical_id) &&
           physical_id >= 10U && physical_id <= 17U;
}
