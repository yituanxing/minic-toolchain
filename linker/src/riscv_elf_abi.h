#ifndef MINILD_RISCV_ELF_ABI_H
#define MINILD_RISCV_ELF_ABI_H

#include <stdbool.h>
#include <stdint.h>

/*
 * RISC-V e_flags are ABI declarations, not an arbitrary bitset.
 * EF_RISCV_RVC and EF_RISCV_TSO are merged by union, matching GNU BFD.
 * FLOAT_ABI is a multi-bit mutually exclusive encoding
 * (soft/single/double/quad).
 *
 * `have_current` is false only for the first admitted object; `merged_out`
 * then gets that object's flags without inventing a default ABI.
 * GNU BFD does not apply code ABI flags from a later object containing no
 * loadable executable bytes, so `incoming_has_code=false` retains `current`.
 */
bool minild_riscv_elf_abi_merge(uint32_t current,
                               uint32_t incoming,
                               bool have_current,
                               bool incoming_has_code,
                               uint32_t *merged_out,
                               const char **reason_out);

#endif
