#include "target/riscv32/rv32_internal.h"
#include <stdint.h>
#include <stdio.h>

/* The live ILP32 frame->typed MachineIR handoff: ABI geometry and every
 * physical slot owner are fixed before machine instruction selection.
 * Retired FILE* stack-transport helpers must not be linked into this test. */
int main(void) {
    Rv32Frame frame = {0};
    MinicRiscvPhysicalFrame machine = {0};
    size_t offsets[] = {16U, SIZE_MAX, 60U};
    size_t objects[] = {24U};

    frame.isa.xlen = 32U;
    frame.isa.abi = MINIC_RISCV_ABI_ILP32;
    frame.frame_size = 64U;
    frame.stack_alignment = 16U;
    frame.outgoing_size = 16U;
    frame.value_count = 3U;
    frame.value_offsets = offsets;
    frame.object_count = 1U;
    frame.object_offsets = objects;
    frame.abi.named_gpr_words = 4U;
    frame.abi.named_stack_words = 3U;
    if (!rv32_machine_physical_frame(&frame, &machine) ||
        machine.frame_size != 64U || machine.stack_alignment != 16U ||
        machine.outgoing_size != 16U || machine.return_address_offset != 16U ||
        machine.first_variadic_gpr != 4U ||
        machine.named_stack_argument_slots != 3U ||
        machine.has_dynamic_stack_alignment || machine.has_dynamic_stack ||
        machine.has_hidden_result_pointer || !machine.saves_return_address)
        return 1;

    frame.stack_alignment = 64U;
    frame.has_dynamic_stack_alignment = true;
    frame.has_dynamic_stack = true;
    frame.abi.indirect_return = true;
    frame.sret_offset = 32U;
    frame.saved_base_offset = 36U;
    frame.entry_sp_offset = 40U;
    frame.is_variadic = true;
    frame.varargs_offset = 44U;
    frame.varargs_size = 16U;
    if (!rv32_machine_physical_frame(&frame, &machine) ||
        machine.stack_alignment != 64U || !machine.has_dynamic_stack ||
        !machine.has_dynamic_stack_alignment || !machine.variadic ||
        !machine.has_hidden_result_pointer ||
        machine.hidden_result_pointer_offset != 32U ||
        machine.saved_frame_base_offset != 36U || machine.entry_sp_offset != 40U ||
        machine.varargs_offset != 44U || machine.varargs_size != 16U)
        return 2;

    if (rv32_machine_physical_frame(NULL, &machine) ||
        rv32_machine_physical_frame(&frame, NULL)) return 3;
    frame.isa.xlen = 64U;
    if (rv32_machine_physical_frame(&frame, &machine)) return 4;
    frame.isa.xlen = 32U;
    frame.frame_size = 0U;
    if (rv32_machine_physical_frame(&frame, &machine)) return 5;
    frame.frame_size = 64U;
    frame.abi.named_gpr_words = 9U;
    if (rv32_machine_physical_frame(&frame, &machine)) return 6;
    frame.abi.named_gpr_words = 4U;
    frame.has_dynamic_stack_alignment = false;
    if (rv32_machine_physical_frame(&frame, &machine)) return 7;
    frame.has_dynamic_stack_alignment = true;
    frame.outgoing_size = 128U;
    if (rv32_machine_physical_frame(&frame, &machine)) return 8;
    frame.outgoing_size = 16U;
    frame.value_offsets = NULL;
    if (rv32_machine_physical_frame(&frame, &machine)) return 9;
    frame.value_offsets = offsets;
    frame.object_offsets = NULL;
    if (rv32_machine_physical_frame(&frame, &machine)) return 10;

    puts("MINIC_RV32_FRAME_ACCESS=PASS physical_abi=2 bad_frame=8 invalid_null=2");
    return 0;
}
