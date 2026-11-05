#include "target/x86_64/machine_opcode.h"
#include "target/common/machine_gnu_asm.h"
#include "target/common/machine_source_asm_template.h"
#include "target/x86_64/asm_contract.h"

#include <inttypes.h>
#include <limits.h>
#include <stdio.h>

static bool x86_emit_register(FILE *, const MinicMachineOperand *);
static bool x86_emit_address(FILE *, const MinicMachineAddress *);
static const char *x86_gp_name(const MinicMachineRegister *);

static bool x86_source_accumulator_form(
    const MinicMachineFunction *function,
    const MinicMachineInstruction *instruction, void *context) {
    (void)context;
    return function != NULL && function->target != NULL &&
        (instruction->source_asm_machine_form_id ==
             MINIC_X86_MACHINE_ASM_MULDIV_U64 ||
         instruction->source_asm_machine_form_id ==
             MINIC_X86_MACHINE_ASM_KASLR_MUL) &&
        minic_x86_64_machine_validate_source_asm(function->target,
            instruction, NULL);
}

static bool x86_source_accumulator_binding(
    const MinicMachineFunction *function,
    const MinicMachineInstruction *instruction,
    size_t index, void *context) {
    return index < instruction->operand_count &&
        x86_source_accumulator_form(function, instruction, context);
}

static bool x86_source_accumulator_operand(
    FILE *output, const MinicMachineFunction *function,
    const MinicMachineInstruction *instruction,
    const MinicMachineGNUAsmLabels *labels, size_t index,
    unsigned char modifier, void *context) {
    (void)labels;
    if (modifier != 0U ||
        !x86_source_accumulator_binding(function, instruction,
            index, context)) return false;
    return output == NULL ||
        x86_emit_register(output, &instruction->operands[index]);
}

static const MinicMachineSourceAsmTemplatePolicy x86_accumulator_policy = {
    x86_source_accumulator_form,
    x86_source_accumulator_binding,
    x86_source_accumulator_operand,
    NULL
};

/* Matched x86 source forms are distinct from the accumulator family: CPUID
 * uses four tied 32-bit named hardware registers; MULACC reads a true
 * explicit memory operand in R11 and writes three tied 64-bit registers;
 * matched-memory writes pointer-indirected arithmetic using caller-scratch
 * registers with no additional register allocator or textual bypass.
 * The target verifier independently proves every binding before this generic
 * GNU placeholder substitution may print a source spelling. */
static bool x86_source_matched_form(
    const MinicMachineFunction *function,
    const MinicMachineInstruction *instruction, void *context) {
    (void)context;
    return function != NULL && function->target != NULL &&
        (instruction->source_asm_machine_form_id ==
             MINIC_X86_MACHINE_ASM_CPUID_MATCHED ||
         instruction->source_asm_machine_form_id ==
             MINIC_X86_MACHINE_ASM_MULACC_MATCHED ||
         instruction->source_asm_machine_form_id ==
             MINIC_X86_MACHINE_ASM_MATCHED_MEMORY) &&
        minic_x86_64_machine_validate_source_asm(function->target,
            instruction, NULL);
}

static bool x86_source_matched_binding(
    const MinicMachineFunction *function,
    const MinicMachineInstruction *instruction,
    size_t index, void *context) {
    return index < instruction->operand_count &&
        x86_source_matched_form(function, instruction, context);
}

static bool x86_source_matched_operand(
    FILE *output, const MinicMachineFunction *function,
    const MinicMachineInstruction *instruction,
    const MinicMachineGNUAsmLabels *labels, size_t index,
    unsigned char modifier, void *context) {
    const MinicMachineOperand *operand;
    (void)labels;
    if (modifier != 0U ||
        !x86_source_matched_binding(function, instruction, index, context))
        return false;
    operand = &instruction->operands[index];
    if (output == NULL) return operand->kind == MINIC_MACHINE_OPERAND_REGISTER ||
        operand->kind == MINIC_MACHINE_OPERAND_ADDRESS;
    if (operand->kind == MINIC_MACHINE_OPERAND_REGISTER)
        return x86_emit_register(output, operand);
    return operand->kind == MINIC_MACHINE_OPERAND_ADDRESS &&
        x86_emit_address(output, &operand->value.address);
}

static const MinicMachineSourceAsmTemplatePolicy x86_matched_policy = {
    x86_source_matched_form,
    x86_source_matched_binding,
    x86_source_matched_operand,
    NULL
};

/* The GNU bit-scan family has five Source-qualified width/tie forms. The
 * source `rep; bsf`, `bsr`, `bsfl`, `bsrl`, `bsrq` remains the exact GNU
 * template; every placeholder binds to the preverified physical RAX/R10. */
static bool x86_source_bit_scan_form(
    const MinicMachineFunction *function,
    const MinicMachineInstruction *instruction, void *context) {
    (void)context;
    return function != NULL && function->target != NULL &&
        instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_BITSCAN_MATCHED &&
        minic_x86_64_machine_validate_source_asm(function->target,
            instruction,NULL);
}

static bool x86_source_bit_scan_binding(
    const MinicMachineFunction *function,
    const MinicMachineInstruction *instruction, size_t index,
    void *context) {
    return index < instruction->operand_count &&
        x86_source_bit_scan_form(function,instruction,context);
}

static bool x86_source_bit_scan_operand(
    FILE *output, const MinicMachineFunction *function,
    const MinicMachineInstruction *instruction,
    const MinicMachineGNUAsmLabels *labels, size_t index,
    unsigned char modifier, void *context) {
    (void)labels;
    if (!x86_source_bit_scan_binding(function,instruction,index,context) ||
        (modifier != 0U && !(modifier == 'q' && index == 0U &&
            instruction->source_asm_machine_form_variant ==
                MINIC_X86_BIT_SCAN_BSR64_READWRITE)))
        return false;
    return output == NULL ||
        x86_emit_register(output,&instruction->operands[index]);
}

static const MinicMachineSourceAsmTemplatePolicy x86_bit_scan_policy = {
    x86_source_bit_scan_form,
    x86_source_bit_scan_binding,
    x86_source_bit_scan_operand,
    NULL
};

/* `%P1` is GNU's memory operand sans size suffix. The Source qualifier has
 * already checked the exact ELF alternative syntax; the x86 physical
 * verifier seals an R10-relative address and an integer zero operand. */
static bool x86_source_prefetchw_alt_form(
    const MinicMachineFunction *function,
    const MinicMachineInstruction *instruction, void *context) {
    (void)context;
    return function != NULL && function->target != NULL &&
        instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_PREFETCHW_ALT &&
        minic_x86_64_machine_validate_source_asm(function->target,
            instruction,NULL);
}

static bool x86_source_prefetchw_alt_binding(
    const MinicMachineFunction *function,
    const MinicMachineInstruction *instruction, size_t index,
    void *context) {
    return index < instruction->operand_count &&
        x86_source_prefetchw_alt_form(function,instruction,context);
}

static bool x86_source_prefetchw_alt_operand(
    FILE *output, const MinicMachineFunction *function,
    const MinicMachineInstruction *instruction,
    const MinicMachineGNUAsmLabels *labels, size_t index,
    unsigned char modifier, void *context) {
    (void)labels;
    if (!x86_source_prefetchw_alt_binding(function,instruction,index,context))
        return false;
    if (index == 0U && modifier == 0U)
        return output == NULL || fputs("$0",output) != EOF;
    if (index != 1U || modifier != 'P') return false;
    return output == NULL ||
        x86_emit_address(output,&instruction->operands[1].value.address);
}

static const MinicMachineSourceAsmTemplatePolicy x86_prefetchw_alt_policy = {
    x86_source_prefetchw_alt_form,
    x86_source_prefetchw_alt_binding,
    x86_source_prefetchw_alt_operand,
    NULL
};

/* Both CMPXCHG16B forms use actual R11-relative 16-byte memory, fixed
 * RAX:RDX and RBX:RCX pairs, and the complete source-matched Linux patch
 * metadata. Only `%[ptr]`, `%P[var]` and `%[var]` can interpolate their
 * preverified memory operand; other bytes are immutable exact GNU source. */
static bool x86_source_cmpxchg128_form(
    const MinicMachineFunction *function,
    const MinicMachineInstruction *instruction, void *context) {
    (void)context;
    return function != NULL && function->target != NULL &&
        instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_CMPXCHG128 &&
        minic_x86_64_machine_validate_source_asm(function->target,
            instruction,NULL);
}

static bool x86_source_cmpxchg128_binding(
    const MinicMachineFunction *function,
    const MinicMachineInstruction *instruction, size_t index,
    void *context) {
    return index < instruction->operand_count &&
        x86_source_cmpxchg128_form(function,instruction,context);
}

static bool x86_source_cmpxchg128_operand(
    FILE *output, const MinicMachineFunction *function,
    const MinicMachineInstruction *instruction,
    const MinicMachineGNUAsmLabels *labels, size_t index,
    unsigned char modifier, void *context) {
    const size_t at =
        instruction->source_asm_machine_form_variant == 2U ? 0U : 1U;
    (void)labels;
    if (!x86_source_cmpxchg128_binding(function,instruction,index,context) ||
        index != at || (modifier != 0U && modifier != 'P'))
        return false;
    return output == NULL ||
        x86_emit_address(output,&instruction->operands[at].value.address);
}

static const MinicMachineSourceAsmTemplatePolicy x86_cmpxchg128_policy = {
    x86_source_cmpxchg128_form,
    x86_source_cmpxchg128_binding,
    x86_source_cmpxchg128_operand,
    NULL
};

/* Crash-snapshot source forms are a finite hardware family, not arbitrary
 * GNU passthrough. The 18 exact source variants independently seal captured
 * GPR, FLAGS or RIP; only the verified output ADDRESS/register is printed. */
static bool x86_source_crash_register_form(
    const MinicMachineFunction *function,
    const MinicMachineInstruction *instruction, void *context) {
    (void)context;
    return function != NULL && function->target != NULL &&
        instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_CRASH_REGISTER &&
        minic_x86_64_machine_validate_source_asm(function->target,
            instruction,NULL);
}

static bool x86_source_crash_register_binding(
    const MinicMachineFunction *function,
    const MinicMachineInstruction *instruction, size_t index,
    void *context) {
    return index < instruction->operand_count &&
        x86_source_crash_register_form(function,instruction,context);
}

static bool x86_source_crash_register_operand(
    FILE *output, const MinicMachineFunction *function,
    const MinicMachineInstruction *instruction,
    const MinicMachineGNUAsmLabels *labels, size_t index,
    unsigned char modifier, void *context) {
    const MinicMachineOperand *operand;
    (void)labels;
    if (index != 0U || modifier != 0U ||
        !x86_source_crash_register_binding(function,instruction,index,context))
        return false;
    operand = &instruction->operands[0];
    if (output == NULL) return operand->kind == MINIC_MACHINE_OPERAND_ADDRESS ||
        operand->kind == MINIC_MACHINE_OPERAND_REGISTER;
    if (operand->kind == MINIC_MACHINE_OPERAND_ADDRESS)
        return x86_emit_address(output,&operand->value.address);
    return x86_emit_register(output,operand);
}

static const MinicMachineSourceAsmTemplatePolicy x86_crash_register_policy = {
    x86_source_crash_register_form,
    x86_source_crash_register_binding,
    x86_source_crash_register_operand,
    NULL
};

/* GNU `%c` immediate interpolation for two exact Linux BUG/WARN templates.
 * This Source ASM form carries four typed, independently checked operands;
 * no x86 emitted instruction is inferred from an unconstrained C string.
 * The `.long %c0 - .` directive asks GAS for its genuine PC32 relocation. */
static bool x86_source_bug_c_form(
    const MinicMachineFunction *function,
    const MinicMachineInstruction *instruction, void *context) {
    (void)context;
    return function != NULL && function->target != NULL &&
        instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_BUG_TABLE_C_IMMEDIATE &&
        minic_x86_64_machine_validate_source_asm(function->target,
            instruction,NULL);
}

static bool x86_source_bug_c_binding(
    const MinicMachineFunction *function,
    const MinicMachineInstruction *instruction, size_t index,
    void *context) {
    return index < 4U &&
        x86_source_bug_c_form(function,instruction,context);
}

static bool x86_source_bug_c_operand(
    FILE *output, const MinicMachineFunction *function,
    const MinicMachineInstruction *instruction,
    const MinicMachineGNUAsmLabels *labels, size_t index,
    unsigned char modifier, void *context) {
    const MinicMachineOperand *op;
    (void)labels;
    if (modifier != 'c' ||
        !x86_source_bug_c_binding(function,instruction,index,context))
        return false;
    op = &instruction->operands[index];
    if (output == NULL) return index == 0U ?
        op->kind == MINIC_MACHINE_OPERAND_SYMBOL :
        op->kind == MINIC_MACHINE_OPERAND_INTEGER;
    if (index != 0U) return
        fprintf(output,"%" PRId64,op->value.integer) >= 0;
    if (fputs(op->value.symbol.name,output) == EOF) return false;
    if (op->value.symbol.addend != 0)
        return fprintf(output,"%+" PRId64,
            op->value.symbol.addend) >= 0;
    return true;
}

static const MinicMachineSourceAsmTemplatePolicy x86_bug_c_policy = {
    x86_source_bug_c_form,
    x86_source_bug_c_binding,
    x86_source_bug_c_operand,
    NULL
};

/* GNU's original safe-MSR exception-register macro expands the selected
 * physical `err` register inside GAS. Source qualification is exact and the
 * MachineIR verifier independently fixes ECX/EAX/EDX/R11 before interpolation.
 * We preserve the real `.irp` and PC-relative __ex_table relocations. */
static bool x86_source_msr_form(const MinicMachineFunction *function,
    const MinicMachineInstruction *instruction, void *context) {
    (void)context;
    return function != NULL && function->target != NULL &&
        instruction->source_asm_machine_form_id == MINIC_X86_MACHINE_ASM_MSR &&
        minic_x86_64_machine_validate_source_asm(function->target,
            instruction,NULL);
}

static bool x86_source_msr_binding(const MinicMachineFunction *function,
    const MinicMachineInstruction *instruction, size_t index, void *context) {
    return index < instruction->operand_count &&
        x86_source_msr_form(function,instruction,context);
}

static bool x86_source_msr_operand(FILE *output,
    const MinicMachineFunction *function,
    const MinicMachineInstruction *instruction,
    const MinicMachineGNUAsmLabels *labels, size_t index,
    unsigned char modifier, void *context) {
    (void)labels;
    if (modifier != 0U ||
        !x86_source_msr_binding(function,instruction,index,context))
        return false;
    return output == NULL ||
        x86_emit_register(output,&instruction->operands[index]);
}

static const MinicMachineSourceAsmTemplatePolicy x86_msr_policy = {
    x86_source_msr_form,
    x86_source_msr_binding,
    x86_source_msr_operand,
    NULL
};

static bool x86_source_percpu_form(const MinicMachineFunction *function,
    const MinicMachineInstruction *instruction, void *context) {
    (void)context;
    return function != NULL && function->target != NULL &&
        instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_PERCPU_GS &&
        minic_x86_64_machine_validate_source_asm(function->target,
            instruction,NULL);
}

static bool x86_source_percpu_binding(const MinicMachineFunction *function,
    const MinicMachineInstruction *instruction, size_t index, void *context) {
    return index < instruction->operand_count &&
        x86_source_percpu_form(function,instruction,context);
}

static bool x86_source_percpu_operand(FILE *output,
    const MinicMachineFunction *function,
    const MinicMachineInstruction *instruction,
    const MinicMachineGNUAsmLabels *labels, size_t index,
    unsigned char modifier, void *context) {
    const MinicMachineOperand *binding;
    (void)labels;
    if (!x86_source_percpu_binding(function,instruction,index,context))
        return false;
    binding = &instruction->operands[index];
    /* Linux percpu_stable_op uses GNU %P[var] with a constrained pointer
     * register, which prints the GS address dereference (%r10). It is
     * never the ordinary memory-lvalue %[var] mode. */
    if (modifier == (unsigned char)'P') {
        if (index != 1U ||
            minic_x86_64_asm_percpu_stable_width(
                instruction->source_asm_template,
                instruction->source_asm_length) == 0U ||
            binding->kind != MINIC_MACHINE_OPERAND_REGISTER)
            return false;
        return output == NULL || fputs("(%r10)",output) != EOF;
    }
    if (modifier != 0U) return false;
    if (output == NULL) return binding->kind ==
            MINIC_MACHINE_OPERAND_ADDRESS ||
        binding->kind == MINIC_MACHINE_OPERAND_REGISTER;
    return binding->kind == MINIC_MACHINE_OPERAND_ADDRESS ?
        x86_emit_address(output,&binding->value.address) :
        x86_emit_register(output,binding);
}

static const MinicMachineSourceAsmTemplatePolicy x86_percpu_policy = {
    x86_source_percpu_form,
    x86_source_percpu_binding,
    x86_source_percpu_operand,
    NULL
};

/* The fully qualified futex family owns 9 fault-site records across six
 * original GNU forms. Emit real section relocations with the ABI-mandated
 * R8D recovery register (architectural register number 8) rather than
 * copying an unchecked `.macro` containing operand or register text. */
static bool x86_emit_futex_extable_record(FILE *out,
    unsigned int fault, unsigned int recovery) {
    return fprintf(out,
        "  .pushsection \"__ex_table\",\"a\"\n"
        "  .balign 4\n"
        "  .long (%ub) - .\n"
        "  .long (%ub) - .\n"
        "  .long (17 | ((-14) << 16)) + (8 << 8)\n"
        "  .popsection\n", fault, recovery) >= 0;
}

static bool x86_emit_futex_extable(FILE *out,
    const MinicMachineFunction *function,
    const MinicMachineInstruction *instruction) {
    const unsigned int variant = instruction->source_asm_machine_form_variant;
    const bool loop = variant >= 4U;
    const char *operation = variant == 4U ? "orl" :
        variant == 5U ? "andl" : "xorl";
    if (out == NULL || function == NULL ||
        !minic_x86_64_machine_validate_source_asm(function->target,
            instruction,NULL)) return false;
    if (variant == 1U || variant == 3U || loop) {
        if (fputs("1:\n",out) == EOF) return false;
        if (loop &&
            (fputs("  movl (%r10), %eax\n"
                   "2:\n"
                   "  movl %eax, %r9d\n",out) == EOF ||
             fprintf(out,"  %s %%r11d, %%r9d\n",operation) < 0 ||
             fputs("3:\n",out) == EOF)) return false;
        if (fputs("  .pushsection .smp_locks,\"a\"\n"
                  "  .balign 4\n"
                  "  .long 671f - .\n"
                  "  .popsection\n"
                  "671:\n"
                  "  lock; ",out) == EOF) return false;
        if (variant == 1U || loop) {
            if (fputs("cmpxchgl %r9d, (%r10)\n",out) == EOF) return false;
            if (loop && fputs("  jnz 2b\n4:\n",out) == EOF) return false;
        } else if (fputs("xaddl %eax, (%r10)\n",out) == EOF) return false;
    } else if (fputs("1:\n  xchgl %eax, (%r10)\n",out) == EOF)
        return false;
    if (!loop && fputs("2:\n",out) == EOF) return false;
    return x86_emit_futex_extable_record(out,1U,loop ? 4U : 2U) &&
        (!loop || x86_emit_futex_extable_record(out,3U,4U));
}

/* Only the seven Source-qualified, operand-free Linux barrier forms are
 * serialized here. Variant and source text are checked independently by the
 * target verifier before any function output is written. */
/* Linux LOCK_PREFIX emits a real .smp_locks relocation identifying the
 * exact following instruction; no preformatted source template is copied. */
static bool x86_emit_scalar_smp_lock(FILE *out) {
    return fputs("  .pushsection .smp_locks,\"a\"\n"
        "  .balign 4\n"
        "  .long 671f - .\n"
        "  .popsection\n"
        "671:\n"
        "  lock; ",out) != EOF;
}

static bool x86_emit_source_asm(FILE *out,
    const MinicMachineFunction *function,
    const MinicMachineInstruction *instruction,
    const MinicMachineGNUAsmLabels *labels, void *context) {
    static const char *const hardware[] = {
        "  lock; addl $0, -4(%rsp)\n", "  mfence\n", "  lfence\n",
        "  sfence\n", "  pause\n", "  rep; nop\n", "  fninit\n"
    };
    unsigned int variant;
    (void)labels;
    if (out == NULL || function == NULL ||
        !minic_x86_64_machine_validate_source_asm(function->target,
            instruction, context)) return false;
    if (instruction->source_asm_machine_form_id == 0U)
        return true; /* Empty volatile asm has an effect but zero bytes. */
    if (instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_PUT_USER_CALL ||
        instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_GET_USER_CALL)
        return true; /* Physical opcodes already emitted this private CALL. */
    if (instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_MULDIV_U64)
        /* GNU's exact two-instruction form has a semicolon separator;
         * preserving a physical instruction on each line also preserves
         * the source-level ABI test's observable instruction boundaries. */
        return fputs("  mulq %r10\n  divq %r11\n", out) != EOF;
    if (instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_KASLR_MUL)
        return fputs("  ", out) != EOF &&
            minic_machine_source_asm_template_emit(out, function,
                instruction, labels, &x86_accumulator_policy) &&
            fputc('\n', out) != EOF;
    if (instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_CPUID_MATCHED ||
        instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_MULACC_MATCHED ||
        instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_MATCHED_MEMORY)
        return fputs("  ", out) != EOF &&
            minic_machine_source_asm_template_emit(out, function,
                instruction, labels, &x86_matched_policy) &&
            fputc('\n', out) != EOF;
    if (instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_BITSCAN_MATCHED) {
        static const char *const bit_scan_hardware[] = {
            "  rep; bsfq %r10, %rax\n",
            "  bsrq %r10, %rax\n",
            "  bsfl %r10d, %eax\n",
            "  bsrl %r10d, %eax\n",
            "  bsrq %r10, %rax\n"
        };
        /* Exact source template, width, operands, ties and FLAGS effect
         * were sealed by MachineIR; produce deterministic ISA syntax. */
        variant = instruction->source_asm_machine_form_variant;
        if (variant < MINIC_X86_BIT_SCAN_BSF64 ||
            variant > MINIC_X86_BIT_SCAN_BSR64_READWRITE)
            return false;
        return minic_machine_source_asm_template_preflight(function,
            instruction,&x86_bit_scan_policy) &&
            fputs(bit_scan_hardware[variant - 1U],out) != EOF;
    }
    if (instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_PREFETCHW_ALT)
        return fputs("  ",out) != EOF &&
            minic_machine_source_asm_template_emit(out,function,
                instruction,labels,&x86_prefetchw_alt_policy) &&
            fputc('\n',out) != EOF;
    if (instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_CMPXCHG128)
        return fputs("  ",out) != EOF &&
            minic_machine_source_asm_template_emit(out,function,
                instruction,labels,&x86_cmpxchg128_policy) &&
            fputc('\n',out) != EOF;
    if (instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_CRASH_REGISTER) {
        const unsigned int form = instruction->source_asm_machine_form_variant;
        /* Canonical GNU physical ISA spellings preserve the historical
         * observed instruction boundaries and R10/R11 independent scratch.
         * The complete original Source spelling and matching constraints
         * are still preflighted before printing any hardware instruction. */
        if (!minic_machine_source_asm_template_preflight(function,
                instruction,&x86_crash_register_policy)) return false;
        if (form <= 16U) {
            const char *reg = x86_gp_name(&instruction->implicit[0].reg);
            return reg != NULL && fprintf(out,"  movq %s, ",reg) >= 0 &&
                x86_emit_address(out,
                    &instruction->operands[0].value.address) &&
                fputc('\n',out) != EOF;
        }
        if (form == 17U)
            return fputs("  pushfq\n  popq (%r11)\n",out) != EOF;
        return form == 18U && fputs("  leaq 0(%rip), %rax\n",out) != EOF;
    }
    if (instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_FUTEX_EXTABLE)
        return x86_emit_futex_extable(out,function,instruction);
    if (instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_GS_TIED_ALU) {
        static const char *const ops[5] = {
            "add", "sub", "and", "or", "xor"
        };
        const unsigned int form = instruction->source_asm_machine_form_variant;
        unsigned int width;
        if (form < 1U || form > 5U ||
            !minic_x86_64_machine_validate_source_asm(function->target,
                instruction,NULL)) return false;
        width = instruction->operands[0].value.physical.reg.width_bits;
        if (fprintf(out,"  %s%c ",ops[form-1U],
                width == 32U ? 'l' : 'q') < 0 ||
            !x86_emit_address(out,
                &instruction->operands[1].value.address)) return false;
        return fprintf(out,", %s\n",width == 32U ? "%eax" : "%rax") >= 0;
    }
    if (instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_IP_HERE)
        /* Keep GNU's actual instruction-relative counter, not the address
         * of the containing function or a fabricated linker symbol. */
        return fputs("  leaq 0(%rip), %rax\n",out) != EOF;
    if (instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_BUG_TABLE_C_IMMEDIATE)
        return fputs("  ",out) != EOF &&
            minic_machine_source_asm_template_emit(out,function,
                instruction,labels,&x86_bug_c_policy) &&
            fputc('\n',out) != EOF;
    if (instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_NOP_CONST_I)
        return fputs("  nop\n",out) != EOF;
    if (instruction->source_asm_machine_form_id == MINIC_X86_MACHINE_ASM_MMIO_READ ||
        instruction->source_asm_machine_form_id == MINIC_X86_MACHINE_ASM_MMIO_WRITE ||
        instruction->source_asm_machine_form_id == MINIC_X86_MACHINE_ASM_ATOMIC_XCHG) {
        static const char *const reads[] = {
            "  movb (%r10), %al\n", "  movw (%r10), %ax\n",
            "  movl (%r10), %eax\n", "  movq (%r10), %rax\n"
        };
        static const char *const writes[] = {
            "  movb %al, (%r10)\n", "  movw %ax, (%r10)\n",
            "  movl %eax, (%r10)\n", "  movq %rax, (%r10)\n"
        };
        static const char *const exchanges[] = {
            "  xchgb %al, (%r10)\n", "  xchgw %ax, (%r10)\n",
            "  xchgl %eax, (%r10)\n", "  xchgq %rax, (%r10)\n"
        };
        const uint32_t form_id = instruction->source_asm_machine_form_id;
        const unsigned int width = instruction->source_asm_machine_form_variant;
        const unsigned int index = width == 1U ? 0U :
            width == 2U ? 1U : width == 4U ? 2U : 3U;
        if (width != 1U && width != 2U && width != 4U && width != 8U)
            return false;
        return fputs(form_id == MINIC_X86_MACHINE_ASM_MMIO_READ ?
            reads[index] : form_id == MINIC_X86_MACHINE_ASM_MMIO_WRITE ?
                writes[index] : exchanges[index],out) != EOF;
    }
    if (instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_FDIV_BUG) {
        /* Proven original x87 kernel test: binary64 loads, stacked DIV/MUL,
         * FISTP store with live x87 rounding, FWAIT, and both FNINITs. */
        return fputs("  fninit\n"
            "  fldl (%r11)\n"
            "  fdivl (%rax)\n"
            "  fmull (%rax)\n"
            "  fldl (%r11)\n"
            "  fsubp %st, %st(1)\n"
            "  fistpl (%r10)\n"
            "  fwait\n"
            "  fninit\n",out) != EOF;
    }
    if (instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_RANDOM_SEED) {
        /* The source operand name is sealed to RAX by MachineIR; RDRAND/
         * RDSEED update CF in hardware and the selected SETC reads it. */
        const unsigned int form = instruction->source_asm_machine_form_variant;
        if (form != 1U && form != 2U) return false;
        return fputs(form == 1U ? "  rdrand %rax\n" :
            "  rdseed %rax\n",out) != EOF;
    }
    if (instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_PORT_IO) {
        const MinicX86PortIo form = minic_x86_64_port_io_form(
            instruction->source_asm_template,
            instruction->source_asm_length);
        const MinicX86PortIoDesc *desc =
            minic_x86_64_port_io_description(form);
        const char *data;
        if (desc == NULL) return false;
        data = desc->width == 1U ? "%al" :
            desc->width == 2U ? "%ax" : "%eax";
        return desc->is_input ?
            fprintf(out,"  %s %%dx, %s\n",desc->opcode,data) >= 0 :
            fprintf(out,"  %s %s, %%dx\n",desc->opcode,data) >= 0;
    }
    if (instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_ATOMIC_RMW) {
        const MinicX86AtomicRmw form = minic_x86_64_atomic_rmw_form(
            instruction->source_asm_template,
            instruction->source_asm_length);
        const MinicX86AtomicRmwDesc *desc =
            minic_x86_64_atomic_rmw_description(form);
        const char *value;
        if (desc == NULL || (desc->locked &&
            !x86_emit_scalar_smp_lock(out))) return false;
        value = desc->width == 4U ? "%r11d" : "%r11";
        return desc->unary ?
            fprintf(out,"%s%s (%%r10)\n",
                desc->locked ? "" : "  ",desc->opcode) >= 0 :
            fprintf(out,"%s%s %s, (%%r10)\n",
                desc->locked ? "" : "  ",desc->opcode,value) >= 0;
    }
    if (instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_RAW_CMPXCHG ||
        instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_TRYCAS) {
        const bool trycas = instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_TRYCAS;
        const MinicX86TryCas form = trycas ?
            minic_x86_64_trycas_form(
                instruction->source_asm_template,
                instruction->source_asm_length) :
            minic_x86_64_raw_cmpxchg_form(
                instruction->source_asm_template,
                instruction->source_asm_length);
        const MinicX86TryCasDesc *desc =
            minic_x86_64_trycas_desc(form);
        if (desc == NULL || (desc->locked &&
            !x86_emit_scalar_smp_lock(out))) return false;
        return fprintf(out,"%s%s %s, (%%r11)\n",
            desc->locked ? "" : "  ",desc->op,desc->new_reg) >= 0;
    }
    if (instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_BITOPS) {
        const MinicX86BitopsForm form = minic_x86_64_linux_bitops_form(
            instruction->source_asm_template,
            instruction->source_asm_length);
        const MinicX86BitopsDesc *desc =
            minic_x86_64_bitops_description(form);
        const bool byte_rmw = form == MINIC_X86_BITOPS_LOCK_OR_BYTE ||
            form == MINIC_X86_BITOPS_LOCK_AND_BYTE ||
            form == MINIC_X86_BITOPS_LOCK_XOR_BYTE;
        if (desc == NULL || (desc->is_locked &&
            !x86_emit_scalar_smp_lock(out))) return false;
        return fprintf(out,"%s%s %s, (%%r10)\n",
            desc->is_locked ? "" : "  ",desc->opcode,
            byte_rmw ? "%r11b" : "%r11") >= 0;
    }
    if (instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_ATOMIC_CC) {
        const MinicX86AtomicCC form = minic_x86_64_atomic_cc_form(
            instruction->source_asm_template,
            instruction->source_asm_length);
        const MinicX86AtomicCCDesc *desc =
            minic_x86_64_atomic_cc_description(form);
        const bool locked = minic_x86_64_atomic_cc_locked_form(
            instruction->source_asm_template,
            instruction->source_asm_length);
        const char *value;
        if (desc == NULL || (locked &&
            !x86_emit_scalar_smp_lock(out))) return false;
        value = desc->width == 4U ? "%r11d" : "%r11";
        return desc->has_value ?
            fprintf(out,"%s%s %s, (%%r10)\n",
                locked ? "" : "  ",desc->opcode,value) >= 0 :
            fprintf(out,"%s%s (%%r10)\n",
                locked ? "" : "  ",desc->opcode) >= 0;
    }
    if (instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_MXCSR) {
        /* Explicit 32-bit CPU control state transfer; the sole true
         * MachineIR memory operand is verified to be (%r10). */
        const unsigned int form = instruction->source_asm_machine_form_variant;
        if (form != 1U && form != 2U) return false;
        return fputs(form == 1U ? "  ldmxcsr (%r10)\n" :
            "  stmxcsr (%r10)\n",out) != EOF;
    }
    if (instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_UNWIND_CONTEXT) {
        /* ORC captures the live instruction/stack/frame pointers in three
         * separately validated 64-bit physical register outputs. */
        return fputs("  leaq 0(%rip), %rax\n"
            "  movq %rsp, %r10\n"
            "  movq %rbp, %r11\n",out) != EOF;
    }
    if (instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_PKRU) {
        /* Source and MachineIR validate the exact byte sequence and
         * EAX/ECX/EDX roles. Print only the two known CPU-state opcodes. */
        const unsigned int form = instruction->source_asm_machine_form_variant;
        if (form != 1U && form != 2U) return false;
        return fputs(form == 1U ? "  .byte 0x0f,0x01,0xee\n" :
            "  .byte 0x0f,0x01,0xef\n",out) != EOF;
    }
    if (instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_COUNTER) {
        const unsigned int form = minic_x86_64_linux_counter_form(
            instruction->source_asm_template,instruction->source_asm_length);
        static const char *const counters[] = {
            "  rdtsc\n", "  rdpmc\n", "  rdtscp\n", NULL, "  xgetbv\n"
        };
        if (form < 1U || form > 5U) return false;
        if (form == 4U) {
            /* Byte-for-byte Linux alternatives: patch records, section
             * boundaries and both replacement instruction bodies survive.
             * Validation above proves this is the sole exact admitted text. */
            return fwrite(instruction->source_asm_template,1U,
                instruction->source_asm_length,out) ==
                    instruction->source_asm_length &&
                fputc('\n',out) != EOF;
        }
        return fputs(counters[form-1U],out) != EOF;
    }
    if (instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_HWEIGHT) {
        const unsigned int bytes = minic_x86_64_hweight_alternative_form(
            instruction->source_asm_template,
            instruction->source_asm_length);
        if (bytes != 4U && bytes != 8U) return false;
        /* The exact admitted Linux alternative is serialized with feature
         * 151 explicitly, retaining genuine PREL32 ELF patch records and
         * the GNU software fallback. No arbitrary template substitution. */
        return fputs("661:\n",out) != EOF &&
            fprintf(out,"  call __sw_hweight%u\n",bytes*8U) >= 0 &&
            fputs("662:\n"
                  "  .skip -(((6651f-6641f)-(662b-661b)) > 0) * "
                  "((6651f-6641f)-(662b-661b)), 0x90\n"
                  "663:\n"
                  "  .pushsection .altinstructions,\"a\"\n"
                  "  .long 661b - .\n"
                  "  .long 6641f - .\n"
                  "  .4byte 151\n"
                  "  .byte 663b-661b\n"
                  "  .byte 6651f-6641f\n"
                  "  .popsection\n"
                  "  .pushsection .altinstr_replacement, \"ax\"\n"
                  "6641:\n",out) != EOF &&
            fputs(bytes == 4U ? "  popcntl %edi, %eax\n" :
                "  popcntq %rdi, %rax\n",out) != EOF &&
            fputs("6651:\n  .popsection\n",out) != EOF;
    }
    if (instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_MSR)
        return fputs("  ",out) != EOF &&
            minic_machine_source_asm_template_emit(out,function,
                instruction,labels,&x86_msr_policy) &&
            fputc('\n',out) != EOF;
    if (instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_PERCPU_GS)
        return fputs("  ",out) != EOF &&
            minic_machine_source_asm_template_emit(out,function,
                instruction,labels,&x86_percpu_policy) &&
            fputc('\n',out) != EOF;

    if (instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_ADD32_CARRY)
        return fputs("  addl %r10d, %eax\n  adcl $0, %eax\n",out) != EOF;
    if (instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_CHECKSUM)
        return fputs("  addl %r10d, %eax\n  adcl $0xffff, %eax\n",out) != EOF;
    if (instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_TCPUDP_NOFOLD)
        return fputs("  addl %r10d, %eax\n"
                     "  adcl %r11d, %eax\n"
                     "  adcl %r8d, %eax\n"
                     "  adcl $0, %eax\n",out) != EOF;
    if (instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_CSUM_PARTIAL) {
        const unsigned int form = minic_x86_64_linux_csum_partial_form(
            instruction->source_asm_template,
            instruction->source_asm_length);
        switch (form) {
        case 1U:
        case 6U:
            return fputs("  addq %r10, %rax\n"
                         "  adcq $0, %rax\n",out) != EOF;
        case 2U:
            return fputs("  addq (%r10), %rax\n"
                         "  adcq (%r11), %rax\n"
                         "  adcq (%r8), %rax\n"
                         "  adcq (%r9), %rax\n"
                         "  adcq (%rcx), %rax\n"
                         "  adcq $0, %rax\n",out) != EOF;
        case 3U:
        case 4U:
        case 5U: {
            unsigned int i, n = form == 3U ? 4U : form == 4U ? 2U : 1U;
            for (i = 0U; i < n; ++i) {
                if (fprintf(out,"  %sq %u*8(%%r10), %%rax\n",
                        i == 0U ? "add" : "adc",i) < 0) return false;
            }
            return fputs("  adcq $0, %rax\n",out) != EOF;
        }
        default: return false;
        }
    }
    if (instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_IP_FAST_CSUM)
        /* The exact source checksum template and the physical RAX/R9/R11
         * bindings have already passed independent MachineIR validation. */
        return fputs("  movl (%r9), %eax\n"
                     "  subl $4, %r11d\n"
                     "  jbe 2f\n"
                     "  addl 4(%r9), %eax\n"
                     "  adcl 8(%r9), %eax\n"
                     "  adcl 12(%r9), %eax\n"
                     "1: adcl 16(%r9), %eax\n"
                     "  leaq 4(%r9), %r9\n"
                     "  decl %r11d\n"
                     "  jne 1b\n"
                     "  adcl $0, %eax\n"
                     "  movl %eax, %r11d\n"
                     "  shrl $16, %eax\n"
                     "  addw %r11w, %ax\n"
                     "  adcl $0, %eax\n"
                     "  notl %eax\n"
                     "2:\n",out) != EOF;
    if (instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_GS_ATOMIC) {
        const MinicX86GsAtomicPlan plan = minic_x86_64_gs_atomic_plan(
            instruction->source_asm_template,
            instruction->source_asm_length);
        const char *suffix;
        const char *reg;
        if (plan.kind == MINIC_X86_GS_ATOMIC_NONE) return false;
        switch (plan.width) {
        case 1U: suffix = "b"; reg = "%al"; break;
        case 2U: suffix = "w"; reg = "%ax"; break;
        case 4U: suffix = "l"; reg = "%eax"; break;
        case 8U: suffix = "q"; reg = "%rax"; break;
        default: return false;
        }
        if (plan.kind == MINIC_X86_GS_ATOMIC_XADD_SMP)
            return fputs("  .pushsection .smp_locks,\"a\"\n"
                     "  .balign 4\n"
                     "  .long 671f - .\n"
                     "  .popsection\n"
                     "671:\n"
                     "  lock; xadd",out) != EOF &&
            fprintf(out,"%s %s, (%%r10)\n",suffix,reg) >= 0;

        if (plan.kind == MINIC_X86_GS_ATOMIC_XADD_LOCAL ||
            plan.kind == MINIC_X86_GS_ATOMIC_XADD_PERCPU ||
            plan.kind == MINIC_X86_GS_ATOMIC_CMPXCHG ||
            plan.kind == MINIC_X86_GS_ATOMIC_CMPXCHG_RETRY) {
            const char *segment = plan.kind ==
                MINIC_X86_GS_ATOMIC_XADD_LOCAL ? "" : "%gs:";
            const bool cmp = plan.kind == MINIC_X86_GS_ATOMIC_CMPXCHG ||
                plan.kind == MINIC_X86_GS_ATOMIC_CMPXCHG_RETRY;
            const bool retry = plan.kind ==
                MINIC_X86_GS_ATOMIC_CMPXCHG_RETRY;
            const char *value_reg = plan.width == 1U ? "%r11b" :
                plan.width == 2U ? "%r11w" :
                plan.width == 4U ? "%r11d" : "%r11";
            if (retry &&
                fprintf(out,"  mov%s %s(%%r10), %s\n1:\n",
                    suffix,segment,reg) < 0)
                return false;
            return fprintf(out,"  %s%s %s, %s(%%r10)\n%s",
                cmp ? "cmpxchg" : "xadd",suffix,
                cmp ? value_reg : reg,segment,
                retry ? "  jnz 1b\n" : "") >= 0;
        }

    }
    variant = instruction->source_asm_machine_form_variant;
    return variant >= 1U && variant <=
            sizeof(hardware) / sizeof(hardware[0]) &&
        fputs(hardware[variant - 1U], out) != EOF;
}

/* The only x86 serializer for this physical integer-MIR subset. Every word
 * printed below derives from a recognized opcode plus independently verified
 * operands. There is no stored GNU syntax, FILE* trampoline or Core pointer. */
static const char *const x86_gp_names[4][16] = {
    {"%al","%cl","%dl","%bl","%spl","%bpl","%sil","%dil",
     "%r8b","%r9b","%r10b","%r11b","%r12b","%r13b","%r14b","%r15b"},
    {"%ax","%cx","%dx","%bx","%sp","%bp","%si","%di",
     "%r8w","%r9w","%r10w","%r11w","%r12w","%r13w","%r14w","%r15w"},
    {"%eax","%ecx","%edx","%ebx","%esp","%ebp","%esi","%edi",
     "%r8d","%r9d","%r10d","%r11d","%r12d","%r13d","%r14d","%r15d"},
    {"%rax","%rcx","%rdx","%rbx","%rsp","%rbp","%rsi","%rdi",
     "%r8","%r9","%r10","%r11","%r12","%r13","%r14","%r15"}
};

static const char *x86_gp_name(const MinicMachineRegister *reg) {
    unsigned int index;
    if (reg == NULL || reg->bank != MINIC_MACHINE_REGISTER_GENERAL)
        return NULL;
    if (reg->number == MINIC_X86_REG_RIP && reg->width_bits == 64U)
        return "%rip";
    if (reg->number >= MINIC_X86_REG_RIP) return NULL;
    switch (reg->width_bits) {
    case 8U: index = 0U; break;
    case 16U: index = 1U; break;
    case 32U: index = 2U; break;
    case 64U: index = 3U; break;
    default: return NULL;
    }
    return x86_gp_names[index][reg->number];
}

static bool x86_emit_gp_operand(FILE *out, const MinicMachineOperand *operand) {
    const char *name;
    if (out == NULL || operand == NULL ||
        operand->kind != MINIC_MACHINE_OPERAND_REGISTER ||
        (name = x86_gp_name(&operand->value.physical.reg)) == NULL)
        return false;
    return fputs(name,out) != EOF;
}

static bool x86_emit_register(FILE *out, const MinicMachineOperand *operand) {
    const MinicMachineRegister *reg;
    if (out == NULL || operand == NULL ||
        operand->kind != MINIC_MACHINE_OPERAND_REGISTER) return false;
    reg = &operand->value.physical.reg;
    if (reg->bank == MINIC_MACHINE_REGISTER_GENERAL)
        return x86_emit_gp_operand(out,operand);
    if (reg->bank == MINIC_MACHINE_REGISTER_VECTOR &&
        reg->number < 16U && reg->width_bits == 128U)
        return fprintf(out,"%%xmm%u",reg->number) >= 0;
    if (reg->bank == MINIC_MACHINE_REGISTER_FLOAT &&
        reg->number < 8U && reg->width_bits == 80U)
        return fprintf(out,"%%st(%u)",reg->number) >= 0;
    return false;
}

static bool x86_emit_symbol(FILE *out, const char *name, int64_t addend,
    unsigned int relocation) {
    const char *suffix;
    if (out == NULL || name == NULL || name[0] == '\0') return false;
    switch (relocation) {
    case MINIC_X86_MACHINE_RELOC_ABSOLUTE:
    case MINIC_X86_MACHINE_RELOC_ABS32S:
    case MINIC_X86_MACHINE_RELOC_PC32: suffix=""; break;
    case MINIC_X86_MACHINE_RELOC_GOTPCREL: suffix="@GOTPCREL"; break;
    case MINIC_X86_MACHINE_RELOC_PLT32: suffix="@PLT"; break;
    default: return false;
    }
    if (fputs(name,out) == EOF || fputs(suffix,out) == EOF) return false;
    if (addend > 0) return fprintf(out,"+%" PRId64,addend) >= 0;
    if (addend < 0) return fprintf(out,"%" PRId64,addend) >= 0;
    return true;
}

static bool x86_emit_address(FILE *out, const MinicMachineAddress *address) {
    const char *base,*index = NULL;
    if (out == NULL || address == NULL ||
        (base = x86_gp_name(&address->base)) == NULL) return false;
    if (address->has_index &&
        (index = x86_gp_name(&address->index)) == NULL) return false;
    if (address->has_segment) {
        if (address->segment.number == MINIC_X86_SEG_FS) {
            if (fputs("%fs:",out) == EOF) return false;
        } else if (address->segment.number == MINIC_X86_SEG_GS) {
            if (fputs("%gs:",out) == EOF) return false;
        } else return false;
    }
    if (address->has_symbol) {
        if (!x86_emit_symbol(out,address->symbol,address->addend,
                             address->relocation)) return false;
    } else if (address->displacement != 0 &&
               fprintf(out,"%" PRId64,address->displacement) < 0) return false;
    if (fprintf(out,"(%s",base) < 0) return false;
    if (index != NULL && fprintf(out,",%s,%u",index,address->scale) < 0)
        return false;
    return fputc(')',out) != EOF;
}

static bool x86_emit_frame(FILE *out, const MinicMachineFunction *function,
    const MinicMachineOperand *operand,
    const MinicX86MachineOpcodeInfo *opcode) {
    const MinicMachineFrameObject *obj;
    const char *base;
    int64_t displacement;
    if (out == NULL || function == NULL || operand == NULL || opcode == NULL ||
        operand->kind != MINIC_MACHINE_OPERAND_FRAME ||
        operand->value.frame.index >= function->frame_object_count)
        return false;
    obj = &function->frame_objects[operand->value.frame.index];
    if (opcode->frame_base == 1U &&
        obj->base != MINIC_MACHINE_FRAME_BASE_FRAME_POINTER) return false;
    if (opcode->frame_base == 2U &&
        obj->base != MINIC_MACHINE_FRAME_BASE_STACK_POINTER) return false;
    base = obj->base == MINIC_MACHINE_FRAME_BASE_FRAME_POINTER ? "%rbp" :
           obj->base == MINIC_MACHINE_FRAME_BASE_STACK_POINTER ? "%rsp" : NULL;
    if (base == NULL) return false;
    if ((operand->value.frame.displacement > 0 &&
         obj->fixed_offset > INT64_MAX - operand->value.frame.displacement) ||
        (operand->value.frame.displacement < 0 &&
         obj->fixed_offset < INT64_MIN - operand->value.frame.displacement))
        return false;
    displacement = obj->fixed_offset + operand->value.frame.displacement;
    if (displacement < INT32_MIN || displacement > INT32_MAX) return false;
    return fprintf(out,"%" PRId64 "(%s)",displacement,base) >= 0;
}

static bool x86_emit_typed_operand(FILE *out,
    const MinicMachineFunction *function, const MinicMachineOperand *operand,
    const MinicX86MachineOpcodeInfo *opcode) {
    if (operand == NULL) return false;
    switch (operand->kind) {
    case MINIC_MACHINE_OPERAND_REGISTER:
        return x86_emit_register(out,operand);
    case MINIC_MACHINE_OPERAND_INTEGER:
        return fprintf(out,"$%" PRId64,operand->value.integer) >= 0;
    case MINIC_MACHINE_OPERAND_ADDRESS:
        return x86_emit_address(out,&operand->value.address);
    case MINIC_MACHINE_OPERAND_FRAME:
        return x86_emit_frame(out,function,operand,opcode);
    case MINIC_MACHINE_OPERAND_SYMBOL:
        return x86_emit_symbol(out,operand->value.symbol.name,
            operand->value.symbol.addend,operand->value.symbol.relocation);
    default: return false;
    }
}

static bool x86_emit_selected(FILE *out,
    const MinicMachineFunction *function, const MinicMachineInstruction *ins,
    const MinicMachineGNUAsmLabels *labels, void *context) {
    const MinicX86MachineOpcodeInfo *opcode;
    (void)context;
    if (out == NULL || function == NULL || ins == NULL || labels == NULL ||
        (opcode=minic_x86_64_machine_opcode_info(ins->opcode)) == NULL ||
        !minic_x86_64_machine_validate_selected(function,ins,NULL))
        return false;
    if (ins->opcode == MINIC_X86_MI_SHLD64_CL_RR ||
        ins->opcode == MINIC_X86_MI_SHRD64_CL_RR) {
        if (fprintf(out,"  %s %%cl, ",opcode->mnemonic) < 0 ||
            !x86_emit_gp_operand(out,&ins->operands[0]) ||
            fputs(", ",out) == EOF ||
            !x86_emit_gp_operand(out,&ins->operands[1])) return false;
        return fputc('\n',out) != EOF;
    }
    if (fprintf(out,"  %s",opcode->mnemonic) < 0) return false;
    switch (opcode->format) {
    case MINIC_X86_MACHINE_FORMAT_NONE:
        /* Fixed-register x87 stack encodings are target-known machine
         * opcodes, not source ASM templates or arbitrary GNU fragments. */
        switch (ins->opcode) {
        case MINIC_X86_MI_FXCH_ST1:
        case MINIC_X86_MI_FSTP_ST0:
            if (fputs(ins->opcode == MINIC_X86_MI_FXCH_ST1
                      ? " %st(1)" : " %st(0)",out) == EOF) return false;
            break;
        case MINIC_X86_MI_FUCOMIP_ST1:
            if (fputs(" %st(1), %st",out) == EOF) return false;
            break;
        case MINIC_X86_MI_FADDP_ST1:
        case MINIC_X86_MI_FSUBRP_ST1:
        case MINIC_X86_MI_FMULP_ST1:
        case MINIC_X86_MI_FDIVRP_ST1:
            if (fputs(" %st, %st(1)",out) == EOF) return false;
            break;
        default: break;
        }
        break;
    case MINIC_X86_MACHINE_FORMAT_RR:
    case MINIC_X86_MACHINE_FORMAT_IR:
    case MINIC_X86_MACHINE_FORMAT_AR:
    case MINIC_X86_MACHINE_FORMAT_RA:
    case MINIC_X86_MACHINE_FORMAT_FR:
    case MINIC_X86_MACHINE_FORMAT_RF:
    case MINIC_X86_MACHINE_FORMAT_SR:
        if ((ins->opcode == MINIC_X86_MI_MOVABS64_SYMBOL_R ||
             ins->opcode == MINIC_X86_MI_MOV64_IMM32S_SYMBOL_R) &&
            fputs(" $",out) == EOF) return false;
        if (ins->opcode != MINIC_X86_MI_MOVABS64_SYMBOL_R &&
            ins->opcode != MINIC_X86_MI_MOV64_IMM32S_SYMBOL_R &&
            fputc(' ',out) == EOF) return false;
        if (!x86_emit_typed_operand(out,function,&ins->operands[0],opcode))
            return false;
        if (opcode->format == MINIC_X86_MACHINE_FORMAT_SR &&
            ins->opcode != MINIC_X86_MI_MOVABS64_SYMBOL_R &&
            ins->opcode != MINIC_X86_MI_MOV64_IMM32S_SYMBOL_R &&
            fputs("(%rip)",out) == EOF) return false;
        if (fputs(", ",out) == EOF ||
            !x86_emit_typed_operand(out,function,&ins->operands[1],opcode))
            return false;
        break;
    case MINIC_X86_MACHINE_FORMAT_R:
        if (ins->opcode == MINIC_X86_MI_JMP_R ||
            ins->opcode == MINIC_X86_MI_CALL_SYSV_R ||
            ins->opcode == MINIC_X86_MI_CALL_MS_R) {
            if (fputs(" *",out) == EOF) return false;
        } else if (fputc(' ',out) == EOF) return false;
        if (ins->opcode == MINIC_X86_MI_SHL64_CL_R ||
            ins->opcode == MINIC_X86_MI_SHR64_CL_R ||
            ins->opcode == MINIC_X86_MI_SAR64_CL_R ||
            ins->opcode == MINIC_X86_MI_SHL32_CL_R ||
            ins->opcode == MINIC_X86_MI_SHR32_CL_R ||
            ins->opcode == MINIC_X86_MI_SAR32_CL_R) {
            if (fputs("%cl, ",out) == EOF) return false;
        }
        if (!x86_emit_gp_operand(out,&ins->operands[0])) return false;
        break;
    case MINIC_X86_MACHINE_FORMAT_B:
        if (ins->operands[0].value.block_id >= labels->count ||
            labels->names == NULL ||
            labels->names[ins->operands[0].value.block_id] == NULL ||
            fprintf(out," %s",
                labels->names[ins->operands[0].value.block_id]) < 0)
            return false;
        break;
    case MINIC_X86_MACHINE_FORMAT_S:
        if (fputc(' ',out) == EOF ||
            !x86_emit_typed_operand(out,function,&ins->operands[0],opcode))
            return false;
        break;
    case MINIC_X86_MACHINE_FORMAT_A:
    case MINIC_X86_MACHINE_FORMAT_F:
        if (fputc(' ',out) == EOF ||
            !x86_emit_typed_operand(out,function,&ins->operands[0],opcode))
            return false;
        break;
    default: return false;
    }
    return fputc('\n',out) != EOF;
}

bool minic_x86_64_machine_gnu_asm_policy(const MinicTargetInfo *target,
    MinicMachineGNUAsmPolicy *out) {
    if (target == NULL || target->architecture != MINIC_TARGET_ARCH_X86_64 ||
        out == NULL) return false;
    out->verify.lookup_opcode=minic_x86_64_machine_opcode_lookup;
    out->verify.validate_selected_instruction=
        minic_x86_64_machine_validate_selected;
    out->verify.validate_generated_features=
        minic_x86_64_machine_validate_features;
    out->verify.validate_function_abi=minic_x86_64_machine_validate_abi;
    out->verify.validate_frame_object=
        minic_x86_64_machine_validate_frame_object;
    out->verify.validate_register=minic_x86_64_machine_validate_register;
    out->verify.validate_address=minic_x86_64_machine_validate_address;
    out->verify.validate_relocation=minic_x86_64_machine_validate_relocation;
    out->verify.validate_source_asm=minic_x86_64_machine_validate_source_asm;
    out->verify.context=NULL;
    out->emit_selected=x86_emit_selected;
    out->emit_source_asm=x86_emit_source_asm;
    out->context=NULL;
    return true;
}
