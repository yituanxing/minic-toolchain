/* Whole-function, owned-RISC-V-MIR smoke: every RV32I/RV64I selected opcode
 * family, synthetic physical CFG, GNU serializer, illegal machine negatives.
 * No production Core-to-FILE* emitter is involved. */
#include "target/riscv/machine_gnu_asm.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "riscv machine smoke failure: %s:%d: %s\n", \
                __FILE__, __LINE__, #expr); \
        return false; \
    } \
} while (0)

static MinicMachineRegister gpr(unsigned xlen, unsigned id) {
    MinicMachineRegister reg = {0};
    reg.number = id;
    reg.width_bits = (uint16_t)xlen;
    reg.bank = MINIC_MACHINE_REGISTER_GENERAL;
    return reg;
}

static bool setup_target(MinicTargetInfo *target, MinicDataLayout *layout,
                         unsigned xlen) {
    if (target == NULL || layout == NULL || (xlen != 32U && xlen != 64U))
        return false;
    memset(target, 0, sizeof(*target));
    memset(layout, 0, sizeof(*layout));
    layout->byte_order = MINIC_BYTE_ORDER_LITTLE;
    layout->pointer_size = xlen / 8U;
    layout->pointer_alignment = xlen / 8U;
    target->architecture = xlen == 32U ?
        MINIC_TARGET_ARCH_RISCV32 : MINIC_TARGET_ARCH_RISCV64;
    target->data_layout = layout;
    target->riscv_isa.xlen = xlen;
    target->riscv_isa.abi = xlen == 32U ?
        MINIC_RISCV_ABI_ILP32 : MINIC_RISCV_ABI_LP64;
    target->riscv_isa.strict_isa = true;
    target->riscv_isa.codegen_features = 0U;
    target->riscv_isa.features = 0U;
    return true;
}

static bool append_selected(MinicMachineFunction *function, size_t block,
                            uint32_t opcode, size_t jump_destination) {
    MinicMachineOpcodeContract contract = {0};
    MinicMachineInstruction ins = {0};
    MinicMachineOperand operands[MINIC_MACHINE_MAX_OPERANDS] = {{0}};
    size_t index;
    const unsigned xlen = function->target->riscv_isa.xlen;
    if (!minic_riscv_machine_lookup_opcode_contract(
            function->target, opcode, &contract, NULL)) return false;
    ins.kind = MINIC_MACHINE_INSTRUCTION_SELECTED;
    ins.opcode = opcode;
    ins.effects = contract.required_effects;
    ins.operands = contract.operand_count != 0U ? operands : NULL;
    ins.operand_count = contract.operand_count;
    ins.implicit = contract.implicit_count != 0U ? contract.implicit : NULL;
    ins.implicit_count = contract.implicit_count;
    for (index = 0U; index < contract.operand_count; ++index) {
        MinicMachineOperand *op = &operands[index];
        op->kind = contract.operand_kinds[index];
        switch (op->kind) {
        case MINIC_MACHINE_OPERAND_REGISTER:
            op->value.physical.reg = gpr(xlen,
                index == 0U ? 5U : index == 1U ? 6U : 7U);
            op->value.physical.access = contract.register_access[index];
            break;
        case MINIC_MACHINE_OPERAND_INTEGER:
            op->value.integer = 3;
            break;
        case MINIC_MACHINE_OPERAND_ADDRESS:
            op->value.address.target_mode =
                MINIC_RISCV_MACHINE_ADDRESS_BASE_DISPLACEMENT;
            op->value.address.has_base = true;
            op->value.address.base = gpr(xlen, 2U);
            op->value.address.scale = 1U;
            op->value.address.displacement = 8;
            break;
        case MINIC_MACHINE_OPERAND_FRAME:
            op->value.frame.index = 0U;
            op->value.frame.displacement = 0;
            break;
        case MINIC_MACHINE_OPERAND_BLOCK:
        case MINIC_MACHINE_OPERAND_BLOCK_ADDRESS:
            op->value.block_id = jump_destination;
            break;
        case MINIC_MACHINE_OPERAND_SYMBOL:
            op->value.symbol.name = "minic_riscv_external_symbol";
            op->value.symbol.relocation =
                opcode == MINIC_RISCV_MACHINE_LA_GOT
                    ? MINIC_RISCV_MACHINE_RELOC_GOT
                    : opcode == MINIC_RISCV_MACHINE_LA_PCREL
                    ? MINIC_RISCV_MACHINE_RELOC_PCREL_LOCAL
                    : MINIC_RISCV_MACHINE_RELOC_CALL;
            break;
        default:
            return false;
        }
    }
    return minic_machine_block_append_instruction(function, block, &ins);
}

static bool make_function(MinicMachineFunction *function,
                          const MinicTargetInfo *target,
                          uint32_t opcode) {
    MinicMachineOpcodeContract contract = {0};
    MinicMachineFrameObject frame = {0};
    size_t entry, fallback, exit_block, frame_id;
    size_t index;
    bool has_explicit_block = false;
    bool branch_terminal;
    if (!minic_machine_function_initialize(function, target,
            "riscv_machine_smoke", target->riscv_isa.codegen_features))
        return false;
    function->frame_size = 32U;
    function->frame_alignment = 16U;
    frame.size_bytes = 8U;
    frame.alignment_bytes = 8U;
    frame.area = MINIC_MACHINE_FRAME_LOCAL;
    frame.base = MINIC_MACHINE_FRAME_BASE_STACK_POINTER;
    frame.has_fixed_offset = true;
    frame.fixed_offset = 8;
    if (!minic_machine_function_add_frame_object(function, &frame, &frame_id) ||
        frame_id != 0U ||
        !minic_machine_function_add_block(function, NULL, false, &entry) ||
        !minic_machine_function_add_block(function, NULL, false, &fallback) ||
        !minic_machine_function_add_block(function,
            ".Lriscv_machine_exit", true, &exit_block) ||
        entry != 0U || fallback != 1U || exit_block != 2U ||
        !minic_riscv_machine_lookup_opcode_contract(
            target, opcode, &contract, NULL))
        return false;
    if (!append_selected(function, entry, opcode, exit_block)) return false;
    branch_terminal = contract.terminates_block;
    for (index = 0U; index < contract.operand_count; ++index)
        if (contract.operand_kinds[index] == MINIC_MACHINE_OPERAND_BLOCK &&
            opcode != MINIC_RISCV_MACHINE_LA_BLOCK)
            has_explicit_block = true;
    if (branch_terminal) {
        if (has_explicit_block &&
            !minic_machine_block_add_successor(function, entry, exit_block))
            return false;
        if (contract.has_indirect_successors &&
            !minic_machine_block_add_successor(function, entry, exit_block))
            return false;
        if (contract.permits_fallthrough &&
            (!minic_machine_block_add_successor(function, entry, fallback) ||
             !minic_machine_block_set_fallthrough(function, entry, fallback)))
            return false;
    } else if (!append_selected(function, entry,
                                MINIC_RISCV_MACHINE_J_BLOCK, exit_block) ||
               !minic_machine_block_add_successor(function, entry, exit_block)) {
        return false;
    }
    return append_selected(function, fallback,
                           MINIC_RISCV_MACHINE_J_BLOCK, exit_block) &&
           minic_machine_block_add_successor(function, fallback, exit_block) &&
           append_selected(function, exit_block,
                           MINIC_RISCV_MACHINE_RET, exit_block);
}

static bool verify_and_serialize(const MinicMachineFunction *fn,
                                  const MinicMachineGNUAsmPolicy *policy,
                                  char *text, size_t text_capacity) {
    FILE *output;
    long size;
    size_t read_size;
    if (!minic_machine_function_verify(fn, &policy->verify)) return false;
    output = tmpfile();
    if (output == NULL) return false;
    if (!minic_machine_gnu_asm_emit_body(output, fn, policy) ||
        fflush(output) != 0 || fseek(output, 0, SEEK_END) != 0 ||
        (size = ftell(output)) < 0 ||
        (size_t)size >= text_capacity ||
        fseek(output, 0, SEEK_SET) != 0) {
        fclose(output);
        return false;
    }
    read_size = fread(text, 1U, (size_t)size, output);
    fclose(output);
    if (read_size != (size_t)size) return false;
    text[read_size] = '\0';
    return true;
}

static bool run_all_selected_opcodes(const MinicTargetInfo *target,
                                      const MinicMachineGNUAsmPolicy *policy,
                                      size_t *tested) {
    uint32_t opcode;
    char serialized[8192];
    *tested = 0U;
    for (opcode = MINIC_RISCV_MACHINE_INVALID + 1U;
         opcode < MINIC_RISCV_MACHINE_LAST; ++opcode) {
        MinicMachineFunction fn = {0};
        MinicRiscvMachineOpcodeInfo info;
        bool valid;
        if (!minic_riscv_machine_opcode_info(target, opcode, &info))
            continue; /* RV64I W opcodes must reject on RV32I. */
        valid = make_function(&fn, target, opcode);
        if (!valid)
            fprintf(stderr, "rv%u opcode=%u make_function rejected\n",
                    target->riscv_isa.xlen, (unsigned)opcode);
        if (valid) valid = verify_and_serialize(
            &fn, policy, serialized, sizeof(serialized));
        if (!valid && fn.block_count == 3U)
            fprintf(stderr, "rv%u opcode=%u verify=%d\n",
                    target->riscv_isa.xlen, (unsigned)opcode,
                    (int)minic_machine_function_verify(&fn, &policy->verify));
        if (valid && info.mnemonic != NULL)
            valid = strstr(serialized, info.mnemonic) != NULL;
        if (!valid)
            fprintf(stderr, "rv%u opcode=%u (%s) failed\n",
                    target->riscv_isa.xlen, (unsigned)opcode,
                    info.mnemonic != NULL ? info.mnemonic : "pseudo");
        minic_machine_function_destroy(&fn);
        if (!valid) return false;
        ++*tested;
    }
    return true;
}

static bool run_negative_cases(const MinicTargetInfo *target,
                               const MinicMachineGNUAsmPolicy *policy) {
    MinicMachineFunction fn = {0};
    MinicMachineInstruction *selected;
    char serialized[8192];
    uint32_t opcode = MINIC_RISCV_MACHINE_ADDI;
    CHECK(make_function(&fn, target, opcode));
    CHECK(verify_and_serialize(&fn, policy, serialized, sizeof(serialized)));
    selected = &fn.blocks[0].instructions[0];
    selected->operands[2].value.integer = 4096;
    CHECK(!minic_machine_function_verify(&fn, &policy->verify));
    selected->operands[2].value.integer = 3;
    CHECK(minic_machine_function_verify(&fn, &policy->verify));
    fn.generated_features = UINT64_C(1) << 31;
    CHECK(!minic_machine_function_verify(&fn, &policy->verify));
    fn.generated_features = target->riscv_isa.codegen_features;
    fn.calling_convention = MINIC_CALLING_CONVENTION_MS_X64;
    CHECK(!minic_machine_function_verify(&fn, &policy->verify));
    fn.calling_convention = MINIC_CALLING_CONVENTION_DEFAULT;
    CHECK(minic_machine_function_verify(&fn, &policy->verify));
    fn.frame_objects[0].area = MINIC_MACHINE_FRAME_OUTGOING;
    fn.frame_objects[0].fixed_offset = -8;
    CHECK(!minic_machine_function_verify(&fn, &policy->verify));
    fn.frame_objects[0].area = MINIC_MACHINE_FRAME_LOCAL;
    fn.frame_objects[0].fixed_offset = 8;
    CHECK(minic_machine_function_verify(&fn, &policy->verify));
    if (target->riscv_isa.xlen == 32U) {
        selected->opcode = MINIC_RISCV_MACHINE_LD;
        CHECK(!minic_machine_function_verify(&fn, &policy->verify));
        selected->opcode = opcode;
    }
    minic_machine_function_destroy(&fn);

    CHECK(make_function(&fn, target, MINIC_RISCV_MACHINE_LA_PCREL));
    selected = &fn.blocks[0].instructions[0];
    selected->operands[1].value.symbol.relocation =
        MINIC_RISCV_MACHINE_RELOC_GOT;
    CHECK(!minic_machine_function_verify(&fn, &policy->verify));
    selected->operands[1].value.symbol.relocation =
        MINIC_RISCV_MACHINE_RELOC_PCREL_LOCAL;
    CHECK(minic_machine_function_verify(&fn, &policy->verify));
    minic_machine_function_destroy(&fn);

    CHECK(make_function(&fn, target, MINIC_RISCV_MACHINE_LW));
    selected = &fn.blocks[0].instructions[0];
    selected->operands[1].value.address.displacement = 4096;
    CHECK(!minic_machine_function_verify(&fn, &policy->verify));
    selected->operands[1].value.address.displacement = 8;
    CHECK(minic_machine_function_verify(&fn, &policy->verify));
    minic_machine_function_destroy(&fn);

    CHECK(make_function(&fn, target, MINIC_RISCV_MACHINE_LW_FRAME));
    selected = &fn.blocks[0].instructions[0];
    CHECK(verify_and_serialize(&fn, policy, serialized, sizeof(serialized)));
    CHECK(strstr(serialized, "lw x5, 8(x2)") != NULL);
    selected->operands[1].value.frame.displacement = 3000;
    CHECK(!minic_machine_function_verify(&fn, &policy->verify));
    selected->operands[1].value.frame.displacement = 0;
    fn.has_dynamic_stack = true;
    fn.frame_objects[0].base = MINIC_MACHINE_FRAME_BASE_FRAME_POINTER;
    CHECK(verify_and_serialize(&fn, policy, serialized, sizeof(serialized)));
    CHECK(strstr(serialized, "lw x5, 8(x8)") != NULL);
    fn.frame_objects[0].area = MINIC_MACHINE_FRAME_INCOMING;
    fn.frame_objects[0].base = MINIC_MACHINE_FRAME_BASE_ENTRY_STACK_POINTER;
    CHECK(!minic_machine_function_verify(&fn, &policy->verify));
    minic_machine_function_destroy(&fn);

    CHECK(make_function(&fn, target, MINIC_RISCV_MACHINE_CALL_DIRECT));
    selected = &fn.blocks[0].instructions[0];
    CHECK(selected->implicit_count != 0U);
    --selected->implicit_count;
    CHECK(!minic_machine_function_verify(&fn, &policy->verify));
    ++selected->implicit_count;
    CHECK(minic_machine_function_verify(&fn, &policy->verify));
    minic_machine_function_destroy(&fn);

    CHECK(make_function(&fn, target, MINIC_RISCV_MACHINE_SLLI));
    selected = &fn.blocks[0].instructions[0];
    selected->operands[2].value.integer = target->riscv_isa.xlen;
    CHECK(!minic_machine_function_verify(&fn, &policy->verify));
    minic_machine_function_destroy(&fn);
    return true;
}

int main(void) {
    unsigned widths[] = {32U, 64U};
    size_t i;
    for (i = 0U; i < sizeof(widths) / sizeof(widths[0]); ++i) {
        MinicTargetInfo target;
        MinicDataLayout layout;
        MinicMachineGNUAsmPolicy policy;
        size_t tested;
        if (!setup_target(&target, &layout, widths[i]) ||
            !minic_riscv_machine_gnu_asm_policy(&target, &policy) ||
            !run_all_selected_opcodes(&target, &policy, &tested) ||
            !run_negative_cases(&target, &policy)) return 1;
        printf("RV%uI machine GNU policy: %zu selected opcodes PASS; "
               "typed CFG, memory, PCREL/GOT, ABI and forged negatives PASS\n",
               widths[i], tested);
    }
    return 0;
}
