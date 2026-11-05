/* Native LoongArch target builtin ISA forms: selected opcode contracts,
 * physical ABI binding, immediate legalizer, and GNU printer.  No source ASM
 * or legacy direct-text emitter participates in this witness. */
#include "target/loongarch64/machine_opcode.h"
#include "target/common/machine_builder.h"
#include "target/data_layout.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef enum BuiltinForm {
    BUILTIN_RR_WR,
    BUILTIN_RR_RD,
    BUILTIN_RRR,
    BUILTIN_CSR_RD,
    BUILTIN_CSR_RW,
    BUILTIN_CSR_XCHG
} BuiltinForm;

typedef enum ForgeKind {
    FORGE_NONE,
    FORGE_WRONG_PHYSICAL,
    FORGE_WRONG_WIDTH,
    FORGE_WRONG_ACCESS,
    FORGE_IMMEDIATE_NEGATIVE,
    FORGE_IMMEDIATE_OVERFLOW,
    FORGE_CALL_EFFECT_MISSING
} ForgeKind;

typedef struct BuiltinCase {
    const char *mnemonic;
    uint32_t opcode;
    BuiltinForm form;
} BuiltinCase;

static const BuiltinCase builtin_cases[] = {
    {"cpucfg", MINIC_LOONG_MACHINE_CPUCFG, BUILTIN_RR_WR},
    {"asrtle.d", MINIC_LOONG_MACHINE_ASRTLE_D, BUILTIN_RR_RD},
    {"asrtgt.d", MINIC_LOONG_MACHINE_ASRTGT_D, BUILTIN_RR_RD},
    {"crc.w.b.w", MINIC_LOONG_MACHINE_CRC_W_B_W, BUILTIN_RRR},
    {"crc.w.h.w", MINIC_LOONG_MACHINE_CRC_W_H_W, BUILTIN_RRR},
    {"crc.w.w.w", MINIC_LOONG_MACHINE_CRC_W_W_W, BUILTIN_RRR},
    {"crc.w.d.w", MINIC_LOONG_MACHINE_CRC_W_D_W, BUILTIN_RRR},
    {"crcc.w.b.w", MINIC_LOONG_MACHINE_CRCC_W_B_W, BUILTIN_RRR},
    {"crcc.w.h.w", MINIC_LOONG_MACHINE_CRCC_W_H_W, BUILTIN_RRR},
    {"crcc.w.w.w", MINIC_LOONG_MACHINE_CRCC_W_W_W, BUILTIN_RRR},
    {"crcc.w.d.w", MINIC_LOONG_MACHINE_CRCC_W_D_W, BUILTIN_RRR},
    {"iocsrrd.b", MINIC_LOONG_MACHINE_IOCSRRD_B, BUILTIN_RR_WR},
    {"iocsrrd.h", MINIC_LOONG_MACHINE_IOCSRRD_H, BUILTIN_RR_WR},
    {"iocsrrd.w", MINIC_LOONG_MACHINE_IOCSRRD_W, BUILTIN_RR_WR},
    {"iocsrrd.d", MINIC_LOONG_MACHINE_IOCSRRD_D, BUILTIN_RR_WR},
    {"iocsrwr.b", MINIC_LOONG_MACHINE_IOCSRWR_B, BUILTIN_RR_RD},
    {"iocsrwr.h", MINIC_LOONG_MACHINE_IOCSRWR_H, BUILTIN_RR_RD},
    {"iocsrwr.w", MINIC_LOONG_MACHINE_IOCSRWR_W, BUILTIN_RR_RD},
    {"iocsrwr.d", MINIC_LOONG_MACHINE_IOCSRWR_D, BUILTIN_RR_RD},
    {"csrrd", MINIC_LOONG_MACHINE_CSRRD, BUILTIN_CSR_RD},
    {"csrwr", MINIC_LOONG_MACHINE_CSRWR, BUILTIN_CSR_RW},
    {"csrxchg", MINIC_LOONG_MACHINE_CSRXCHG, BUILTIN_CSR_XCHG}
};

static MinicMachineOperand gp(unsigned int reg, unsigned int access) {
    MinicMachineOperand op = {0};
    op.kind = MINIC_MACHINE_OPERAND_REGISTER;
    op.value.physical.reg = (MinicMachineRegister){reg, 64U,
        MINIC_MACHINE_REGISTER_GENERAL};
    op.value.physical.access = access;
    return op;
}

static bool test_case(const BuiltinCase *test, ForgeKind forged) {
    MinicTargetInfo target = {0};
    MinicMachineGNUAsmPolicy policy = {0};
    MinicMachineFunction machine = {0};
    MinicMachineBuilder builder = {0};
    MinicMachineOperand ops[3] = {{0}};
    MinicMachineRegisterEffect read_ra = {
        {1U, 64U, MINIC_MACHINE_REGISTER_GENERAL},
        MINIC_MACHINE_REGISTER_READ
    };
    MinicMachineOpcodeContract contract = {0};
    MinicMachineInstruction *native;
    FILE *out = NULL;
    char actual[4096], expected[160];
    uint32_t opcode = 0U;
    size_t block, nops, length;
    bool ok = false;
    const char *stage = "builder";
    const unsigned int read = MINIC_MACHINE_REGISTER_READ;
    const unsigned int write = MINIC_MACHINE_REGISTER_WRITE;
    if (test == NULL || !minic_loongarch64_machine_builtin_opcode(
            test->mnemonic, &opcode) || opcode != test->opcode)
        return false;
    target.architecture = MINIC_TARGET_ARCH_LOONGARCH64;
    target.data_layout = minic_loongarch64_linux_data_layout();
    if (!minic_loongarch64_machine_gnu_asm_policy_initialize(&target, &policy) ||
        !policy.verify.lookup_opcode(&target, opcode, &contract,
            policy.context) ||
        contract.required_effects != MINIC_MACHINE_EFFECT_CALL ||
        contract.implicit_count != 0U ||
        !minic_machine_function_initialize(&machine, &target,
            "builtin_isolated", MINIC_LOONG_MACHINE_FEATURE_BASE |
                MINIC_LOONG_MACHINE_FEATURE_FP))
        return false;
    machine.frame_alignment = 16U;
    if (!minic_machine_builder_initialize(&builder, &machine) ||
        !minic_machine_builder_create_block(&builder, NULL,
            false, false, &block) ||
        !minic_machine_builder_position_at(&builder, block))
        goto done;
    ops[0] = gp(4U, write);
    ops[1] = gp(4U, read);
    nops = 2U;
    switch (test->form) {
    case BUILTIN_RR_WR:
        break;
    case BUILTIN_RR_RD:
        ops[0].value.physical.access = read;
        ops[1].value.physical.reg.number = 5U;
        break;
    case BUILTIN_RRR:
        ops[2] = gp(5U, read);
        nops = 3U;
        break;
    case BUILTIN_CSR_RD:
    case BUILTIN_CSR_RW:
        if (test->form == BUILTIN_CSR_RW)
            ops[0].value.physical.access = read | write;
        ops[1].kind = MINIC_MACHINE_OPERAND_INTEGER;
        ops[1].value.integer = 16383;
        break;
    case BUILTIN_CSR_XCHG:
        ops[0].value.physical.access = read | write;
        ops[1] = gp(5U, read);
        ops[2].kind = MINIC_MACHINE_OPERAND_INTEGER;
        ops[2].value.integer = 16383;
        nops = 3U;
        break;
    }
    if (contract.operand_count != nops ||
        !minic_machine_builder_emit_selected(&builder, opcode,
            MINIC_MACHINE_EFFECT_CALL, ops, nops, NULL, 0U) ||
        !minic_machine_builder_emit_selected(&builder,
            MINIC_LOONG_MACHINE_RET, MINIC_MACHINE_EFFECT_RETURN,
            NULL, 0U, &read_ra, 1U)) goto done;
    stage = "preflight";
    native = &machine.blocks[block].instructions[0];
    switch (forged) {
    case FORGE_NONE: break;
    case FORGE_WRONG_PHYSICAL:
        native->operands[0].value.physical.reg.number = 12U;
        break;
    case FORGE_WRONG_WIDTH:
        native->operands[0].value.physical.reg.width_bits = 32U;
        break;
    case FORGE_WRONG_ACCESS:
        native->operands[0].value.physical.access ^= write;
        break;
    case FORGE_IMMEDIATE_NEGATIVE:
        native->operands[nops - 1U].value.integer = -1;
        break;
    case FORGE_IMMEDIATE_OVERFLOW:
        native->operands[nops - 1U].value.integer = 16384;
        break;
    case FORGE_CALL_EFFECT_MISSING:
        native->effects = 0U;
        break;
    }
    if (forged != FORGE_NONE) {
        ok = !minic_machine_gnu_asm_preflight(&machine, &policy);
        goto done;
    }
    if (!minic_machine_gnu_asm_preflight(&machine, &policy)) goto done;
    stage = "serializer";
    out = tmpfile();
    if (out == NULL || !minic_machine_gnu_asm_emit_body(out, &machine, &policy) ||
        fflush(out) != 0 || fseek(out, 0L, SEEK_SET) != 0) goto done;
    length = fread(actual, 1U, sizeof(actual) - 1U, out);
    actual[length] = '\0';
    switch (test->form) {
    case BUILTIN_RR_WR:
        snprintf(expected, sizeof(expected), "  %s $a0, $a0\n", test->mnemonic);
        break;
    case BUILTIN_RR_RD:
        snprintf(expected, sizeof(expected), "  %s $a0, $a1\n", test->mnemonic);
        break;
    case BUILTIN_RRR:
        snprintf(expected, sizeof(expected), "  %s $a0, $a0, $a1\n", test->mnemonic);
        break;
    case BUILTIN_CSR_RD:
    case BUILTIN_CSR_RW:
        snprintf(expected, sizeof(expected), "  %s $a0, 16383\n", test->mnemonic);
        break;
    case BUILTIN_CSR_XCHG:
        snprintf(expected, sizeof(expected), "  %s $a0, $a1, 16383\n", test->mnemonic);
        break;
    default: goto done;
    }
    ok = strstr(actual, expected) != NULL &&
        strstr(actual, "  bl ") == NULL &&
        strstr(actual, "  .ascii ") == NULL;
done:
    if (!ok) fprintf(stderr, "loong builtin %s forged=%d stage=%s failed\n",
        test->mnemonic, (int)forged, stage);
    if (out != NULL) fclose(out);
    minic_machine_function_destroy(&machine);
    return ok;
}

int main(void) {
    uint32_t opcode = 0U;
    size_t i;
    assert(!minic_loongarch64_machine_builtin_opcode(NULL, &opcode));
    assert(!minic_loongarch64_machine_builtin_opcode("", &opcode));
    assert(!minic_loongarch64_machine_builtin_opcode("csrrd.w", &opcode));
    assert(!minic_loongarch64_machine_builtin_opcode("unknown", &opcode));
    assert(!minic_loongarch64_machine_builtin_opcode("cpucfg", NULL));
    for (i = 0U; i < sizeof(builtin_cases) / sizeof(builtin_cases[0]); ++i) {
        assert(test_case(&builtin_cases[i], FORGE_NONE));
        assert(test_case(&builtin_cases[i], FORGE_WRONG_PHYSICAL));
        assert(test_case(&builtin_cases[i], FORGE_WRONG_WIDTH));
        assert(test_case(&builtin_cases[i], FORGE_WRONG_ACCESS));
        assert(test_case(&builtin_cases[i], FORGE_CALL_EFFECT_MISSING));
        if (builtin_cases[i].form >= BUILTIN_CSR_RD) {
            assert(test_case(&builtin_cases[i], FORGE_IMMEDIATE_NEGATIVE));
            assert(test_case(&builtin_cases[i], FORGE_IMMEDIATE_OVERFLOW));
        }
    }
    puts("LoongArch native machine builtin 22 mnemonics + strict negatives PASS");
    return 0;
}
