#include "target/loongarch64/frame_emit.h"
#include <stdio.h>

/* Native emitter contract fixture.  The output is valid LoongArch assembly;
 * the caller checks both the optimized si14<<2 forms and their safe fallback. */
int main(void) {
    return !(
        minic_loongarch64_emit_sp_mem(stdout, "ld.d", "$t0", 2048U) &&
        minic_loongarch64_emit_sp_mem(stdout, "st.d", "$t1", 32760U) &&
        minic_loongarch64_emit_sp_mem(stdout, "ld.w", "$t2", 2052U) &&
        minic_loongarch64_emit_frame_mem(stdout, "st.w", "$t4", 32764U, true) &&
        minic_loongarch64_emit_frame_mem(stdout, "ld.d", "$t5", 4096U, true) &&
        /* No unsigned word pointer load and no unaligned double pointer form. */
        minic_loongarch64_emit_sp_mem(stdout, "ld.wu", "$t0", 2052U) &&
        minic_loongarch64_emit_sp_mem(stdout, "ld.d", "$t1", 2052U) &&
        /* Exactly past the pointer immediate limit uses the old fallback. */
        minic_loongarch64_emit_sp_mem(stdout, "ld.d", "$t2", 32768U) &&
        minic_loongarch64_emit_sp_adjust(stdout, 16U, false) &&
        minic_loongarch64_emit_sp_adjust(stdout, 16U, true) &&
        minic_loongarch64_emit_sp_adjust(stdout, 2048U, false) &&
        minic_loongarch64_emit_sp_adjust(stdout, 2048U, true) &&
        minic_loongarch64_emit_frame_address(stdout, "$t0", 0U, false) &&
        minic_loongarch64_emit_frame_address(stdout, "$t1", 2047U, true) &&
        minic_loongarch64_emit_frame_address(stdout, "$t2", 2048U, false));
}
