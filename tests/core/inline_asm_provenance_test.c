#include "core/core_ir.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Core ASM owns its sealed machine relocation identity. An absent symbol is
 * NULL, and a separate ASM record must never borrow the producer's C ID. */
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
             function.inline_asms[first].deferred_immediate_symbol == NULL &&
             function.inline_asms[second].deferred_immediate_symbol == NULL &&
             function.inline_asms[first].goto_target == MINIC_CORE_BLOCK_INVALID &&
             function.inline_asms[second].goto_second_target == MINIC_CORE_BLOCK_INVALID;
    }
    if (ok) {
        const char symbol[] = "__minic_deferred_asm_immediate_0_0";
        function.inline_asms[second].deferred_immediate_symbol = malloc(sizeof(symbol));
        ok = function.inline_asms[second].deferred_immediate_symbol != NULL;
        if (ok) {
            (void)memcpy(function.inline_asms[second].deferred_immediate_symbol,
                         symbol, sizeof(symbol));
            ok = strcmp(function.inline_asms[second].deferred_immediate_symbol,
                        symbol) == 0 &&
                 function.inline_asms[first].deferred_immediate_symbol == NULL;
        }
    }
    minic_core_function_destroy(&function);
    if (!ok) {
        (void)fprintf(stderr, "FAIL Core inline ASM sealed symbol ownership\n");
        return 1;
    }
    (void)puts("MINIC_CORE_INLINE_ASM_PROVENANCE=PASS absent=NULL owned-symbol-zero=1");
    return 0;
}
