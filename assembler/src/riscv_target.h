#ifndef MINIAS_RISCV_TARGET_H
#define MINIAS_RISCV_TARGET_H

#include "riscv_fixup_kinds.h"

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define MINIAS_RISCV_OPTION_PIC 1U

/* TargetContext: RISC-V ISA/ABI/attribute data never belongs to Common.
 * RV32 and RV64 are profiles of this single Target, not separate assemblers. */
typedef struct MiniAsRiscvState {
    bool strict_rv64i;
    unsigned strict_i_xlen;
    bool isa_explicit;
    unsigned target_xlen;
    uint64_t isa_features;
    unsigned char target_attributes[512];
    size_t target_attributes_size;
    /* GNU RISC-V .option push/pop is target-local, never shared state. */
    uint32_t option_stack[64];
    size_t option_depth;
} MiniAsRiscvState;

/* Typed target-private accessor; avoids RISC-V downcasts in MC Core. */
#define MINIAS_RV_STATE(as) ((MiniAsRiscvState *)((as)->target_state))

/* Shared target-owned MC opcode families: used by parser, encoder and
 * relaxation without any mnemonic parsing in the common assembler. */
enum MiniAsRiscvMCFamily {
    RV_MC_R = 1, RV_MC_I, RV_MC_LOAD, RV_MC_STORE, RV_MC_U,
    RV_MC_BRANCH, RV_MC_JAL, RV_MC_WORD, RV_MC_C16,
    RV_MC_SYMBOL_LOAD, RV_MC_SYMBOL_STORE,
    RV_MC_LI, RV_MC_CALL, RV_MC_TAIL, RV_MC_LA, RV_MC_RAW
};

/* One Target-private instruction descriptor lookup supplies both parser
 * dispatch and ISA/XLEN policy. Unknown/legacy pseudo spellings return
 * false and continue through the Target's explicit compatibility policy. */
bool minias_riscv_mc_descriptor_policy(const char *mnemonic,
                                       uint64_t *required_features,
                                       bool *rv64_only,
                                       unsigned *family);

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
