/* Direct negative and positive checks for AArch64 static clone ASM policy.
 * Build with src/target/aarch64/asm_contract.c under -ffunction-sections. */
#include "target/aarch64/asm_contract.h"
#include <stdio.h>
#include <string.h>

static int expect(const char *name, bool got, bool wanted)
{
    if (got == wanted) return 0;
    fprintf(stderr, "%s: clone policy got=%u expected=%u\n",
            name, (unsigned)got, (unsigned)wanted);
    return 1;
}

int main(void)
{
    MinicInlineAsmOperand output = {0}, input = {0};
    MinicInlineAsm source = {0};
    int failures = 0;

    output.constraint_text = "=r";
    output.constraint_length = 2U;
    input.constraint_text = "0";
    input.constraint_length = 1U;
    source.template_text = "";
    source.template_length = 0U;
    source.outputs = &output;
    source.output_count = 1U;
    source.inputs = &input;
    source.input_count = 1U;

    failures += expect("tied-register-empty", minic_aarch64_clone_safe_opaque_asm(&source), true);
    source.template_text = "nop";
    source.template_length = 3U;
    failures += expect("nonempty-template", minic_aarch64_clone_safe_opaque_asm(&source), false);
    source.template_text = "";
    source.template_length = 0U;
    source.has_memory_clobber = true;
    failures += expect("memory-clobber", minic_aarch64_clone_safe_opaque_asm(&source), false);
    source.has_memory_clobber = false;
    source.clobber_count = 1U;
    failures += expect("other-clobber", minic_aarch64_clone_safe_opaque_asm(&source), false);
    source.clobber_count = 0U;
    source.register_clobber_count = 1U;
    failures += expect("register-clobber", minic_aarch64_clone_safe_opaque_asm(&source), false);
    source.register_clobber_count = 0U;
    source.is_goto = true;
    failures += expect("asm-goto", minic_aarch64_clone_safe_opaque_asm(&source), false);
    source.is_goto = false;
    source.label_count = 1U;
    failures += expect("named-label", minic_aarch64_clone_safe_opaque_asm(&source), false);
    source.label_count = 0U;
    output.constraint_text = "+r";
    failures += expect("readwrite-output", minic_aarch64_clone_safe_opaque_asm(&source), false);
    output.constraint_text = "=r";
    input.constraint_text = "r";
    failures += expect("untied-input", minic_aarch64_clone_safe_opaque_asm(&source), false);
    input.constraint_text = "0";
    source.output_count = 2U;
    failures += expect("multiple-outputs", minic_aarch64_clone_safe_opaque_asm(&source), false);

    source.output_count = 0U;
    source.input_count = 0U;
    source.is_volatile = true;
    failures += expect("volatile-empty", minic_aarch64_clone_safe_opaque_asm(&source), true);
    source.has_memory_clobber = true;
    failures += expect("volatile-memory-clobber", minic_aarch64_clone_safe_opaque_asm(&source), false);
    source.has_memory_clobber = false;
    source.is_volatile = false;
    failures += expect("nonvolatile-zero-operands", minic_aarch64_clone_safe_opaque_asm(&source), false);
    return failures == 0 ? 0 : 1;
}
