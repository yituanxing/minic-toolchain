#include "target/riscv64/spill_layout.h"
#include "target/common/core_spill_layout.h"
#include "target/data_layout.h"
/* RV64 original 8/16-byte block-local spill API/behavior stays unchanged. */
bool minic_riscv64_block_reused_spill_layout(const MinicC0Program *program,
                                           const MinicCoreFunction *function,
                                           const MinicCoreBlock *block,
                                           size_t value_base_offset,
                                           size_t *value_offsets,
                                           size_t *peak_bytes) {
    return minic_core_block_reused_spill_layout(
        program, function, block, minic_default_data_layout(), 8U,
        value_base_offset, value_offsets, peak_bytes);
}
