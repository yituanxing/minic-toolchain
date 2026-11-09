#include "target/common/machine_ir.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The first union alternative (physical) occupies only 16 bytes, while the
 * address and symbol alternatives carry fields beyond those first bytes.
 * Exercise the exact production MachineIR storage and its by-value return. */
static MinicMachineOperand address_operand(unsigned base) {
    MinicMachineOperand result;
    (void)memset(&result, 0, sizeof(result));
    result.kind = MINIC_MACHINE_OPERAND_ADDRESS;
    result.value.address.has_base = true;
    result.value.address.base.number = base;
    result.value.address.scale = 1U;
    result.value.address.displacement = -56;
    return result;
}

static MinicMachineOperand symbol_operand(void) {
    MinicMachineOperand result;
    (void)memset(&result, 0, sizeof(result));
    result.kind = MINIC_MACHINE_OPERAND_SYMBOL;
    result.value.symbol.name = "sample_symbol";
    result.value.symbol.relocation = 3U;
    return result;
}

static int check_address(unsigned base) {
    MinicMachineOperand operand = address_operand(base);
    return operand.kind == MINIC_MACHINE_OPERAND_ADDRESS &&
        operand.value.address.has_base &&
        operand.value.address.base.number == base &&
        operand.value.address.scale == 1U &&
        operand.value.address.displacement == -56 &&
        !operand.value.address.has_symbol &&
        operand.value.address.symbol == NULL &&
        operand.value.address.relocation == 0U &&
        operand.value.address.addend == 0;
}

int main(void) {
    MinicMachineOperand symbol = symbol_operand();
#if __SIZEOF_POINTER__ == 8
    if (sizeof(MinicMachineOperand) != 112U ||
        offsetof(MinicMachineOperand, value.address.has_symbol) != 64U ||
        offsetof(MinicMachineOperand, value.address.symbol) != 72U) return 3;
#endif
    if (!check_address(29U) || !check_address(31U)) return 1;
    if (symbol.kind != MINIC_MACHINE_OPERAND_SYMBOL ||
        symbol.value.symbol.name == NULL ||
        symbol.value.symbol.name[0] != 's' ||
        symbol.value.symbol.relocation != 3U ||
        symbol.value.symbol.addend != 0) return 2;
    return 0;
}
