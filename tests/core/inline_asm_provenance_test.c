#include "core/core_ir.h"

#include <stdint.h>
#include <stdio.h>

/* A zero-initialized Core ASM record must never pretend it originated from
 * frontend asm statement zero.  A proven caller may install that actual id;
 * another Core record must retain the absent-provenance sentinel. */
int main(void) {
    MinicCoreFunction function;
    MinicCoreInlineAsmId first, second;
    bool ok;

    minic_core_function_initialize(&function);
    ok = minic_core_function_add_opaque_inline_asm(
             &function, "vld $vr0, %0", 12U, true, false, &first) &&
         minic_core_function_add_opaque_inline_asm(
             &function, "vst $vr0, %0", 12U, true, true, &second);
    if (ok) {
        ok = first == 0U && second == 1U &&
             function.inline_asms[first].source_inline_asm_id == SIZE_MAX &&
             function.inline_asms[second].source_inline_asm_id == SIZE_MAX &&
             function.inline_asms[first].goto_target == MINIC_CORE_BLOCK_INVALID &&
             function.inline_asms[second].goto_second_target == MINIC_CORE_BLOCK_INVALID;
    }
    if (ok) {
        function.inline_asms[second].source_inline_asm_id = 0U;
        ok = function.inline_asms[second].source_inline_asm_id == 0U &&
             function.inline_asms[first].source_inline_asm_id == SIZE_MAX;
    }
    minic_core_function_destroy(&function);
    if (!ok) {
        (void)fprintf(stderr, "FAIL Core inline ASM provenance sentinel\n");
        return 1;
    }
    (void)puts("MINIC_CORE_INLINE_ASM_PROVENANCE=PASS absent=SIZE_MAX real-source-zero=0");
    return 0;
}
