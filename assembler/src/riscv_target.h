#ifndef MINIAS_RISCV_TARGET_H
#define MINIAS_RISCV_TARGET_H

#include "riscv_fixup_kinds.h"

#include <stdbool.h>

#define MINIAS_RISCV_OPTION_PIC 1U

/* Shared target-owned MC opcode families: used by parser, encoder and
 * relaxation without any mnemonic parsing in the common assembler. */
enum MiniAsRiscvMCFamily {
    RV_MC_R = 1, RV_MC_I, RV_MC_LOAD, RV_MC_STORE, RV_MC_U,
    RV_MC_BRANCH, RV_MC_JAL, RV_MC_WORD, RV_MC_C16,
    RV_MC_SYMBOL_LOAD, RV_MC_SYMBOL_STORE,
    RV_MC_LI, RV_MC_CALL, RV_MC_TAIL, RV_MC_LA, RV_MC_RAW
};

/* RISC-V-only psABI relocation constants. */
#define MINIAS_R_RISCV_32 1U
#define MINIAS_R_RISCV_64 2U
#define MINIAS_R_RISCV_JAL 17U
#define MINIAS_R_RISCV_CALL_PLT 19U
#define MINIAS_R_RISCV_GOT_HI20 20U
#define MINIAS_R_RISCV_PCREL_HI20 23U
#define MINIAS_R_RISCV_PCREL_LO12_I 24U
#define MINIAS_R_RISCV_PCREL_LO12_S 25U
#define MINIAS_R_RISCV_ADD16 34U
#define MINIAS_R_RISCV_ADD32 35U
#define MINIAS_R_RISCV_ADD64 36U
#define MINIAS_R_RISCV_SUB16 38U
#define MINIAS_R_RISCV_SUB32 39U
#define MINIAS_R_RISCV_SUB64 40U
#define MINIAS_R_RISCV_RELAX 51U

#endif
