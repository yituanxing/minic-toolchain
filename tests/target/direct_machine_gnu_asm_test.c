#define _POSIX_C_SOURCE 200809L
/* Two LP64 target-owned native opcode/ABI contracts, with actual owned
 * MachineFunction -> independent verifier -> GNU machine body serialization.
 * No machine assembly string or FILE* trampoline is stored in MIR. */
#include "target/aarch64/machine_opcode.h"
#include "target/loongarch64/machine_opcode.h"
#include "target/loongarch64/machine_target_state.h"
#include "target/common/machine_ir.h"
#include "target/common/machine_ir_verify.h"
#include "target/common/machine_gnu_asm.h"
#include "target/data_layout.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static FILE *open_test_output(void) {
    const char *directory = getenv("TMPDIR");
    char path[512];
    int fd;
    FILE *file;
    if (directory == NULL || *directory == '\0') directory = "/tmp";
    if (snprintf(path, sizeof(path), "%s/minic-native-asm-XXXXXX",
            directory) >= (int)sizeof(path)) return NULL;
    fd = mkstemp(path);
    if (fd < 0) return NULL;
    unlink(path);
    file = fdopen(fd, "w+b");
    if (file == NULL) close(fd);
    return file;
}

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "machine native contract:%d: %s\n", __LINE__, #expr); \
    return false; } } while (0)

static MinicMachineOperand reg(unsigned int number, unsigned int width,
                              unsigned int access) {
    MinicMachineOperand result = {0};
    result.kind = MINIC_MACHINE_OPERAND_REGISTER;
    result.value.physical.reg = (MinicMachineRegister){number,
        (uint16_t)width, MINIC_MACHINE_REGISTER_GENERAL};
    result.value.physical.access = access;
    return result;
}

static MinicMachineOperand fpreg(unsigned int number, unsigned int width,
    MinicMachineRegisterBank bank, unsigned int access) {
    MinicMachineOperand result = reg(number, width, access);
    result.value.physical.reg.bank = bank;
    return result;
}

static MinicMachineOperand imm(int64_t value) {
    MinicMachineOperand result = {0};
    result.kind = MINIC_MACHINE_OPERAND_INTEGER;
    result.value.integer = value;
    return result;
}

static MinicMachineOperand block(size_t block_id) {
    MinicMachineOperand result = {0};
    result.kind = MINIC_MACHINE_OPERAND_BLOCK;
    result.value.block_id = block_id;
    return result;
}

static MinicMachineOperand frame(size_t index) {
    MinicMachineOperand result = {0};
    result.kind = MINIC_MACHINE_OPERAND_FRAME;
    result.value.frame.index = index;
    return result;
}

static MinicMachineOperand address(unsigned int base, uint32_t mode,
                                   int64_t displacement) {
    MinicMachineOperand result = {0};
    result.kind = MINIC_MACHINE_OPERAND_ADDRESS;
    result.value.address.target_mode = mode;
    result.value.address.has_base = true;
    result.value.address.base = (MinicMachineRegister){base, 64U,
        MINIC_MACHINE_REGISTER_GENERAL};
    result.value.address.scale = 1U;
    result.value.address.displacement = displacement;
    return result;
}

static MinicMachineOperand symbol(char *name, uint32_t relocation) {
    MinicMachineOperand result = {0};
    result.kind = MINIC_MACHINE_OPERAND_SYMBOL;
    result.value.symbol.name = name;
    result.value.symbol.relocation = relocation;
    return result;
}

static bool append(MinicMachineFunction *function,
    const MinicMachineGNUAsmPolicy *policy, size_t block_id,
    uint32_t opcode, unsigned int effects,
    MinicMachineOperand *operands, size_t count) {
    MinicMachineInstruction instruction = {0};
    MinicMachineOpcodeContract contract = {0};
    CHECK(policy->verify.lookup_opcode(function->target, opcode,
        &contract, policy->verify.context));
    CHECK(count == contract.operand_count);
    instruction.kind = MINIC_MACHINE_INSTRUCTION_SELECTED;
    instruction.opcode = opcode;
    instruction.effects = effects;
    instruction.operands = operands;
    instruction.operand_count = count;
    instruction.implicit = contract.implicit_count != 0U
        ? contract.implicit : NULL;
    instruction.implicit_count = contract.implicit_count;
    return minic_machine_block_append_instruction(function, block_id,
        &instruction);
}

static bool emitted_contains(const MinicMachineFunction *function,
    const MinicMachineGNUAsmPolicy *policy, const char *first,
    const char *second, const char *third) {
    FILE *output = open_test_output();
    char buffer[8192];
    size_t count;
    CHECK(output != NULL);
    if (!minic_machine_gnu_asm_emit_body(output, function, policy)) {
        fclose(output);
        return false;
    }
    CHECK(fflush(output) == 0);
    CHECK(fseek(output, 0, SEEK_SET) == 0);
    count = fread(buffer, 1U, sizeof(buffer) - 1U, output);
    buffer[count] = '\0';
    CHECK(fclose(output) == 0);
    CHECK(strstr(buffer, first) != NULL);
    CHECK(strstr(buffer, second) != NULL);
    CHECK(strstr(buffer, third) != NULL);
    return true;
}

static bool run_a64(void) {
    MinicTargetInfo target = {0};
    MinicMachineGNUAsmPolicy policy = {0};
    MinicMachineFunction function = {0};
    size_t entry, fallthrough, taken, frame_id;
    MinicMachineOperand operands[3];
    MinicMachineFrameObject object = {0};
    unsigned int rw = MINIC_MACHINE_REGISTER_READ;
    unsigned int wr = MINIC_MACHINE_REGISTER_WRITE;
    target.architecture = MINIC_TARGET_ARCH_AARCH64;
    target.data_layout = minic_aarch64_linux_data_layout();
    CHECK(minic_aarch64_machine_gnu_asm_policy_initialize(&target, &policy));
    CHECK(policy.section_type_marker == MINIC_MACHINE_GNU_SECTION_TYPE_PERCENT);
    CHECK(minic_machine_function_initialize(&function, &target, "a64_probe",
        MINIC_A64_MACHINE_FEATURE_BASE | MINIC_A64_MACHINE_FEATURE_FP_SIMD));
    function.frame_alignment = 16U;
    function.frame_size = 32U;
    object = (MinicMachineFrameObject){.area = MINIC_MACHINE_FRAME_LOCAL,
        .base = MINIC_MACHINE_FRAME_BASE_STACK_POINTER,
        .size_bytes = 8U, .alignment_bytes = 8U,
        .has_fixed_offset = true, .fixed_offset = 16};
    CHECK(minic_machine_function_add_frame_object(&function, &object, &frame_id));
    CHECK(minic_machine_function_add_block(&function, NULL, false, &entry));
    CHECK(minic_machine_function_add_block(&function, NULL, false, &fallthrough));
    CHECK(minic_machine_function_add_block(&function, NULL, false, &taken));
    CHECK(minic_machine_block_add_successor(&function, entry, fallthrough));
    CHECK(minic_machine_block_add_successor(&function, entry, taken));
    CHECK(minic_machine_block_set_fallthrough(&function, entry, fallthrough));

    operands[0] = reg(9U, 64U, wr);
    operands[1] = imm(42);
    operands[2] = imm(0);
    CHECK(append(&function, &policy, entry, MINIC_A64_MACHINE_MOVZ, 0,
        operands, 3U));
    operands[0] = reg(9U, 64U, rw);
    operands[1] = block(taken);
    CHECK(append(&function, &policy, entry, MINIC_A64_MACHINE_CBNZ,
        MINIC_MACHINE_EFFECT_BRANCH, operands, 2U));

    operands[0] = reg(9U, 64U, wr);
    operands[1] = frame(frame_id);
    CHECK(append(&function, &policy, fallthrough, MINIC_A64_MACHINE_LDR_FRAME_X,
        MINIC_MACHINE_EFFECT_MEMORY_READ, operands, 2U));
    operands[0] = reg(9U, 64U, rw);
    operands[1] = address(10U, MINIC_A64_ADDRESS_UNSIGNED, 0);
    CHECK(append(&function, &policy, fallthrough, MINIC_A64_MACHINE_STR_X,
        MINIC_MACHINE_EFFECT_MEMORY_WRITE, operands, 2U));
    operands[0] = symbol("probe_callee", MINIC_A64_RELOC_DIRECT);
    CHECK(append(&function, &policy, fallthrough, MINIC_A64_MACHINE_BL,
        MINIC_MACHINE_EFFECT_CALL, operands, 1U));
    /* Real production scalar-FP/vector instruction families are selected
     * with physical register operands, never cached GNU asm lines. */
    operands[0] = fpreg(9U, 64U, MINIC_MACHINE_REGISTER_FLOAT, wr);
    operands[1] = address(10U, MINIC_A64_ADDRESS_UNSIGNED, 0);
    CHECK(append(&function, &policy, fallthrough, MINIC_A64_MACHINE_LDR_FP_D,
        MINIC_MACHINE_EFFECT_MEMORY_READ, operands, 2U));
    operands[0] = fpreg(9U, 64U, MINIC_MACHINE_REGISTER_FLOAT, wr);
    operands[1] = fpreg(9U, 64U, MINIC_MACHINE_REGISTER_FLOAT, rw);
    operands[2] = fpreg(10U, 64U, MINIC_MACHINE_REGISTER_FLOAT, rw);
    CHECK(append(&function, &policy, fallthrough, MINIC_A64_MACHINE_FADD,
        0U, operands, 3U));
    operands[0] = fpreg(9U, 64U, MINIC_MACHINE_REGISTER_FLOAT, rw);
    CHECK(append(&function, &policy, fallthrough, MINIC_A64_MACHINE_FCMP,
        0U, operands, 2U));
    operands[0] = reg(9U, 32U, wr);
    operands[1] = imm(3);
    CHECK(append(&function, &policy, fallthrough, MINIC_A64_MACHINE_CSET,
        0U, operands, 2U));
    operands[0] = fpreg(10U, 64U, MINIC_MACHINE_REGISTER_FLOAT, wr);
    operands[1] = reg(9U, 64U, rw);
    CHECK(append(&function, &policy, fallthrough, MINIC_A64_MACHINE_UCVTF,
        0U, operands, 2U));
    operands[0] = reg(9U, 64U, wr);
    operands[1] = fpreg(10U, 64U, MINIC_MACHINE_REGISTER_FLOAT, rw);
    CHECK(append(&function, &policy, fallthrough, MINIC_A64_MACHINE_FCVTZU,
        0U, operands, 2U));
    operands[0] = fpreg(16U, 128U, MINIC_MACHINE_REGISTER_VECTOR, wr);
    operands[1] = fpreg(16U, 128U, MINIC_MACHINE_REGISTER_VECTOR, rw);
    operands[2] = fpreg(17U, 128U, MINIC_MACHINE_REGISTER_VECTOR, rw);
    CHECK(append(&function, &policy, fallthrough, MINIC_A64_MACHINE_VECTOR_EOR_16B,
        0U, operands, 3U));
    CHECK(append(&function, &policy, fallthrough, MINIC_A64_MACHINE_RET,
        MINIC_MACHINE_EFFECT_RETURN, NULL, 0U));

    operands[0] = reg(9U, 64U, wr);
    operands[1] = reg(9U, 64U, rw);
    operands[2] = imm(32);
    CHECK(append(&function, &policy, taken, MINIC_A64_MACHINE_ADD_IMM, 0,
        operands, 3U));
    CHECK(append(&function, &policy, taken, MINIC_A64_MACHINE_RET,
        MINIC_MACHINE_EFFECT_RETURN, NULL, 0U));

    for (size_t bi = 0U; bi < function.block_count; ++bi)
        for (size_t ii = 0U; ii < function.blocks[bi].instruction_count; ++ii) {
            const MinicMachineInstruction *machine = &function.blocks[bi].instructions[ii];
            if (machine->kind == MINIC_MACHINE_INSTRUCTION_SELECTED &&
                !policy.verify.validate_selected_instruction(&function, machine, NULL))
                fprintf(stderr, "a64 illegal selected block=%zu ins=%zu op=%u\n",
                    bi, ii, machine->opcode);
        }
    CHECK(minic_machine_function_verify(&function, &policy.verify));
    CHECK(emitted_contains(&function, &policy,
        "fadd d9, d9, d10", "fcvtzu x9, d10", "eor v16.16b, v16.16b, v17.16b"));

    function.generated_features = UINT64_MAX;
    CHECK(!minic_machine_function_verify(&function, &policy.verify));
    function.generated_features = MINIC_A64_MACHINE_FEATURE_BASE |
        MINIC_A64_MACHINE_FEATURE_FP_SIMD;
    function.calling_convention = MINIC_CALLING_CONVENTION_MS_X64;
    CHECK(!minic_machine_function_verify(&function, &policy.verify));
    function.calling_convention = MINIC_CALLING_CONVENTION_DEFAULT;
    function.blocks[taken].instructions[0].operands[2].value.integer = 4096;
    CHECK(!minic_machine_function_verify(&function, &policy.verify));
    function.blocks[taken].instructions[0].operands[2].value.integer = 32;
    function.blocks[fallthrough].instructions[0].operands[1].value.frame.displacement = 8192;
    CHECK(!minic_machine_function_verify(&function, &policy.verify));
    function.blocks[fallthrough].instructions[0].operands[1].value.frame.displacement = 0;
    function.blocks[fallthrough].instructions[4].operands[2].value.physical.reg.width_bits = 32U;
    CHECK(!minic_machine_function_verify(&function, &policy.verify));
    function.blocks[fallthrough].instructions[4].operands[2].value.physical.reg.width_bits = 64U;
    CHECK(minic_machine_function_verify(&function, &policy.verify));
    minic_machine_function_destroy(&function);
    return true;
}

static bool run_loong(void) {
    MinicTargetInfo target = {0};
    MinicMachineGNUAsmPolicy policy = {0};
    MinicMachineFunction function = {0};
    MinicMachineFrameObject object = {0};
    MinicMachineOperand operands[3];
    size_t entry, fallthrough, taken, frame_id;
    unsigned int rw = MINIC_MACHINE_REGISTER_READ;
    unsigned int wr = MINIC_MACHINE_REGISTER_WRITE;
    target.architecture = MINIC_TARGET_ARCH_LOONGARCH64;
    target.data_layout = minic_loongarch64_linux_data_layout();
    CHECK(minic_loongarch64_machine_gnu_asm_policy_initialize(&target, &policy));
    CHECK(policy.section_type_marker == MINIC_MACHINE_GNU_SECTION_TYPE_AT);
    CHECK(minic_machine_function_initialize(&function, &target, "loong_probe",
        MINIC_LOONG_MACHINE_FEATURE_BASE | MINIC_LOONG_MACHINE_FEATURE_FP));
    function.frame_alignment = 16U;
    function.frame_size = 32U;
    object = (MinicMachineFrameObject){.area = MINIC_MACHINE_FRAME_LOCAL,
        .base = MINIC_MACHINE_FRAME_BASE_STACK_POINTER,
        .size_bytes = 8U, .alignment_bytes = 8U,
        .has_fixed_offset = true, .fixed_offset = 16};
    CHECK(minic_machine_function_add_frame_object(&function, &object, &frame_id));
    CHECK(minic_machine_function_add_block(&function, NULL, false, &entry));
    CHECK(minic_machine_function_add_block(&function, NULL, false, &fallthrough));
    CHECK(minic_machine_function_add_block(&function, NULL, false, &taken));
    CHECK(minic_machine_block_add_successor(&function, entry, fallthrough));
    CHECK(minic_machine_block_add_successor(&function, entry, taken));
    CHECK(minic_machine_block_set_fallthrough(&function, entry, fallthrough));

    operands[0] = reg(12U, 64U, wr);
    operands[1] = imm(42);
    CHECK(append(&function, &policy, entry, MINIC_LOONG_MACHINE_LI_D, 0,
        operands, 2U));
    operands[0] = reg(12U, 64U, rw);
    operands[1] = block(taken);
    CHECK(append(&function, &policy, entry, MINIC_LOONG_MACHINE_BNEZ,
        MINIC_MACHINE_EFFECT_BRANCH, operands, 2U));

    operands[0] = reg(12U, 64U, wr);
    operands[1] = frame(frame_id);
    CHECK(append(&function, &policy, fallthrough,
        MINIC_LOONG_MACHINE_LD_FRAME_D,
        MINIC_MACHINE_EFFECT_MEMORY_READ, operands, 2U));
    operands[0] = reg(12U, 64U, rw);
    operands[1] = address(13U, MINIC_LOONG_ADDRESS_IMM12, 0);
    CHECK(append(&function, &policy, fallthrough, MINIC_LOONG_MACHINE_ST_D,
        MINIC_MACHINE_EFFECT_MEMORY_WRITE, operands, 2U));
    operands[0] = symbol("probe_callee", MINIC_LOONG_RELOC_DIRECT);
    CHECK(append(&function, &policy, fallthrough, MINIC_LOONG_MACHINE_BL,
        MINIC_MACHINE_EFFECT_CALL, operands, 1U));
    operands[0] = fpreg(8U, 64U, MINIC_MACHINE_REGISTER_FLOAT, wr);
    operands[1] = address(3U, MINIC_LOONG_ADDRESS_IMM12, 16);
    CHECK(append(&function, &policy, fallthrough, MINIC_LOONG_MACHINE_FLD_D,
        MINIC_MACHINE_EFFECT_MEMORY_READ, operands, 2U));
    operands[0] = fpreg(8U, 64U, MINIC_MACHINE_REGISTER_FLOAT, wr);
    operands[1] = fpreg(8U, 64U, MINIC_MACHINE_REGISTER_FLOAT, rw);
    operands[2] = fpreg(9U, 64U, MINIC_MACHINE_REGISTER_FLOAT, rw);
    CHECK(append(&function, &policy, fallthrough, MINIC_LOONG_MACHINE_FADD_D,
        0U, operands, 3U));
    operands[0] = fpreg(0U, 1U, MINIC_MACHINE_REGISTER_FLAGS, wr);
    CHECK(append(&function, &policy, fallthrough, MINIC_LOONG_MACHINE_FCMP_CLT_D,
        0U, operands, 3U));
    operands[0] = reg(12U, 64U, wr);
    operands[1] = fpreg(0U, 1U, MINIC_MACHINE_REGISTER_FLAGS, rw);
    CHECK(append(&function, &policy, fallthrough, MINIC_LOONG_MACHINE_MOVCF2GR,
        0U, operands, 2U));
    operands[0] = fpreg(8U, 32U, MINIC_MACHINE_REGISTER_FLOAT, wr);
    operands[1] = fpreg(8U, 64U, MINIC_MACHINE_REGISTER_FLOAT, rw);
    CHECK(append(&function, &policy, fallthrough, MINIC_LOONG_MACHINE_FCVT_S_D,
        0U, operands, 2U));
    operands[0] = reg(12U, 64U, wr);
    operands[1] = fpreg(8U, 32U, MINIC_MACHINE_REGISTER_FLOAT, rw);
    CHECK(append(&function, &policy, fallthrough, MINIC_LOONG_MACHINE_MOVFR2GR_S,
        0U, operands, 2U));
    CHECK(append(&function, &policy, fallthrough, MINIC_LOONG_MACHINE_RET,
        MINIC_MACHINE_EFFECT_RETURN, NULL, 0U));

    operands[0] = reg(12U, 64U, wr);
    operands[1] = reg(12U, 64U, rw);
    operands[2] = imm(32);
    CHECK(append(&function, &policy, taken, MINIC_LOONG_MACHINE_ADDI_D, 0,
        operands, 3U));
    CHECK(append(&function, &policy, taken, MINIC_LOONG_MACHINE_RET,
        MINIC_MACHINE_EFFECT_RETURN, NULL, 0U));

    CHECK(minic_machine_function_verify(&function, &policy.verify));
    CHECK(emitted_contains(&function, &policy,
        "fld.d $ft0, $sp, 16", "fcmp.clt.d $fcc0, $ft0, $ft1",
        "movcf2gr $t0, $fcc0"));
    function.generated_features = UINT64_MAX;
    CHECK(!minic_machine_function_verify(&function, &policy.verify));
    function.generated_features = MINIC_LOONG_MACHINE_FEATURE_BASE |
        MINIC_LOONG_MACHINE_FEATURE_FP;
    function.calling_convention = MINIC_CALLING_CONVENTION_MS_X64;
    CHECK(!minic_machine_function_verify(&function, &policy.verify));
    function.calling_convention = MINIC_CALLING_CONVENTION_DEFAULT;
    function.blocks[taken].instructions[0].operands[2].value.integer = 4096;
    CHECK(!minic_machine_function_verify(&function, &policy.verify));
    function.blocks[taken].instructions[0].operands[2].value.integer = 32;
    function.blocks[fallthrough].instructions[0].operands[1].value.frame.displacement = 8192;
    CHECK(!minic_machine_function_verify(&function, &policy.verify));
    function.blocks[fallthrough].instructions[0].operands[1].value.frame.displacement = 0;
    function.blocks[fallthrough].instructions[5].operands[0].value.physical.reg.width_bits = 32U;
    CHECK(!minic_machine_function_verify(&function, &policy.verify));
    function.blocks[fallthrough].instructions[5].operands[0].value.physical.reg.width_bits = 1U;
    CHECK(minic_loongarch64_target_soft_float_abi_set(&target, true));
    function.generated_features = MINIC_LOONG_MACHINE_FEATURE_BASE;
    /* An LP64D call's FP volatile clobbers are not physical LP64S state;
     * the target model must refuse to launder them across ABIs. */
    CHECK(!minic_machine_function_verify(&function, &policy.verify));
    CHECK(minic_loongarch64_target_soft_float_abi_set(&target, false));
    function.generated_features = MINIC_LOONG_MACHINE_FEATURE_BASE |
        MINIC_LOONG_MACHINE_FEATURE_FP;
    CHECK(minic_machine_function_verify(&function, &policy.verify));
    minic_machine_function_destroy(&function);
    return true;
}

static size_t count_pattern(const char *text, const char *pattern) {
    size_t count = 0U;
    const size_t length = strlen(pattern);
    while ((text = strstr(text, pattern)) != NULL) {
        ++count;
        text += length;
    }
    return count;
}

/* This deliberately exercises physical FRAME owners, not arbitrary textual
 * `st.d/ld.d` matches: an intervening core-volatility claim or a different
 * register must preserve the load.  Only an actual next-block unconditional
 * transfer may disappear from emitted GNU assembly. */
static bool run_loong_adjacent_frame_peephole(bool distinct_register,
    bool volatile_read, bool native_address, unsigned int address_variant,
    bool branch_nonfallthrough) {
    MinicTargetInfo target = {0};
    MinicMachineGNUAsmPolicy policy = {0};
    MinicMachineFunction function = {0};
    MinicMachineFrameObject object = {0};
    MinicMachineOperand operands[2];
    size_t entry, next, gap = 0U, frame_id;
    FILE *out = NULL;
    char buffer[4096];
    size_t length;
    bool ok = false;
    const char *stage = "initialize";
    target.architecture = MINIC_TARGET_ARCH_LOONGARCH64;
    target.data_layout = minic_loongarch64_linux_data_layout();
    if (!minic_loongarch64_machine_gnu_asm_policy_initialize(&target, &policy) ||
        !minic_machine_function_initialize(&function, &target,
            "loong_size_peephole", MINIC_LOONG_MACHINE_FEATURE_BASE |
                MINIC_LOONG_MACHINE_FEATURE_FP))
        return false;
    function.frame_alignment = 16U;
    function.frame_size = 32U;
    object = (MinicMachineFrameObject){.area = MINIC_MACHINE_FRAME_LOCAL,
        .base = MINIC_MACHINE_FRAME_BASE_STACK_POINTER,
        .size_bytes = 8U, .alignment_bytes = 8U,
        .has_fixed_offset = true, .fixed_offset = 16};
    stage = "block construction";
    if (!minic_machine_function_add_frame_object(&function, &object, &frame_id) ||
        !minic_machine_function_add_block(&function, NULL, false, &entry) ||
        (branch_nonfallthrough &&
            !minic_machine_function_add_block(&function, NULL, false, &gap)) ||
        !minic_machine_function_add_block(&function, NULL, false, &next)) goto done;
    stage = "store";
    operands[0] = reg(12U, 64U, MINIC_MACHINE_REGISTER_READ);
    operands[1] = native_address ?
        address(3U, MINIC_LOONG_ADDRESS_IMM12, 16) : frame(frame_id);
    if (!append(&function, &policy, entry,
            native_address ? MINIC_LOONG_MACHINE_ST_D :
                             MINIC_LOONG_MACHINE_ST_FRAME_D,
            MINIC_MACHINE_EFFECT_MEMORY_WRITE, operands, 2U)) goto done;
    stage = "load";
    operands[0] = reg(distinct_register ? 13U : 12U, 64U,
        MINIC_MACHINE_REGISTER_WRITE);
    if (native_address && address_variant != 0U)
        operands[1] = address(address_variant == 2U ? 4U : 3U,
            MINIC_LOONG_ADDRESS_IMM12, address_variant == 1U ? 24 : 16);
    if (!append(&function, &policy, entry,
            native_address ? MINIC_LOONG_MACHINE_LD_D :
                             MINIC_LOONG_MACHINE_LD_FRAME_D,
            MINIC_MACHINE_EFFECT_MEMORY_READ |
                (volatile_read ? MINIC_MACHINE_EFFECT_VOLATILE : 0U),
            operands, 2U)) goto done;
    stage = "branch/verify";
    operands[0] = block(next);
    if (!append(&function, &policy, entry, MINIC_LOONG_MACHINE_B,
            MINIC_MACHINE_EFFECT_BRANCH, operands, 1U)) goto done;
    stage = "successor";
    if (!minic_machine_block_add_successor(&function, entry, next)) goto done;
    stage = "next return";
    if (branch_nonfallthrough &&
        !append(&function, &policy, gap, MINIC_LOONG_MACHINE_RET,
            MINIC_MACHINE_EFFECT_RETURN, NULL, 0U)) goto done;
    if (!append(&function, &policy, next, MINIC_LOONG_MACHINE_RET,
            MINIC_MACHINE_EFFECT_RETURN, NULL, 0U)) goto done;
    stage = "verify";
    if (!minic_machine_function_verify(&function, &policy.verify)) goto done;
    stage = "emit";
    out = open_test_output();
    if (out == NULL || !minic_machine_gnu_asm_emit_body(out, &function, &policy) ||
        fflush(out) != 0 || fseek(out, 0, SEEK_SET) != 0) goto done;
    length = fread(buffer, 1U, sizeof(buffer) - 1U, out);
    buffer[length] = '\0';
    ok = count_pattern(buffer, "  st.d $t0, $sp, 16\n") == 1U &&
        count_pattern(buffer, address_variant == 2U ? "  ld.d $t0, $a0, 16\n" :
            address_variant == 1U ? "  ld.d $t0, $sp, 24\n" :
            distinct_register ? "  ld.d $t1, $sp, 16\n" :
                                "  ld.d $t0, $sp, 16\n") ==
            ((distinct_register || volatile_read || address_variant != 0U)
                ? 1U : 0U) &&
        count_pattern(buffer, "  b .Lminic_mir_") ==
            (branch_nonfallthrough ? 1U : 0U);
    if (!ok) fprintf(stderr, "Loong size peephole emission:\n%s\n", buffer);
done:
    if (!ok) fprintf(stderr, "Loong size peephole failed at %s distinct=%d volatile=%d native=%d variant=%u nonfallthrough=%d\n",
        stage, distinct_register ? 1 : 0, volatile_read ? 1 : 0,
        native_address ? 1 : 0, address_variant,
        branch_nonfallthrough ? 1 : 0);
    if (out != NULL) fclose(out);
    minic_machine_function_destroy(&function);
    return ok;
}

int main(void) {
    if (!run_a64() || !run_loong() ||
        !run_loong_adjacent_frame_peephole(false, false, false, 0U, false) ||
        !run_loong_adjacent_frame_peephole(true, false, false, 0U, false) ||
        !run_loong_adjacent_frame_peephole(false, true, false, 0U, false) ||
        !run_loong_adjacent_frame_peephole(false, false, true, 0U, false) ||
        !run_loong_adjacent_frame_peephole(true, false, true, 0U, false) ||
        !run_loong_adjacent_frame_peephole(false, true, true, 0U, false) ||
        !run_loong_adjacent_frame_peephole(false, false, true, 1U, false) ||
        !run_loong_adjacent_frame_peephole(false, false, true, 2U, false) ||
        !run_loong_adjacent_frame_peephole(false, false, true, 0U, true))
        return EXIT_FAILURE;
    puts("AArch64+LoongArch64 physical MIR -> verify -> GNU: PASS");
    return EXIT_SUCCESS;
}
