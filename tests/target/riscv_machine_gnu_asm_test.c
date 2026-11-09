/* Whole-function, owned-RISC-V-MIR smoke: every RV32I/RV64I selected opcode
 * family, synthetic physical CFG, GNU serializer, illegal machine negatives.
 * No production Core-to-FILE* emitter is involved. */
#define _GNU_SOURCE
#include "target/riscv/machine_gnu_asm.h"
#include "target/riscv/machine_target_state.h"

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

static MinicMachineRegister fpr(unsigned id) {
    MinicMachineRegister reg = {0};
    reg.number = id;
    reg.width_bits = 64U;
    reg.bank = MINIC_MACHINE_REGISTER_FLOAT;
    return reg;
}

static bool setup_target(MinicTargetInfo *target, MinicDataLayout *layout,
                         unsigned xlen) {
    MinicRiscvIsa isa = {0};
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
    isa.xlen = xlen;
    isa.abi = xlen == 32U ?
        MINIC_RISCV_ABI_ILP32 : MINIC_RISCV_ABI_LP64;
    isa.strict_isa = true;
    isa.codegen_features = 0U;
    isa.features = 0U;
    return minic_riscv_target_isa_store(target, &isa);
}

static bool append_selected(MinicMachineFunction *function, size_t block,
                            uint32_t opcode, size_t jump_destination) {
    MinicMachineOpcodeContract contract = {0};
    MinicMachineInstruction ins = {0};
    MinicMachineOperand operands[MINIC_MACHINE_MAX_OPERANDS] = {{0}};
    size_t index;
    const unsigned xlen = minic_riscv_target_isa(function->target).xlen;
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
            if (opcode >= MINIC_RISCV_MACHINE_FMV_X_W &&
                opcode < MINIC_RISCV_MACHINE_LAST) {
                bool is_gpr = false;
                switch (opcode) {
                case MINIC_RISCV_MACHINE_FMV_X_W:
                case MINIC_RISCV_MACHINE_FMV_X_D:
                case MINIC_RISCV_MACHINE_FEQ_D:
                case MINIC_RISCV_MACHINE_FLT_D:
                case MINIC_RISCV_MACHINE_FLE_D:
                case MINIC_RISCV_MACHINE_FCVT_W_D_RTZ:
                case MINIC_RISCV_MACHINE_FCVT_WU_D_RTZ:
                case MINIC_RISCV_MACHINE_FCVT_L_D_RTZ:
                case MINIC_RISCV_MACHINE_FCVT_LU_D_RTZ:
                    is_gpr = index == 0U;
                    break;
                case MINIC_RISCV_MACHINE_FMV_W_X:
                case MINIC_RISCV_MACHINE_FMV_D_X:
                case MINIC_RISCV_MACHINE_FCVT_D_W:
                case MINIC_RISCV_MACHINE_FCVT_D_WU:
                case MINIC_RISCV_MACHINE_FCVT_D_L:
                case MINIC_RISCV_MACHINE_FCVT_D_LU:
                    is_gpr = index == 1U;
                    break;
                default:
                    break;
                }
                if (!is_gpr)
                    op->value.physical.reg = fpr(
                        index == 0U ? 0U : index == 1U ? 1U : 2U);
            }
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
            "riscv_machine_smoke", minic_riscv_target_isa(target).codegen_features))
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
    if (opcode == MINIC_RISCV_MACHINE_JALR_BLOCK &&
        !append_selected(function, entry,
                         MINIC_RISCV_MACHINE_LA_BLOCK, exit_block))
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
    char *buffer = NULL;
    size_t size = 0U;
    bool okay;
    if (!minic_machine_function_verify(fn, &policy->verify)) return false;
    /* Build farms need not have writable /tmp. The stream is an in-memory
     * GNU assembly buffer, so this test never depends on a temp filesystem. */
    output = open_memstream(&buffer, &size);
    if (output == NULL) return false;
    okay = minic_machine_gnu_asm_emit_body(output, fn, policy) &&
        fflush(output) == 0;
    if (fclose(output) != 0) okay = false;
    if (okay && buffer != NULL && size < text_capacity) {
        memcpy(text, buffer, size);
        text[size] = '\0';
    } else okay = false;
    free(buffer);
    return okay;
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
                    minic_riscv_target_isa(target).xlen, (unsigned)opcode);
        if (valid) valid = verify_and_serialize(
            &fn, policy, serialized, sizeof(serialized));
        if (!valid && fn.block_count == 3U)
            fprintf(stderr, "rv%u opcode=%u verify=%d\n",
                    minic_riscv_target_isa(target).xlen, (unsigned)opcode,
                    (int)minic_machine_function_verify(&fn, &policy->verify));
        if (valid && info.mnemonic != NULL)
            valid = strstr(serialized, info.mnemonic) != NULL;
        if (!valid)
            fprintf(stderr, "rv%u opcode=%u (%s) failed\n",
                    minic_riscv_target_isa(target).xlen, (unsigned)opcode,
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
    fn.generated_features = minic_riscv_target_isa(target).codegen_features;
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
    if (minic_riscv_target_isa(target).xlen == 32U) {
        selected->opcode = MINIC_RISCV_MACHINE_LD;
        CHECK(!minic_machine_function_verify(&fn, &policy->verify));
        selected->opcode = opcode;
    }
    minic_machine_function_destroy(&fn);

    /* Without a machine byte-layout pass, a direct B-branch may address
     * only a provably neighboring block. Longer Core edges must be split
     * into a nearby auxiliary edge and typed LA_BLOCK/JALR_BLOCK. */
    CHECK(make_function(&fn, target, MINIC_RISCV_MACHINE_BEQZ));
    {
        size_t fourth;
        CHECK(minic_machine_function_add_block(&fn, NULL, false, &fourth));
        CHECK(fourth == 3U);
        CHECK(append_selected(&fn, fourth, MINIC_RISCV_MACHINE_RET, fourth));
        CHECK(minic_machine_function_verify(&fn, &policy->verify));
        fn.blocks[0].instructions[0].operands[1].value.block_id = fourth;
        fn.blocks[0].successors[0] = fourth;
        CHECK(!minic_machine_function_verify(&fn, &policy->verify));
        fn.blocks[0].instructions[0].operands[1].value.block_id = 2U;
        fn.blocks[0].successors[0] = 2U;
        CHECK(minic_machine_function_verify(&fn, &policy->verify));
    }
    minic_machine_function_destroy(&fn);

    /* Production RV64/RV32 long branches historically use `lla; jalr`.
     * Both operations must be individually selected and independently
     * verified. The CFG target must match the LA_BLOCK address materialized
     * immediately before JALR_BLOCK; no serializer-hidden pseudo expansion. */
    CHECK(make_function(&fn, target, MINIC_RISCV_MACHINE_JALR_BLOCK));
    CHECK(verify_and_serialize(&fn, policy, serialized, sizeof(serialized)));
    CHECK(strstr(serialized, "  lla x5, .Lriscv_machine_exit\n"
                             "  jalr zero, 0(x5)\n") != NULL);
    selected = &fn.blocks[0].instructions[1];
    CHECK(selected->operand_count == 2U);
    selected->operands[0].value.physical.reg.number = 6U;
    CHECK(!minic_machine_function_verify(&fn, &policy->verify));
    selected->operands[0].value.physical.reg.number = 5U;
    CHECK(minic_machine_function_verify(&fn, &policy->verify));
    fn.blocks[0].instructions[0].operands[1].value.block_id = 1U;
    CHECK(!minic_machine_function_verify(&fn, &policy->verify));
    fn.blocks[0].instructions[0].operands[1].value.block_id = 2U;
    CHECK(minic_machine_function_verify(&fn, &policy->verify));
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
    selected->operands[2].value.integer = minic_riscv_target_isa(target).xlen;
    CHECK(!minic_machine_function_verify(&fn, &policy->verify));
    minic_machine_function_destroy(&fn);
    return true;
}

static bool run_lp64d_fp_cases(const MinicTargetInfo *target,
                             const MinicMachineGNUAsmPolicy *policy) {
    MinicMachineFunction fn = {0};
    MinicMachineInstruction *selected;
    MinicMachineRegister saved;
    MinicRiscvIsa saved_isa = minic_riscv_target_isa(target);
    MinicTargetInfo insufficient_target = *target;
    MinicRiscvIsa insufficient_isa = saved_isa;
    char serialized[8192];
    size_t tested;
    CHECK(saved_isa.abi == MINIC_RISCV_ABI_LP64D);
    CHECK(run_all_selected_opcodes(target, policy, &tested));
    CHECK(tested >= 26U);
    CHECK(make_function(&fn, target, MINIC_RISCV_MACHINE_FMV_X_D));
    CHECK(verify_and_serialize(&fn, policy, serialized, sizeof(serialized)));
    CHECK(strstr(serialized, "fmv.x.d t0, ft1") != NULL);
    selected = &fn.blocks[0].instructions[0];
    saved = selected->operands[1].value.physical.reg;
    selected->operands[1].value.physical.reg.bank =
        MINIC_MACHINE_REGISTER_GENERAL;
    CHECK(!minic_machine_function_verify(&fn, &policy->verify));
    selected->operands[1].value.physical.reg = saved;
    CHECK(minic_machine_function_verify(&fn, &policy->verify));
    selected->operands[1].value.physical.reg.number = 32U;
    CHECK(!minic_machine_function_verify(&fn, &policy->verify));
    selected->operands[1].value.physical.reg = saved;
    minic_machine_function_destroy(&fn);
    CHECK(make_function(&fn, target, MINIC_RISCV_MACHINE_FCVT_W_D_RTZ));
    CHECK(verify_and_serialize(&fn, policy, serialized, sizeof(serialized)));
    CHECK(strstr(serialized, "fcvt.w.d t0, ft1, rtz") != NULL);
    minic_machine_function_destroy(&fn);
    insufficient_isa.codegen_features = MINIC_RISCV_FEATURE_F;
    insufficient_isa.features = MINIC_RISCV_FEATURE_F;
    CHECK(minic_riscv_target_isa_store(&insufficient_target,
        &insufficient_isa));
    CHECK(!minic_riscv_machine_opcode_info(&insufficient_target,
        MINIC_RISCV_MACHINE_FADD_D,
        &(MinicRiscvMachineOpcodeInfo){0}));
    CHECK(minic_riscv_machine_opcode_info(&insufficient_target,
        MINIC_RISCV_MACHINE_FADD_S,
        &(MinicRiscvMachineOpcodeInfo){0}));
    puts("RV64 LP64D typed F/D: 26 opcodes, bank/feature/rtz checks PASS");
    return true;
}

/* Source-origin GNU asm is the only opaque machine operation allowed; its
 * template is emitted by explicit, checked physical bindings. Compiler-
 * selected instructions must use target-owned opcode identities instead. */
static bool run_bound_source_asm(const MinicTargetInfo *target,
                                const MinicMachineGNUAsmPolicy *policy) {
    MinicMachineFunction fn = {0};
    MinicMachineInstruction source = {0};
    MinicMachineOperand bindings[5] = {{0}};
    const char *template_text =
        "addi %[dst], %[src], 7\n"
        "  lr.w %[dst], %[mem]\n"
        "  addi %[dst], %[dst], %n[neg]\n"
        "  bnez %[src], %l[target]";
    size_t entry, fallthrough, destination;
    char output[8192];
    char original;
    char *saved_constraint;
    CHECK(minic_machine_function_initialize(&fn, target,
        "riscv_source_asm_smoke", minic_riscv_target_isa(target).codegen_features));
    fn.frame_size = 16U;
    fn.frame_alignment = 16U;
    CHECK(minic_machine_function_add_block(&fn, NULL, false, &entry));
    CHECK(minic_machine_function_add_block(&fn, NULL, false, &fallthrough));
    CHECK(minic_machine_function_add_block(&fn,
        ".Lriscv_asm_goto_destination", false, &destination));
    bindings[0].kind = MINIC_MACHINE_OPERAND_REGISTER;
    bindings[0].value.physical.reg = gpr(minic_riscv_target_isa(target).xlen, 5U);
    bindings[0].value.physical.access = MINIC_MACHINE_REGISTER_WRITE;
    bindings[0].asm_constraint = "=r";
    bindings[0].asm_name = "dst";
    bindings[1].kind = MINIC_MACHINE_OPERAND_REGISTER;
    bindings[1].value.physical.reg = gpr(minic_riscv_target_isa(target).xlen, 6U);
    bindings[1].value.physical.access = MINIC_MACHINE_REGISTER_READ;
    bindings[1].asm_constraint = "r";
    bindings[1].asm_name = "src";
    bindings[2].kind = MINIC_MACHINE_OPERAND_BLOCK;
    bindings[2].value.block_id = destination;
    bindings[2].asm_name = "target";
    bindings[3].kind = MINIC_MACHINE_OPERAND_ADDRESS;
    bindings[3].value.address.target_mode =
        MINIC_RISCV_MACHINE_ADDRESS_BASE_DISPLACEMENT;
    bindings[3].value.address.has_base = true;
    bindings[3].value.address.base = gpr(minic_riscv_target_isa(target).xlen, 8U);
    bindings[3].value.address.scale = 1U;
    bindings[3].asm_constraint = "+A";
    bindings[3].asm_name = "mem";
    bindings[4].kind = MINIC_MACHINE_OPERAND_INTEGER;
    bindings[4].value.integer = -7;
    bindings[4].asm_constraint = "i";
    bindings[4].asm_name = "neg";
    source.kind = MINIC_MACHINE_INSTRUCTION_SOURCE_ASM;
    source.effects = MINIC_MACHINE_EFFECT_SOURCE_ASM |
                     MINIC_MACHINE_EFFECT_BRANCH |
                     MINIC_MACHINE_EFFECT_MEMORY_READ |
                     MINIC_MACHINE_EFFECT_MEMORY_WRITE;
    source.source_asm_template = (char *)template_text;
    source.source_asm_length = strlen(template_text);
    source.source_asm_terminates_block = true;
    source.source_asm_permits_fallthrough = true;
    source.has_core_origin = true;
    source.core_block_id = 1U;
    source.core_instruction_id = 17U;
    source.operands = bindings;
    source.operand_count = 5U;
    CHECK(minic_machine_block_append_instruction(&fn, entry, &source));
    CHECK(minic_machine_block_add_successor(&fn, entry, destination));
    CHECK(minic_machine_block_add_successor(&fn, entry, fallthrough));
    CHECK(minic_machine_block_set_fallthrough(&fn, entry, fallthrough));
    CHECK(append_selected(&fn, fallthrough, MINIC_RISCV_MACHINE_J_BLOCK,
                          destination));
    CHECK(minic_machine_block_add_successor(&fn, fallthrough, destination));
    CHECK(append_selected(&fn, destination, MINIC_RISCV_MACHINE_RET,
                          destination));
    CHECK(verify_and_serialize(&fn, policy, output, sizeof(output)));
    CHECK(strstr(output, "addi x5, x6, 7\n"
                         "  lr.w x5, (x8)") != NULL);
    CHECK(strstr(output, "bnez x6, .Lriscv_asm_goto_destination") != NULL);
    fn.blocks[entry].instructions[0].effects &=
        ~MINIC_MACHINE_EFFECT_MEMORY_WRITE;
    CHECK(!minic_machine_function_verify(&fn, &policy->verify));
    fn.blocks[entry].instructions[0].effects |=
        MINIC_MACHINE_EFFECT_MEMORY_WRITE;
    CHECK(minic_machine_function_verify(&fn, &policy->verify));
    saved_constraint = fn.blocks[entry].instructions[0].operands[0].asm_constraint;
    fn.blocks[entry].instructions[0].operands[0].asm_constraint = "=m";
    CHECK(!minic_machine_function_verify(&fn, &policy->verify));
    fn.blocks[entry].instructions[0].operands[0].asm_constraint =
        saved_constraint;
    CHECK(minic_machine_function_verify(&fn, &policy->verify));
    original = fn.blocks[entry].instructions[0].source_asm_template[
        source.source_asm_length - 1U];
    fn.blocks[entry].instructions[0].source_asm_template[
        source.source_asm_length - 1U] = '%';
    CHECK(!minic_machine_function_verify(&fn, &policy->verify));
    fn.blocks[entry].instructions[0].source_asm_template[
        source.source_asm_length - 1U] = original;
    CHECK(minic_machine_function_verify(&fn, &policy->verify));
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
            policy.section_type_marker != MINIC_MACHINE_GNU_SECTION_TYPE_AT ||
            !run_all_selected_opcodes(&target, &policy, &tested) ||
            !run_negative_cases(&target, &policy) ||
            !run_bound_source_asm(&target, &policy)) return 1;
        printf("RV%uI machine GNU policy: %zu selected opcodes PASS; "
               "typed CFG, memory, PCREL/GOT, ABI, source-asm bindings "
               "and forged negatives PASS\n",
               widths[i], tested);
    }
    {
        MinicTargetInfo target;
        MinicDataLayout layout;
        MinicMachineGNUAsmPolicy policy;
        MinicRiscvIsa isa;
        if (!setup_target(&target, &layout, 64U)) return 1;
        isa = minic_riscv_target_isa(&target);
        isa.abi = MINIC_RISCV_ABI_LP64D;
        isa.features = MINIC_RISCV_FEATURE_F | MINIC_RISCV_FEATURE_D;
        isa.codegen_features = isa.features;
        if (!minic_riscv_target_isa_store(&target, &isa) ||
            !minic_riscv_machine_gnu_asm_policy(&target, &policy) ||
            !run_lp64d_fp_cases(&target, &policy)) return 1;
    }
    return 0;
}
