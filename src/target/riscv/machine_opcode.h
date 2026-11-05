#ifndef MINIC_TARGET_RISCV_MACHINE_OPCODE_H
#define MINIC_TARGET_RISCV_MACHINE_OPCODE_H

#include "target/common/machine_ir_verify.h"

/* Native, target-owned RISC-V machine instruction identities. These values
 * are never GNU assembly strings and must not be constructed from Core/C
 * opcode numbers. Compiler-selected instructions are independently verified
 * against the selected RV32/RV64 machine contract. */
typedef enum MinicRiscvMachineOpcode {
    MINIC_RISCV_MACHINE_INVALID = 0,
    MINIC_RISCV_MACHINE_LUI,
    MINIC_RISCV_MACHINE_AUIPC,
    MINIC_RISCV_MACHINE_JAL_BLOCK,
    MINIC_RISCV_MACHINE_JALR_BRANCH,
    /* Physical register-indirect branch to a known CFG target. The target
     * verifier requires the immediately preceding typed LA_BLOCK to write
     * this register and reference the same MachineBlock. */
    MINIC_RISCV_MACHINE_JALR_BLOCK,
    MINIC_RISCV_MACHINE_JALR_CALL,
    MINIC_RISCV_MACHINE_BEQ,
    MINIC_RISCV_MACHINE_BNE,
    MINIC_RISCV_MACHINE_BLT,
    MINIC_RISCV_MACHINE_BGE,
    MINIC_RISCV_MACHINE_BLTU,
    MINIC_RISCV_MACHINE_BGEU,
    MINIC_RISCV_MACHINE_LB,
    MINIC_RISCV_MACHINE_LH,
    MINIC_RISCV_MACHINE_LW,
    MINIC_RISCV_MACHINE_LBU,
    MINIC_RISCV_MACHINE_LHU,
    MINIC_RISCV_MACHINE_LB_FRAME,
    MINIC_RISCV_MACHINE_LH_FRAME,
    MINIC_RISCV_MACHINE_LW_FRAME,
    MINIC_RISCV_MACHINE_LBU_FRAME,
    MINIC_RISCV_MACHINE_LHU_FRAME,
    MINIC_RISCV_MACHINE_SB,
    MINIC_RISCV_MACHINE_SH,
    MINIC_RISCV_MACHINE_SW,
    MINIC_RISCV_MACHINE_SB_FRAME,
    MINIC_RISCV_MACHINE_SH_FRAME,
    MINIC_RISCV_MACHINE_SW_FRAME,
    MINIC_RISCV_MACHINE_ADDI,
    MINIC_RISCV_MACHINE_SLTI,
    MINIC_RISCV_MACHINE_SLTIU,
    MINIC_RISCV_MACHINE_XORI,
    MINIC_RISCV_MACHINE_ORI,
    MINIC_RISCV_MACHINE_ANDI,
    MINIC_RISCV_MACHINE_SLLI,
    MINIC_RISCV_MACHINE_SRLI,
    MINIC_RISCV_MACHINE_SRAI,
    MINIC_RISCV_MACHINE_ADD,
    MINIC_RISCV_MACHINE_SUB,
    MINIC_RISCV_MACHINE_SLL,
    MINIC_RISCV_MACHINE_SLT,
    MINIC_RISCV_MACHINE_SLTU,
    MINIC_RISCV_MACHINE_XOR,
    MINIC_RISCV_MACHINE_SRL,
    MINIC_RISCV_MACHINE_SRA,
    MINIC_RISCV_MACHINE_OR,
    MINIC_RISCV_MACHINE_AND,
    MINIC_RISCV_MACHINE_FENCE,
    MINIC_RISCV_MACHINE_FENCE_MASK,
    MINIC_RISCV_MACHINE_ECALL,
    MINIC_RISCV_MACHINE_EBREAK,
    /* RV64I-only: these are not legal RV32I selected instructions. */
    MINIC_RISCV_MACHINE_LWU,
    MINIC_RISCV_MACHINE_LD,
    MINIC_RISCV_MACHINE_SD,
    MINIC_RISCV_MACHINE_LWU_FRAME,
    MINIC_RISCV_MACHINE_LD_FRAME,
    MINIC_RISCV_MACHINE_SD_FRAME,
    MINIC_RISCV_MACHINE_ADDIW,
    MINIC_RISCV_MACHINE_SLLIW,
    MINIC_RISCV_MACHINE_SRLIW,
    MINIC_RISCV_MACHINE_SRAIW,
    MINIC_RISCV_MACHINE_ADDW,
    MINIC_RISCV_MACHINE_SUBW,
    MINIC_RISCV_MACHINE_SLLW,
    MINIC_RISCV_MACHINE_SRLW,
    MINIC_RISCV_MACHINE_SRAW,
    /* Explicitly modeled GNU pseudos. Each has fixed, verified machine
     * semantics and a target-owned serializer expansion, never raw text. */
    MINIC_RISCV_MACHINE_LI,
    MINIC_RISCV_MACHINE_MV,
    MINIC_RISCV_MACHINE_NOT,
    MINIC_RISCV_MACHINE_NEG,
    MINIC_RISCV_MACHINE_SEQZ,
    MINIC_RISCV_MACHINE_SNEZ,
    MINIC_RISCV_MACHINE_SEXT_W,
    MINIC_RISCV_MACHINE_ZEXT_W,
    MINIC_RISCV_MACHINE_BEQZ,
    MINIC_RISCV_MACHINE_BNEZ,
    MINIC_RISCV_MACHINE_J_BLOCK,
    MINIC_RISCV_MACHINE_RET,
    MINIC_RISCV_MACHINE_LA_BLOCK,
    MINIC_RISCV_MACHINE_LA_PCREL,
    MINIC_RISCV_MACHINE_LA_GOT,
    MINIC_RISCV_MACHINE_CALL_DIRECT,
    MINIC_RISCV_MACHINE_TAIL_DIRECT,
    MINIC_RISCV_MACHINE_CALL_INDIRECT,
    MINIC_RISCV_MACHINE_BARRIER,
    MINIC_RISCV_MACHINE_UNREACHABLE,
    MINIC_RISCV_MACHINE_LAST
} MinicRiscvMachineOpcode;

/* A symbol's relocation kind is part of the verified operation; GNU `la`
 * and `lla` cannot be interchanged without changing ELF relocations. */
typedef enum MinicRiscvMachineRelocation {
    MINIC_RISCV_MACHINE_RELOC_INVALID = 0,
    MINIC_RISCV_MACHINE_RELOC_PCREL_LOCAL,
    MINIC_RISCV_MACHINE_RELOC_GOT,
    MINIC_RISCV_MACHINE_RELOC_CALL
} MinicRiscvMachineRelocation;

typedef enum MinicRiscvMachineAddressMode {
    MINIC_RISCV_MACHINE_ADDRESS_INVALID = 0,
    MINIC_RISCV_MACHINE_ADDRESS_BASE_DISPLACEMENT
} MinicRiscvMachineAddressMode;

typedef enum MinicRiscvMachineForm {
    MINIC_RISCV_MACHINE_FORM_INVALID = 0,
    MINIC_RISCV_MACHINE_FORM_NONE,
    MINIC_RISCV_MACHINE_FORM_REGISTER,
    MINIC_RISCV_MACHINE_FORM_REGISTER_REGISTER,
    MINIC_RISCV_MACHINE_FORM_REGISTER_IMMEDIATE,
    MINIC_RISCV_MACHINE_FORM_REGISTER_REGISTER_IMMEDIATE,
    MINIC_RISCV_MACHINE_FORM_REGISTER_REGISTER_REGISTER,
    MINIC_RISCV_MACHINE_FORM_LOAD,
    MINIC_RISCV_MACHINE_FORM_STORE,
    MINIC_RISCV_MACHINE_FORM_LOAD_FRAME,
    MINIC_RISCV_MACHINE_FORM_STORE_FRAME,
    MINIC_RISCV_MACHINE_FORM_REGISTER_REGISTER_BLOCK,
    MINIC_RISCV_MACHINE_FORM_REGISTER_BLOCK,
    MINIC_RISCV_MACHINE_FORM_BLOCK,
    MINIC_RISCV_MACHINE_FORM_REGISTER_SYMBOL,
    MINIC_RISCV_MACHINE_FORM_SYMBOL,
    MINIC_RISCV_MACHINE_FORM_FENCE_MASK
} MinicRiscvMachineForm;

typedef struct MinicRiscvMachineOpcodeInfo {
    const char *mnemonic;
    MinicRiscvMachineForm form;
    bool rv64_only;
} MinicRiscvMachineOpcodeInfo;

bool minic_riscv_machine_opcode_info(
    const MinicTargetInfo *, uint32_t opcode,
    MinicRiscvMachineOpcodeInfo *);

bool minic_riscv_machine_lookup_opcode_contract(
    const MinicTargetInfo *, uint32_t opcode,
    MinicMachineOpcodeContract *, void *context);
bool minic_riscv_machine_validate_register(
    const MinicTargetInfo *, const MinicMachineRegister *, void *context);
bool minic_riscv_machine_validate_address(
    const MinicTargetInfo *, const MinicMachineAddress *, void *context);
bool minic_riscv_machine_validate_relocation(
    const MinicTargetInfo *, const char *symbol, uint32_t relocation,
    int64_t addend, void *context);
bool minic_riscv_machine_validate_selected_instruction(
    const MinicMachineFunction *, const MinicMachineInstruction *, void *context);
bool minic_riscv_machine_validate_generated_features(
    const MinicTargetInfo *, uint64_t generated_features, void *context);
bool minic_riscv_machine_validate_function_abi(
    const MinicTargetInfo *, MinicFunctionCallingConvention, void *context);
bool minic_riscv_machine_validate_frame_object(
    const MinicTargetInfo *, const MinicMachineFunction *,
    const MinicMachineFrameObject *, void *context);
/* A single already-resolved frame access. This never inserts materialization
 * instructions: a non-encodable offset must be legalized by the target
 * builder into typed LI/ADD plus a normal ADDRESS load/store. */
bool minic_riscv_machine_resolve_frame_access(
    const MinicMachineFunction *, uint32_t opcode,
    const MinicMachineOperand *frame_operand,
    unsigned *base_register, int64_t *immediate);

#endif
