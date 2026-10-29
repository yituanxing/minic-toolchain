#include "target/aarch64/asm_contract.h"
#include "target/common/lp64_frame.h"
#include "target/common/direct_linux_bug_asm.h"
#include "target/data_layout.h"
#include "frontend/const_eval.h"
#include <string.h>

/* Original Linux 6.6 arch/arm64/include/asm/jump_label.h.  Both jump
 * sites own one 16-byte __jump_table record: site and C label PREL32,
 * static-key/branch-flags PREL64.  Only the original two templates qualify;
 * arbitrary source text never reaches the target assembler through this gate. */
static unsigned int aarch64_linux_jump_label_form(
    const char *source, size_t length) {
    static const char shared[] =
        ".pushsection__jump_table,\"aw\".align3"
        ".long1b-.,%l[l_yes]-..quad%c0-..popsection";
    static const char *const prefixes[] = { "1:nop", "1:b%l[l_yes]" };
    char compact[256];
    size_t i, n = 0U;
    if (source == NULL || !length || length >= sizeof(compact)) return 0U;
    for (i = 0U; i < length; ++i) {
        char c = source[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return 0U;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(prefixes) / sizeof(prefixes[0]); ++i) {
        size_t a = strlen(prefixes[i]), b = sizeof(shared) - 1U;
        if (n == a + b && memcmp(compact, prefixes[i], a) == 0 &&
            memcmp(compact + a, shared, b) == 0)
            return (unsigned int)i + 1U;
    }
    return 0U;
}

static bool aarch64_linux_jump_key_relocation(const char *key) {
    size_t i, length;
    if (key == NULL) return false;
    length = strlen(key);
    if (length == 0U || length > 128U ||
        !((key[0] >= 'a' && key[0] <= 'z') ||
          (key[0] >= 'A' && key[0] <= 'Z') ||
          key[0] == '_' || key[0] == '.')) return false;
    for (i = 1U; i < length; ++i) {
        char c = key[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '$')
            continue;
        if ((c == '+' || c == '-') && i + 1U < length) {
            for (++i; i < length; ++i)
                if (key[i] < '0' || key[i] > '9') return false;
            return true;
        }
        return false;
    }
    return true;
}

bool minic_aarch64_qualify_inline_asm_goto(
    const MinicC0Program *program, const MinicInlineAsm *source) {
    const MinicInlineAsmOperand *key;
    const MinicInlineAsmLabel *label;
    const MinicExpression *expression;
    if (program == NULL || source == NULL || !source->is_goto ||
        source->is_volatile || source->output_count != 0U ||
        source->input_count != 1U || source->inputs == NULL ||
        source->label_count != 1U || source->labels == NULL ||
        source->clobber_count != 0U || source->register_clobber_count != 0U ||
        source->has_memory_clobber ||
        aarch64_linux_jump_label_form(source->template_text,
                                      source->template_length) == 0U) return false;
    key = &source->inputs[0];
    label = &source->labels[0];
    expression = minic_c0_program_expression(program, key->expression);
    return key->access == MINIC_INLINE_ASM_OPERAND_READ_ONLY &&
           key->constraint_text != NULL && key->constraint_length == 1U &&
           key->constraint_text[0] == 'i' && expression != NULL &&
           minic_type_is_pointer(expression->type) &&
           label->name != NULL && label->name_length == 5U &&
           memcmp(label->name, "l_yes", 5U) == 0 &&
           label->target_statement != MINIC_STATEMENT_INVALID;
}

bool minic_aarch64_linux_jump_label_asm_goto_core(
    const MinicCoreInlineAsm *source) {
    return source != NULL && source->is_goto && source->is_volatile &&
           source->goto_target != MINIC_CORE_BLOCK_INVALID &&
           source->goto_second_target == MINIC_CORE_BLOCK_INVALID &&
           !source->has_memory_clobber &&
           source->register_clobber_count == 0U &&
           source->source_inline_asm_id == SIZE_MAX &&
           source->resolved_immediate_count == 1U &&
           aarch64_linux_jump_key_relocation(source->resolved_immediates[0]) &&
           aarch64_linux_jump_label_form(source->template_text,
                                         source->template_length) != 0U;
}

bool minic_aarch64_emit_linux_jump_label_asm_goto(
    FILE *out, const MinicCoreInlineAsm *source,
    size_t function_id, size_t function_blocks) {
    unsigned int form = source != NULL
        ? aarch64_linux_jump_label_form(source->template_text,
                                        source->template_length) : 0U;
    if (out == NULL || form == 0U ||
        !minic_aarch64_linux_jump_label_asm_goto_core(source) ||
        source->goto_target >= function_blocks) return false;
    if (form == 1U) {
        if (fputs("1: nop\n", out) == EOF) return false;
    } else if (fprintf(out, "1: b .Lmini_arm_f%zu_b%u\n",
                       function_id, (unsigned int)source->goto_target) < 0)
        return false;
    return fprintf(out,
        "  .pushsection __jump_table,\"aw\"\n"
        "  .balign 8\n"
        "  .long 1b - .\n"
        "  .long .Lmini_arm_f%zu_b%u - .\n"
        "  .quad %s - .\n"
        "  .popsection\n",
        function_id, (unsigned int)source->goto_target,
        source->resolved_immediates[0]) >= 0;
}

/* Linux 6.6 arch/arm64/include/asm/alternative-macros.h.
 * The original assembler-generated section and paired .orgs are needed for
 * boot-time feature patching. This is an EXACT, target-owned template gate;
 * the only variable part is the cpucap number in the .hword field. The
 * branch must remain an asm-goto edge, not a generic opaque barrier. */
static const char aarch64_linux_cap_goto_prefix[] =
    ".if 1 == 1\n"
    "661:\n\t"
    "nop\n"
    "662:\n"
    ".pushsection .altinstructions,\"a\"\n"
    " .word 661b - .\n"
    " .word 663f - .\n"
    " .hword ";
static const char aarch64_linux_cap_goto_suffix[] =
    "\n"
    " .byte 662b-661b\n"
    " .byte 664f-663f\n"
    ".popsection\n"
    ".subsection 1\n"
    "663:\n\t"
    "b\t%l[l_yes]\n"
    "664:\n\t"
    ".org\t. - (664b-663b) + (662b-661b)\n\t"
    ".org\t. - (662b-661b) + (664b-663b)\n\t"
    ".previous\n"
    ".endif\n";
/* alternative_has_cap_likely() uses ALTERNATIVE_CB. Its existing branch
 * jumps to l_no until alt_cb_patch_nops patches it away. The second PREL32
 * field is the callback address, not a replacement instruction offset. */
static const char aarch64_linux_cap_cb_prefix[] =
    ".if 1 == 1\n"
    "661:\n\t"
    "b\t%l[l_no]\n"
    "662:\n"
    ".pushsection .altinstructions,\"a\"\n"
    " .word 661b - .\n"
    " .word alt_cb_patch_nops- .\n"
    " .hword (1 << 15) | (";
static const char aarch64_linux_cap_cb_suffix[] =
    ")\n"
    " .byte 662b-661b\n"
    " .byte 664f-663f\n"
    ".popsection\n"
    "663:\n\t"
    "664:\n\t"
    ".endif\n";

static bool aarch64_linux_cap_goto_text(const char *s, size_t len,
                                        bool source_form, bool callback_form) {
    const char *const expected_prefix = callback_form
        ? aarch64_linux_cap_cb_prefix : aarch64_linux_cap_goto_prefix;
    const char *const expected_suffix = callback_form
        ? aarch64_linux_cap_cb_suffix : aarch64_linux_cap_goto_suffix;
    const size_t prefix = callback_form
        ? sizeof(aarch64_linux_cap_cb_prefix) - 1U
        : sizeof(aarch64_linux_cap_goto_prefix) - 1U;
    const size_t suffix = callback_form
        ? sizeof(aarch64_linux_cap_cb_suffix) - 1U
        : sizeof(aarch64_linux_cap_goto_suffix) - 1U;
    size_t middle, i;
    unsigned int cap = 0U;
    if (s == NULL || len <= prefix + suffix ||
        memcmp(s, expected_prefix, prefix) != 0 ||
        memcmp(s + len - suffix, expected_suffix, suffix) != 0)
        return false;
    middle = len - prefix - suffix;
    if (source_form)
        return middle == sizeof("%[cpucap]") - 1U &&
               memcmp(s + prefix, "%[cpucap]", middle) == 0;
    if (middle == 2U && memcmp(s + prefix, "%0", middle) == 0)
        return true; /* Core deferred-immediate contract, not a guessed cap. */
    if (middle == 0U || middle > 5U) return false;
    for (i = 0U; i < middle; ++i) {
        if (s[prefix + i] < '0' || s[prefix + i] > '9') return false;
        cap = cap * 10U + (unsigned int)(s[prefix + i] - '0');
    }
    return cap < (1U << 15);
}

bool minic_aarch64_linux_cap_asm_goto_source(const MinicInlineAsm *s) {
    const bool callback_form = s != NULL &&
        aarch64_linux_cap_goto_text(s->template_text, s->template_length,
                                    true, true);
    return s != NULL && s->is_goto && !s->is_volatile &&
           s->output_count == 0U && s->input_count == 1U &&
           s->inputs != NULL && s->label_count == 1U && s->labels != NULL &&
           s->clobber_count == 0U && !s->has_memory_clobber &&
           s->register_clobber_count == 0U &&
           s->inputs[0].access == MINIC_INLINE_ASM_OPERAND_READ_ONLY &&
           s->inputs[0].constraint_text != NULL &&
           s->inputs[0].constraint_length == 1U &&
           s->inputs[0].constraint_text[0] == 'i' &&
           s->inputs[0].name != NULL &&
           s->inputs[0].name_length == sizeof("cpucap") - 1U &&
           memcmp(s->inputs[0].name, "cpucap", sizeof("cpucap") - 1U) == 0 &&
           s->labels[0].name != NULL &&
           s->labels[0].name_length ==
               (callback_form ? sizeof("l_no") - 1U : sizeof("l_yes") - 1U) &&
           memcmp(s->labels[0].name, callback_form ? "l_no" : "l_yes",
                  s->labels[0].name_length) == 0 &&
           (callback_form || aarch64_linux_cap_goto_text(
               s->template_text, s->template_length, true, false));
}

bool minic_aarch64_linux_cap_asm_goto_core(const MinicCoreInlineAsm *a) {
    return a != NULL && a->is_goto && a->is_volatile &&
           a->goto_target != MINIC_CORE_BLOCK_INVALID &&
           (aarch64_linux_cap_goto_text(a->template_text,
                                        a->template_length, false, false) ||
            aarch64_linux_cap_goto_text(a->template_text,
                                        a->template_length, false, true));
}

bool minic_aarch64_emit_linux_cap_asm_goto(
    FILE *out, const MinicCoreInlineAsm *a, size_t function_id,
    size_t target_block) {
    static const char unlikely_goto_token[] = "%l[l_yes]";
    static const char likely_goto_token[] = "%l[l_no]";
    const char *const goto_token = a != NULL &&
        aarch64_linux_cap_goto_text(a->template_text, a->template_length,
                                    false, true)
        ? likely_goto_token : unlikely_goto_token;
    const size_t goto_token_length = a != NULL &&
        aarch64_linux_cap_goto_text(a->template_text, a->template_length,
                                    false, true)
        ? sizeof(likely_goto_token) - 1U
        : sizeof(unlikely_goto_token) - 1U;
    static const char imm_token[] = "%0";
    const bool deferred = a != NULL && a->source_inline_asm_id != SIZE_MAX;
    size_t i;
    if (out == NULL || !minic_aarch64_linux_cap_asm_goto_core(a) ||
        (deferred ? strstr(a->template_text, imm_token) == NULL
                  : strstr(a->template_text, imm_token) != NULL))
        return false;

    /* A Core control-flow label is rendered as the real target-owned block
     * label, so Linux's patched branch lands on the correct C label. */
    if (deferred &&
        fprintf(out, "  // MINIC_DEFERRED_ASM_IMMEDIATE requires inline specialization\n"
                     "  .extern __minic_deferred_asm_immediate_%zu_0\n",
                a->source_inline_asm_id) < 0)
        return false;
    for (i = 0U; i < a->template_length;) {
        if (i + goto_token_length <= a->template_length &&
            memcmp(a->template_text + i, goto_token,
                   goto_token_length) == 0) {
            if (fprintf(out, ".Lmini_arm_f%zu_b%zu",
                        function_id, target_block) < 0)
                return false;
            i += goto_token_length;
        } else if (deferred &&
                   i + sizeof(imm_token) - 1U <= a->template_length &&
                   memcmp(a->template_text + i, imm_token,
                          sizeof(imm_token) - 1U) == 0) {
            if (fprintf(out, "__minic_deferred_asm_immediate_%zu_0",
                        a->source_inline_asm_id) < 0)
                return false;
            i += sizeof(imm_token) - 1U;
        } else if (fputc((unsigned char)a->template_text[i++], out) == EOF) {
            return false;
        }
    }
    return fputc('\n', out) != EOF;
}
/* Transport contract for every structured GNU asm Core SSA operand.
 * Every register input and lvalue address lives in an eight-byte Core
 * frame slot.  The shared LP64 plan already permits 1 MiB frames and
 * our target-owned large-SP legalizer expands unencodable LDR Xt slots.
 * The old 4095/8 bound confused the raw byte imm12 with scaled AArch64
 * unsigned-imm12 and rejected original Linux on slot #512 and above.
 * Frame validity, not encoded ISA immediates, is the sole Core gate. */
static bool aarch64_linux_structured_spill_slot_valid(size_t slot) {
    return slot <= MINIC_LARGE_FRAME_MAX / 8U;
}

/* Real Linux GNU AArch64 has two distinct zero-operand effects:
 * __bug_table ELF metadata with BRK, and exactly enumerated one-instruction
 * privileged/ordering ISA. Source/preflight and machine emission use the
 * SAME qualifier. Never accept arbitrary target text or replace with a
 * compiler barrier. */
/* Linux PSTATE.{SSBS,DIT,PAN} uses an instruction encoding in old GNU
 * assemblers that do not know the register alias. The admissible producer
 * set is the frozen kernel's exact .inst(0xd500401f | ...) with two fully
 * specified selector fields and a boolean immediate. No arbitrary .inst
 * expression, PSTATE field, or extra instruction is admitted. */
unsigned int minic_aarch64_linux_pstate_imm_opcode(
    const char *text, size_t length) {
    static const struct { unsigned int op1, op2; } forms[] = {
        {0U, 4U}, {3U, 1U}, {3U, 2U}, {3U, 4U}
    };
    char compact[160], expected[160];
    size_t n = 0U, i, bit;
    if (!text || !length || length >= sizeof(compact)) return 0U;
    for (i = 0U; i < length; ++i) {
        const char ch = text[i];
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') continue;
        if (ch == '\0' || n + 1U >= sizeof(compact)) return 0U;
        compact[n++] = ch;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(forms) / sizeof(forms[0]); ++i)
        for (bit = 0U; bit < 2U; ++bit) {
            const int written = snprintf(expected, sizeof(expected),
                ".inst(0xd500401f|((%u)<<16|(%u)<<5)|((!!(%u))<<8))",
                forms[i].op1, forms[i].op2, (unsigned int)bit);
            if (written > 0 && (size_t)written < sizeof(expected) &&
                strcmp(compact, expected) == 0)
                return 0xd500401fU | (forms[i].op1 << 16U) |
                       (forms[i].op2 << 5U) | ((unsigned int)bit << 8U);
        }
    return 0U;
}

/* DAIFSet/DAIFClr execute a four-bit PSTATE mask update, not a normal
 * memory barrier. Linux uses #3 in GIC fast paths and #15 during full
 * exception masking. Admit the exact Linux annotation only when it occurs
 * after the single supported #15 instruction; another `//` or a semicolon
 * cannot hide an unrelated privileged instruction. */
unsigned int minic_aarch64_linux_daif_immediate_form(
    const char *text, size_t length) {
    static const char *const forms[] = {
        "msrdaifset,#3", "msrdaifclr,#3",
        "msrdaifset,#0xf", "msrdaifclr,#0xf",
        "msrdaifset,#0xf//local_daif_mask"
    };
    char compact[96];
    size_t n = 0U, i;
    if (!text || length == 0U || length >= sizeof(compact)) return 0U;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return 0U;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(forms) / sizeof(forms[0]); ++i)
        if (strcmp(compact, forms[i]) == 0)
            return i == 4U ? 3U : (unsigned int)(i + 1U);
    return 0U;
}

unsigned int minic_aarch64_linux_rept_nop_count(
    const char *text, size_t length) {
    char compact[32];
    size_t i, n = 0U;
    if (!text || !length || length >= sizeof(compact)) return 0U;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
        if (c == '\0' || c == ';' || n + 1U >= sizeof(compact)) return 0U;
        compact[n++] = c;
    }
    compact[n] = '\0';
    if (strcmp(compact, ".rept4nop.endr") == 0) return 4U;
    if (strcmp(compact, ".rept8nop.endr") == 0) return 8U;
    return 0U;
}

bool minic_aarch64_qualified_opaque_asm(const MinicCoreInlineAsm *a) {
    static const char *const allowed[] = {
        "msrdaifset,#3", "msrdaifclr,#3", "msrdaifclr,#8",
        "yield", "hint#6", "hint#17", "hint#18", "hint#20",
        "sev", "sevl", "wfe", "wfi", "nop",
        "dsbsy", "dsbst", "dsbish", "dsbishst", "dsbld",
        "dmbish", "dmbishld", "dmbishst", "dmbsy",
        "icialluis", "iciallu",
        "dmbosh", "dmboshld", "dmboshst", "isb"
    };
    char compact[64];
    size_t i, n = 0U;
    if (minic_aarch64_linux_jump_label_asm_goto_core(a)) return true;
    if (minic_aarch64_linux_cap_asm_goto_core(a)) return true;
    if (minic_aarch64_linux_barrier_alternative_opaque(a)) return true;
    if (a != NULL && a->is_volatile && !a->is_goto &&
        a->register_clobber_count == 0U &&
        minic_aarch64_linux_daif_immediate_form(
            a->template_text, a->template_length) != 0U) return true;
    if (a != NULL && a->is_volatile && !a->is_goto &&
        a->register_clobber_count == 0U &&
        minic_aarch64_linux_pstate_imm_opcode(
            a->template_text, a->template_length) != 0U) return true;
    if (minic_multi_direct_linux_bug_asm(
            a, "14470", "14471", "14472", "brk0x800", true))
        return true;
    if (a != NULL && a->is_volatile && !a->is_goto &&
        a->register_clobber_count == 0U && !a->has_memory_clobber &&
        minic_aarch64_linux_rept_nop_count(
            a->template_text, a->template_length) != 0U)
        return true;
    if (a == NULL || a->template_text == NULL || !a->is_volatile ||
        a->template_length == 0U || a->template_length >= sizeof(compact) ||
        a->is_goto || a->register_clobber_count != 0U)
        return false;
    for (i = 0U; i < a->template_length; ++i) {
        const char c = a->template_text[i];
        if (c == ' ' || c == '\t') continue;
        if (c == '\n' || c == '\r' || c == ';' || c == '\0')
            return false;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(allowed) / sizeof(allowed[0]); ++i)
        if (strcmp(compact, allowed[i]) == 0) return true;
    return false;
}
/* Linux 6.6 array_index_mask_nospec: compare two unsigned LP64 words,
 * consume NZCV immediately with SBC, store the all-zero/all-one mask. */
static bool mask_template(const char *text, size_t len) {
    static const char expected[] = "cmp%1,%2sbc%0,xzr,xzr";
    char norm[96];
    size_t i, n = 0U;
    if (!text || len >= sizeof(norm)) return false;
    for (i = 0U; i < len; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
        if (n + 1U >= sizeof(norm)) return false;
        norm[n++] = c;
    }
    norm[n] = '\0';
    return strcmp(norm, expected) == 0;
}
/* The genuine 6.6.143 sysreg.h read_sysreg macro uses u64 (usually
 * unsigned long long), while local IRQ flags and pointer-sized values
 * are unsigned long. Both are exactly 64-bit AAPCS64 GP values. Share
 * one *target-owned* type gate for source and machine qualification. */
static bool aarch64_linux_sysreg_u64_type(MinicType type) {
    return minic_type_equal(type, minic_type_unsigned_long()) ||
           minic_type_equal(type, minic_type_unsigned_long_long());
}

/* Exact original Linux 6.6.143 MRS register read family. These privileged
 * system registers can be assembled for EL1; do not user-mode QEMU execute
 * them and never turn this into arbitrary template passthrough. */
static const char *aarch64_linux_mrs_register(const char *text, size_t len) {
    char normalized[96];
    size_t i, used = 0U;
    static const struct { const char *text; const char *reg; } allowed[] = {
        {"mrs%0,sp_el0", "sp_el0"},
        {"mrs%0,tpidr_el1", "tpidr_el1"},
        {"mrs%0,tpidr_el2", "tpidr_el2"},
        {"mrs%0,clidr_el1", "clidr_el1"},
        {"mrs%0,cntkctl_el1", "cntkctl_el1"},
        {"mrs%0,cntp_ctl_el0", "cntp_ctl_el0"},
        {"mrs%0,cntv_ctl_el0", "cntv_ctl_el0"},
        {"mrs%0,cntfrq_el0", "cntfrq_el0"},
        {"mrs%0,cntvct_el0", "cntvct_el0"},
        {"mrs%0,CurrentEL", "CurrentEL"},
        {"mrs%0,tcr_el1", "tcr_el1"},
        {"mrs%0,esr_el1", "esr_el1"},
        {"mrs%0,sctlr_el1", "sctlr_el1"},
        {"mrs%0,far_el1", "far_el1"},
        {"mrs%0,par_el1", "par_el1"},
        {"mrs%0,id_aa64mmfr1_el1", "id_aa64mmfr1_el1"},
        /* hyp/debug-sr.h derives the architectural breakpoint/watchpoint
         * bank sizes from ID_AA64DFR0_EL1, then saves MDCCINT_EL1. The
         * hyp sysreg save/restore path also reads these fixed registers. */
        {"mrs%0,id_aa64dfr0_el1", "id_aa64dfr0_el1"},
        {"mrs%0,mdccint_el1", "mdccint_el1"},
        {"mrs%0,tpidrro_el0", "tpidrro_el0"},
        {"mrs%0,sp_el1", "sp_el1"},
        {"mrs%0,vmpidr_el2", "vmpidr_el2"},
        {"mrs%0,hcr_el2", "hcr_el2"},
        /* KVM hyp/switch.h reads/restores these exact EL2 trap/debug
         * registers using the ordinary read_sysreg() GNU "=r" API. */
        {"mrs%0,fpexc32_el2", "fpexc32_el2"},
        {"mrs%0,hstr_el2", "hstr_el2"},
        {"mrs%0,mdcr_el2", "mdcr_el2"},
        {"mrs%0,tpidr_el0", "tpidr_el0"},
        {"mrs%0,mdscr_el1", "mdscr_el1"},
        {"mrs%0,ttbr1_el1", "ttbr1_el1"},
        {"mrs%0,daif", "daif"}
    };
    if (text == NULL || len >= sizeof(normalized))
        return NULL;
    for (i = 0U; i < len; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            continue;
        if (used + 1U >= sizeof(normalized))
            return NULL;
        normalized[used++] = c;
    }
    normalized[used] = '\0';
    for (i = 0U; i < sizeof(allowed) / sizeof(allowed[0]); ++i)
        if (strcmp(allowed[i].text, normalized) == 0)
            return allowed[i].reg;
    return NULL;
}
/* The architectural DMB/DSB/ISB operations are hardware fences; they
 * cannot be silently downgraded into a compiler-only memory clobber.
 * The source and machine both use this target-owned exact template table. */
static const char *aarch64_barrier_template(const char *text, size_t len) {
    static const char *const allowed[] = {
        "dmb ish", "dmb ishld", "dmb ishst", "dmb sy",
        "dmb osh", "dmb oshld", "dmb oshst",
        "dsb sy", "dsb ish", "dsb ishst", "dsb ld", "dsb st",
        "dsb nsh", "dsb nshst", "isb",
        "msr daifset, #3", "msr daifclr, #3", "msr daifclr, #8",
        /* barrier.h psb_csync()/tsb_csync(): architected trace drain hints. */
        "yield", "hint #6", "hint #17", "hint #18", "hint #20",
        "sev", "sevl", "wfe", "wfi", "nop",
        "ic ialluis", "ic iallu"
    };
    char normalized[64];
    size_t i, n = 0U;
    if (text == NULL || len == 0U || len >= sizeof(normalized))
        return NULL;
    /* Original Linux cache.h writes the real IC operand with a tab. The
     * opcode is unique and privilege-sensitive; keep only this exact tabbed
     * spelling, without admitting tabs/newlines in other hardware ASM. */
    if (len == sizeof("ic\tialluis") - 1U &&
        memcmp(text, "ic\tialluis", len) == 0)
        return "ic ialluis";
    if (len == sizeof("ic\tiallu") - 1U &&
        memcmp(text, "ic\tiallu", len) == 0)
        return "ic iallu";
    for (i = 0U; i < len; ++i) {
        const char c = text[i];
        if (c == '\t' || c == '\r' || c == '\n')
            return NULL; /* no hidden second hardware instruction */
        if (n + 1U >= sizeof(normalized))
            return NULL;
        normalized[n++] = c;
    }
    while (n != 0U && normalized[n - 1U] == ' ') --n;
    normalized[n] = '\0';
    for (i = 0U; i < sizeof(allowed) / sizeof(allowed[0]); ++i)
        if (strcmp(normalized, allowed[i]) == 0)
            return allowed[i];
    return NULL;
}
/* Linux BRK BUG trap: the AArch64 GNU I constraint is an add/sub
 * immediate class, but BRK itself takes an imm16. Admit the exact
 * brk %0 template with a literal in the intersection (0..4095).
 * Never treat a GNU constraint as permission to pass through arbitrary ASM. */
static bool aarch64_linux_brk_template(const char *text, size_t length) {
    static const char exact[] = "brk%0";
    char compact[32];
    size_t i, n = 0U;
    if (text == NULL || length == 0U || length >= sizeof(compact)) return false;
    for (i = 0U; i < length; ++i) {
        char c = text[i];
        if (c == '\r' || c == '\n') return false;
        if (c == ' ' || c == '\t') continue;
        if (n + 1U >= sizeof(compact)) return false;
        compact[n++] = c;
    }
    compact[n] = '\0';
    return strcmp(compact, exact) == 0;
}
static bool aarch64_linux_brk_value(int64_t value) {
    return value >= 0 && value <= 4095;
}
/* The original Linux 6.6.143 arm64 io.h __raw_read{b,w,l,q}
 * carries a Linux feature-91 .altinstructions patch table. Neither a
 * plain LDR nor a compiler barrier can replace the complete contract.
 * Exact source admission and machine emission share this target catalog.
 * 91 is ARM64_WORKAROUND_DEVICE_LOAD_ACQUIRE from v6.6.143 cpucaps. */
typedef struct {
    const char *old_source, *new_source;
    const char *old_asm, *new_asm, *store_asm;
    size_t width;
} Aarch64RawIoRead;
static const Aarch64RawIoRead aarch64_raw_io_reads[] = {
    {"ldrb%w0,[%1]", "ldarb%w0,[%1]", "ldrb w9, [x10]",
     "ldarb w9, [x10]", "strb w9, [x11]", 1U},
    {"ldrh%w0,[%1]", "ldarh%w0,[%1]", "ldrh w9, [x10]",
     "ldarh w9, [x10]", "strh w9, [x11]", 2U},
    {"ldr%w0,[%1]", "ldar%w0,[%1]", "ldr w9, [x10]",
     "ldar w9, [x10]", "str w9, [x11]", 4U},
    {"ldr%0,[%1]", "ldar%0,[%1]", "ldr x9, [x10]",
     "ldar x9, [x10]", "str x9, [x11]", 8U}
};
static const Aarch64RawIoRead *aarch64_raw_io_read(
    const char *text, size_t length) {
    static const char middle[] =
        "662:.pushsection.altinstructions,\"a\""
        ".word661b-..word663f-..hword91"
        ".byte662b-661b.byte664f-663f"
        ".popsection.subsection1663:";
    static const char tail[] =
        "664:.org.-(664b-663b)+(662b-661b)"
        ".org.-(662b-661b)+(664b-663b).previous.endif";
    char compact[512], expected[512];
    size_t i, n = 0U;
    if (!text || !length || length >= sizeof(compact)) return NULL;
    for (i = 0U; i < length; ++i) {
        const char ch = text[i];
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n')
            continue;
        if (n + 1U >= sizeof(compact)) return NULL;
        compact[n++] = ch;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(aarch64_raw_io_reads) /
                          sizeof(aarch64_raw_io_reads[0]); ++i) {
        const Aarch64RawIoRead *d = &aarch64_raw_io_reads[i];
        const int written = snprintf(expected, sizeof(expected),
            ".if1==1661:%s%s%s%s", d->old_source, middle,
            d->new_source, tail);
        if (written > 0 && (size_t)written < sizeof(expected) &&
            strcmp(compact, expected) == 0) return d;
    }
    return NULL;
}

/* Both real 6.6.143 arm64 prefetch() and prefetchw() in
 * processor.h use GNU p and %a0. A trailing newline is source whitespace;
 * the only safe hardware opcodes are exact PRFM PLDL1KEEP/PSTL1KEEP. */
static const char *aarch64_linux_prefetch_template(const char *text, size_t length) {
    char compact[64];
    size_t i, n = 0U;
    if (!text || length == 0U || length >= sizeof(compact)) return NULL;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            continue;
        if (c == ';' || n + 1U >= sizeof(compact))
            return NULL;
        compact[n++] = c;
    }
    compact[n] = '\0';
    if (strcmp(compact, "prfmpldl1keep,%a0") == 0)
        return "pldl1keep";
    if (strcmp(compact, "prfmpstl1keep,%a0") == 0)
        return "pstl1keep";
    return NULL;
}

/* ORIGINAL Linux 6.6.143 arch/arm64/include/asm/io.h, all four
 * __raw_write{b,w,l,q} operations:
 * asm volatile("str{b,h} %w0, [%1]" : : "rZ"(val), "r"(addr));
 * This is a hardware store through a pointer register, NOT a GNU "Qo"
 * memory operand. Core carries two true scalar inputs and preserves its
 * implicit write effect via a single target-neutral contract flag. */
typedef struct Aarch64RawIoStore {
    const char *source;
    const char *instruction;
    size_t width;
} Aarch64RawIoStore;
static const Aarch64RawIoStore aarch64_raw_io_stores[] = {
    {"strb%w0,[%1]", "strb w9, [x10]", 1U},
    {"strh%w0,[%1]", "strh w9, [x10]", 2U},
    {"str%w0,[%1]",  "str w9, [x10]", 4U},
    {"str%x0,[%1]",  "str x9, [x10]", 8U}
};
static const Aarch64RawIoStore *aarch64_raw_io_store(
    const char *text, size_t length) {
    char compact[64];
    size_t i, n = 0U;
    if (!text || !length || length >= sizeof(compact)) return NULL;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t') continue;
        /* A second hardware instruction (including LF, CR or ';') is
         * not a member of this specific Linux MMIO operation family. */
        if (c == '\r' || c == '\n' || c == ';' ||
            n + 1U >= sizeof(compact)) return NULL;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(aarch64_raw_io_stores) /
                         sizeof(aarch64_raw_io_stores[0]); ++i)
        if (strcmp(aarch64_raw_io_stores[i].source, compact) == 0)
            return &aarch64_raw_io_stores[i];
    return NULL;
}


/* Original arm64 barrier.h four-width hardware acquire load. */
static size_t aarch64_load_acquire_width(const char *s,size_t len) {
    static const char *const kinds[]={
        "ldarb%w0,%1","ldarh%w0,%1","ldar%w0,%1","ldar%0,%1"
    };
    char norm[64];size_t i,n=0U;
    if(!s||!len||len>=sizeof(norm))return 0U;
    for(i=0U;i<len;++i) {
        const char c=s[i];
        if(c==' '||c==9)continue;
        if(c==10||c==13||c==';'||n+1U>=sizeof(norm))return 0U;
        norm[n++]=c;
    }
    norm[n]=0;
    for(i=0U;i<4U;++i)
        if(strcmp(norm,kinds[i])==0)return (size_t)1U<<i;
    return 0U;
}

/* Real Linux arm64 store-release GNU ASM: release ordering is hardware. */
static size_t aarch64_stlr_width(const char *s, size_t len) {
    static const char *const form[] = {
        NULL, "stlrb%w1,%0", "stlrh%w1,%0", NULL,
        "stlr%w1,%0", NULL, NULL, NULL, "stlr%x1,%0"
    };
    char norm[64]; size_t i, n = 0U;
    if (!s || !len || len >= sizeof(norm)) return 0U;
    for (i = 0U; i < len; ++i) {
        if (s[i] == '\n' || s[i] == '\r' || s[i] == ';') return 0U;
        if (s[i] == ' ' || s[i] == '\t') continue;
        if (n + 1U >= sizeof(norm)) return 0U;
        norm[n++] = s[i];
    }
    norm[n] = '\0';
    for (i = 1U; i <= 8U; ++i)
        if (form[i] && strcmp(norm, form[i]) == 0) return i;
    return 0U;
}
static bool aarch64_stlr_type(MinicType type, size_t width) {
    MinicType plain;
    if (!minic_type_unqualified(type, &plain) ||
        plain.base_kind != MINIC_TYPE_BASE_INT) return false;
    return (width == 1U && plain.integer_rank == MINIC_INTEGER_RANK_CHAR) ||
           (width == 2U && plain.integer_rank == MINIC_INTEGER_RANK_SHORT) ||
           (width == 4U && plain.integer_rank == MINIC_INTEGER_RANK_INT) ||
           (width == 8U && (plain.integer_rank == MINIC_INTEGER_RANK_LONG ||
                            plain.integer_rank == MINIC_INTEGER_RANK_LONG_LONG));
}

/* AAPCS64 C pointer occupies one 64-bit GPR, including typed volatile
 * pointer lvalues in original Linux RCU publish/read primitives.
 * Only a MEMORY address role can be pointer-valued. Scalar ALU and
 * MMIO value contracts remain integer-only and width checked. */
static bool aarch64_stlr_memory_type(MinicType type,size_t width){
    MinicType plain;
    return aarch64_stlr_type(type,width)||
        (width==1U&&minic_type_unqualified(type,&plain)&&
         minic_type_is_bool_integer(plain))||
        (width==8U&&minic_type_unqualified(type,&plain)&&
         minic_type_is_pointer(plain));
}

/* One typed AArch64 ISA descriptor for source qualification AND machine
 * emission. No ordinal op IDs, width thresholds or duplicated ALU tables:
 * new Linux atomics extend this target-owned operation/mode inventory only.
 * Common Core keeps exactly the existing structured operand roles. */
typedef struct {
    const char *name;
    const char *alu;
    const char *rmw_constraint;
    const char *fetch32_constraint;
    const char *fetch64_constraint;
} Aarch64AtomicOperation;

typedef struct {
    const Aarch64AtomicOperation *op;
    size_t width;
    unsigned int mode; /* 0=full, 1=relaxed, 2=acquire, 3=release */
} Aarch64AtomicForm;

static const Aarch64AtomicOperation aarch64_atomic_operations[] = {
    {"and", "and", "Lr", "Kr", "Lr"},
    {"or", "orr", "Lr", "Kr", "Lr"},
    {"xor", "eor", "Lr", "Kr", "Lr"},
    {"andnot", "bic", "r", "r", "r"},
    {"add", "add", "Ir", "Ir", "Ir"},
    {"sub", "sub", "Jr", "Jr", "Jr"}
};

/* The LSE half of Linux's atomic API is compiled alongside its LL/SC
 * fallback even on CPUs that select LL/SC at runtime. Its ISA extension is
 * an explicit instruction property of the inline-ASM template, independent
 * of the subset of instructions emitted by MiniC itself.
 *
 * Recognize complete GNU templates in both their named source and numeric
 * Core forms. No C function-name based admission, and no opaque fallback:
 * the same descriptor validates all 32/64-bit RMWs and 8/16/32/64-bit CAS.
 */
typedef enum {
    AARCH64_LSE_STORE, AARCH64_LSE_FETCH, AARCH64_LSE_CAS
} Aarch64LseKind;

typedef struct {
    Aarch64LseKind kind;
    const char *mnemonic;
    size_t width;
    unsigned int mode; /* RMW 0=relaxed, 1=acquire, 2=release, 3=acq_rel */
} Aarch64LseForm;

static bool aarch64_lse_form(const char *text, size_t length,
                            bool numeric, Aarch64LseForm *result) {
    static const struct {
        const char *store, *fetch;
    } operations[] = {
        {"stclr", "ldclr"}, {"stset", "ldset"},
        {"steor", "ldeor"}, {"stadd", "ldadd"}
    };
    static const char *const mode_suffix[] = {"", "a", "l", "al"};
    static const size_t cas_widths[] = {1U, 2U, 4U, 8U};
    static const char *const cas_suffix[] = {"b", "h", "", ""};
    char compact[160], expected[160];
    size_t i, n = 0U, operation, mode, width;
    if (!text || !length || length >= sizeof(compact) || !result)
        return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return false;
        compact[n++] = c;
    }
    compact[n] = '\0';

    for (width = 4U; width <= 8U; width += 4U) {
        const char *input = width == 8U ? "%" : "%w";
        for (operation = 0U; operation < sizeof(operations) /
                 sizeof(operations[0]); ++operation) {
            const char *names[2] = {
                operations[operation].store, operations[operation].fetch
            };
            unsigned int kind;
            for (kind = 0U; kind < 2U; ++kind) {
                for (mode = 0U; mode < (kind ? 4U : 1U); ++mode) {
                    const char *mnemonic = names[kind];
                    const int written = snprintf(expected, sizeof(expected),
                        ".arch_extensionlse%s%s%s%s,%s%s",
                        mnemonic, kind ? mode_suffix[mode] : "",
                        input, numeric ? (kind ? "2" : "1") : "[i]",
                        kind ? input : "",
                        kind ? (numeric ? "1,%0" : "[old],%[v]") :
                               (numeric ? "%0" : "%[v]"));
                    if (written > 0 && (size_t)written < sizeof(expected) &&
                        strcmp(compact, expected) == 0) {
                        result->kind = kind ? AARCH64_LSE_FETCH : AARCH64_LSE_STORE;
                        result->mnemonic = mnemonic;
                        result->width = width;
                        result->mode = (unsigned int)mode;
                        return true;
                    }
                }
            }
        }
    }
    for (width = 0U; width < 4U; ++width) {
        /* Linux's CAS macro spells 64-bit operands %x[old]/%x[new],
         * unlike 64-bit LSE fetch/store, whose placeholders are bare. */
        const char *reg = cas_widths[width] == 8U ? "%x" : "%w";
        for (mode = 0U; mode < 4U; ++mode) {
            const int written = snprintf(expected, sizeof(expected),
                ".arch_extensionlsecas%s%s%s%s,%s%s,%s",
                mode_suffix[mode], cas_suffix[width], reg,
                numeric ? "1" : "[old]", reg,
                numeric ? "2" : "[new]", numeric ? "%0" : "%[v]");
            if (written > 0 && (size_t)written < sizeof(expected) &&
                strcmp(compact, expected) == 0) {
                result->kind = AARCH64_LSE_CAS;
                result->mnemonic = "cas";
                result->width = cas_widths[width];
                result->mode = (unsigned int)mode;
                return true;
            }
        }
    }
    return false;
}

/* Reserve source atomic prefixes from exactly the same typed operation
 * catalog used for source qualification and physical LL/SC emission. */
bool minic_aarch64_requires_qualified_input_asm(const MinicInlineAsm *src) {
    static const char *const fixed[] = {
        "brk", "stlrb", "stlrh", "stlr", "ldarb", "ldarh", "ldar",
        "msr", "svc", "bic", ".if", ".irp", "dc", "ic",
        /* GNU .irp SYSREG metadata and D/I-cache operations are ISA-owned.
         * A rejected constraint must not reach the generic Core ASM path. */
        "strb", "strh", "str",
        /* Original Linux CMPXCHG begins with PRFM and named operands;
         * rejected constraints/modes must not escape into generic Core. */
        "prfm", "prfm", "1:", ".arch_extension lse", "rbit"
    };
    char prefix[64];
    const char *const single[] = {prefix};
    size_t width, fetch, op;
    /* The exact Linux one-label alternative is a qualified *control-flow*
     * shape handled by Core's generic asm-goto lowering. Its target-owned
     * section/ISA contract is checked again at machine admission. */
    if (minic_aarch64_linux_cap_asm_goto_source(src)) return false;
    /* Exact Linux asm-goto table records are not ordinary scalar ASM input.
     * Core independently re-verifies the complete source/operand contract. */
    if (src != NULL && src->is_goto &&
        aarch64_linux_jump_label_form(src->template_text,
                                      src->template_length) != 0U) return false;
    if (minic_backend_inline_asm_prefix_matches(
            src, fixed, sizeof(fixed) / sizeof(fixed[0])))
        return true;
    for (width = 0U; width < 2U; ++width)
        for (fetch = 0U; fetch < 2U; ++fetch)
            for (op = 0U; op < sizeof(aarch64_atomic_operations) /
                                  sizeof(aarch64_atomic_operations[0]); ++op) {
                const int n = snprintf(prefix, sizeof(prefix),
                    "// atomic%s%s_%s", width ? "64" : "",
                    fetch ? "_fetch" : "", aarch64_atomic_operations[op].name);
                if (n <= 0 || (size_t)n >= sizeof(prefix))
                    return false;
                if (minic_backend_inline_asm_prefix_matches(src, single, 1U))
                    return true;
            }
    return false;
}

static const char *const aarch64_atomic_mode_suffix[] = {
    "", "_relaxed", "_acquire", "_release"
};
static const char *const aarch64_atomic_load[] = {
    "ldxr", "ldxr", "ldaxr", "ldxr"
};
static const char *const aarch64_atomic_store[] = {
    "stlxr", "stxr", "stxr", "stlxr"
};

/* ARM64_LSE_ATOMIC_INSN expands to a two-body ALTERNATIVE, not just an
 * LL/SC loop. The full metadata is mandatory for late CPU-capability
 * patching, so split it only if EVERY section, relocation and .org token
 * matches Linux's native __ALTERNATIVE_CFG layout and the frozen LSE cap.
 * The two instruction bodies are subsequently checked by typed decoders. */
static bool aarch64_linux_alt_partition(
    const char *text, size_t length, unsigned int cap,
    char *old_body, size_t old_size,
    char *new_body, size_t new_size) {
    static const char prefix[] = ".if1==1661:";
    static const char suffix[] =
        "664:.org.-(664b-663b)+(662b-661b)"
        ".org.-(662b-661b)+(664b-663b)"
        ".previous.endif";
    char compact[2200], middle[192];
    const char *boundary, *end;
    size_t i, n = 0U, left, right;
    const int middle_length = snprintf(middle, sizeof(middle),
        "662:.pushsection.altinstructions,\"a\""
        ".word661b-..word663f-..hword%u"
        ".byte662b-661b.byte664f-663f"
        ".popsection.subsection1663:", cap);
    if (!text || !length || length >= sizeof(compact) ||
        middle_length < 1 || (size_t)middle_length >= sizeof(middle) ||
        !old_body || !new_body) return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return false;
        compact[n++] = c;
    }
    compact[n] = '\0';
    if (n <= sizeof(prefix) - 1U + (size_t)middle_length +
                 sizeof(suffix) - 1U ||
        memcmp(compact, prefix, sizeof(prefix) - 1U) != 0 ||
        memcmp(compact + n - sizeof(suffix) + 1U,
               suffix, sizeof(suffix) - 1U) != 0)
        return false;
    boundary = strstr(compact + sizeof(prefix) - 1U, middle);
    if (!boundary) return false;
    end = compact + n - sizeof(suffix) + 1U;
    left = (size_t)(boundary - (compact + sizeof(prefix) - 1U));
    right = (size_t)(end - (boundary + (size_t)middle_length));
    if (!left || !right || left >= old_size || right >= new_size)
        return false;
    memcpy(old_body, compact + sizeof(prefix) - 1U, left);
    old_body[left] = '\0';
    memcpy(new_body, boundary + (size_t)middle_length, right);
    new_body[right] = '\0';
    return true;
}
static bool aarch64_linux_alt_lse_partition(
    const char *text, size_t length,
    char *old_body, size_t old_size,
    char *new_body, size_t new_size) {
    return aarch64_linux_alt_partition(text, length, 33U,
                                       old_body, old_size,
                                       new_body, new_size);
}

/* Every complete Linux alternative uses the same two PREL32 patch offsets
 * and equal-length .org guards. The target-specific replacement may require
 * an ISA extension (LSE feature 33), or use only base arm64 instructions
 * (VHE alternate system-register access feature 50). */
static bool aarch64_linux_alt_emit_middle(
    FILE *out, unsigned int cap, bool uses_lse) {
    if (!out || (cap != 33U && cap != 47U && cap != 50U)) return false;
    return fprintf(out,
        "662:\n"
        "  .pushsection .altinstructions, \"a\"\n"
        "  .word 661b - .\n"
        "  .word 663f - .\n"
        "  .hword %u\n"
        "  .byte 662b - 661b\n"
        "  .byte 664f - 663f\n"
        "  .popsection\n"
        "  .subsection 1\n"
        "663:\n", cap) >= 0 &&
        (!uses_lse || fputs("  .arch_extension lse\n", out) != EOF);
}

static bool aarch64_linux_alt_emit_end(FILE *out) {
    return fprintf(out,
        "664:\n"
        "  .org . - (664b-663b) + (662b-661b)\n"
        "  .org . - (662b-661b) + (664b-663b)\n"
        "  .previous\n") >= 0;
}

/* Match the entire original GNU Linux template, not a mnemonic prefix.
 * No partial acceptance, arbitrary replacement opcode or opaque ASM
 * passthrough. Both source and Core machine recheck use this single owner. */
static bool aarch64_atomic_form(const char *text, size_t len, bool fetch,
                                Aarch64AtomicForm *result) {
    char compact[240], expected[240];
    size_t i, n = 0U, width_index, op_index, mode;
    if (!text || !len || !result || len >= sizeof(compact)) return false;
    for (i = 0U; i < len; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (n + 1U >= sizeof(compact)) return false;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (width_index = 0U; width_index < 2U; ++width_index) {
        const char *word0 = width_index ? "%0" : "%w0";
        const char *word1 = width_index ? "%1" : "%w1";
        const char *word3 = width_index ? "%3" : "%w3";
        const char *word4 = width_index ? "%4" : "%w4";
        for (op_index = 0U;
             op_index < sizeof(aarch64_atomic_operations) /
                            sizeof(aarch64_atomic_operations[0]); ++op_index) {
            const Aarch64AtomicOperation *op = &aarch64_atomic_operations[op_index];
            for (mode = 0U; mode < (fetch ? 4U : 1U); ++mode) {
                const int written = fetch ?
                    snprintf(expected, sizeof(expected),
                        "//atomic%s_fetch_%s%s"
                        "prfmpstl1strm,%%3"
                        "1:%s%s,%%3"
                        "%s%s,%s,%s"
                        "%s%%w2,%s,%%3"
                        "cbnz%%w2,1b%s",
                        width_index ? "64" : "", op->name,
                        aarch64_atomic_mode_suffix[mode],
                        aarch64_atomic_load[mode], word0,
                        op->alu, word1, word0, word4,
                        aarch64_atomic_store[mode], word1,
                        mode == 0U ? "dmbish" : "") :
                    snprintf(expected, sizeof(expected),
                        "//atomic%s_%s"
                        "prfmpstl1strm,%%2"
                        "1:ldxr%s,%%2"
                        "%s%s,%s,%s"
                        "stxr%%w1,%s,%%2"
                        "cbnz%%w1,1b",
                        width_index ? "64" : "", op->name,
                        word0, op->alu, word0, word0,
                        word3, word0);
                if (written > 0 && (size_t)written < sizeof(expected) &&
                    strcmp(compact, expected) == 0) {
                    result->op = op;
                    result->width = width_index ? 8U : 4U;
                    result->mode = (unsigned int)mode;
                    return true;
                }
            }
        }
    }
    return false;
}
/* Original GNU Linux 6.6.143 arm64 this_cpu_{add,andnot,or} +
 * add_return 8/16/32/64 LL/SC. One target-only descriptor table and the
 * existing Aarch64AtomicOperation ISA catalog, NOT 16 separate engines.
 * Source names (loop,tmp/ret,ptr,val) and Core numeric forms share one
 * complete decoder; extra opcodes, wrong status or width are rejected. */
typedef struct Aarch64PerCpuForm {
    const Aarch64AtomicOperation *op;
    size_t width;
    bool returns_new;
} Aarch64PerCpuForm;
static bool aarch64_linux_percpu_rmw_form(
    const char *text, size_t length, Aarch64PerCpuForm *result) {
    static const char *const suffix[] = {"b", "h", "", ""};
    static const size_t widths[] = {1U, 2U, 4U, 8U};
    char compact[256], expected[256];
    size_t i, n=0U, width, op_index, ret;
    {
        char old_body[256], new_body[256];
        Aarch64PerCpuForm old;
        if (aarch64_linux_alt_lse_partition(
                text, length, old_body, sizeof(old_body),
                new_body, sizeof(new_body))) {
            const char *mnemonic;
            const char *reg;
            const char *value, *memory, *return_value;
            const bool numeric = strstr(old_body, "%2") != NULL;
            unsigned int returns_new, nop_style;
            if (!aarch64_linux_percpu_rmw_form(
                    old_body, strlen(old_body), &old)) return false;
            reg = old.width == 8U ? "%" : "%w";
            value = numeric ? "3" : "[val]";
            memory = numeric ? "%2" : "%[ptr]";
            return_value = numeric ? "1" : "[ret]";
            /* The *numeric* old body has the same %1 spelling for tmp and
             * ret, so LSE's replacement must disambiguate whether the
             * source produces a return value. The named source is unique. */
            for (returns_new = 0U; returns_new < 2U; ++returns_new) {
              for (nop_style = 0U; nop_style < 2U; ++nop_style) {
                int written;
                if ((!numeric && old.returns_new != (returns_new != 0U)) ||
                    (returns_new && strcmp(old.op->name, "add") != 0))
                    continue;
                if (strcmp(old.op->name, "add") == 0)
                    mnemonic = returns_new ? "ldadd" : "stadd";
                else if (strcmp(old.op->name, "andnot") == 0)
                    mnemonic = "stclr";
                else if (strcmp(old.op->name, "or") == 0)
                    mnemonic = "stset";
                else continue;
                written = returns_new ? snprintf(expected, sizeof(expected),
                    ".arch_extensionlse%s%s%s,%s%s,%s"
                    "add%s%s,%s%s,%s%s%s",
                    mnemonic, reg, value, reg, return_value, memory,
                    reg, return_value, reg, return_value, reg, value,
                    nop_style ? ".rept2nop.endr" : "nopnop") :
                    snprintf(expected, sizeof(expected),
                    ".arch_extensionlse%s%s%s,%s%s",
                    mnemonic, reg, value, memory,
                    nop_style ? ".rept3nop.endr" : "nopnopnop");
                if (written > 0 && (size_t)written < sizeof(expected) &&
                    strcmp(new_body, expected) == 0) {
                    *result = old;
                    result->returns_new = returns_new != 0U;
                    return true;
                }
              }
            }
            return false;
        }
    }
    if (!text || !length || !result || length>=sizeof(compact)) return false;
    for(i=0U;i<length;++i) {
        const char c=text[i];
        if(c==' '||c=='\t'||c=='\n'||c=='\r')continue;
        if(n+1U>=sizeof(compact))return false;
        compact[n++]=c;
    }
    compact[n]='\0';
    for(width=0U;width<4U;++width) {
        const char *r=width==3U?"%":"%w";
        const char *n1=width==3U?"%1":"%w1";
        const char *n3=width==3U?"%3":"%w3";
        for(op_index=0U;op_index<sizeof(aarch64_atomic_operations)/
                                 sizeof(aarch64_atomic_operations[0]);++op_index) {
            const Aarch64AtomicOperation *op=&aarch64_atomic_operations[op_index];
            if(strcmp(op->name,"add")!=0 && strcmp(op->name,"andnot")!=0 &&
               strcmp(op->name,"or")!=0)continue;
            for(ret=0U;ret<2U;++ret) {
                const char *value_name=ret?"ret":"tmp";
                int written;
                if(ret&&strcmp(op->name,"add")!=0)continue;
                written=snprintf(expected,sizeof(expected),
                    "1:ldxr%s%s[%s],%%[ptr]"
                    "%s%s[%s],%s[%s],%s[val]"
                    "stxr%s%%w[loop],%s[%s],%%[ptr]"
                    "cbnz%%w[loop],1b",
                    suffix[width],r,value_name,
                    op->alu,r,value_name,r,value_name,r,
                    suffix[width],r,value_name);
                if(written>0&&(size_t)written<sizeof(expected)&&
                   strcmp(compact,expected)==0) {
                    result->op=op;result->width=widths[width];
                    result->returns_new=ret!=0U;return true;
                }
                written=snprintf(expected,sizeof(expected),
                    "1:ldxr%s%s,%%2"
                    "%s%s,%s,%s"
                    "stxr%s%%w0,%s,%%2"
                    "cbnz%%w0,1b",
                    suffix[width],n1,
                    op->alu,n1,n1,n3,
                    suffix[width],n1);
                if(written>0&&(size_t)written<sizeof(expected)&&
                   strcmp(compact,expected)==0) {
                    result->op=op;result->width=widths[width];
                    result->returns_new=ret!=0U;return true;
                }
            }
        }
    }
    return false;
}

/* Original Linux arch/arm64/include/asm/uaccess.h:
 * __uaccess_mask_ptr() removes bit 55 using a true AArch64 BIC logical
 * immediate. It is not a compiler-only barrier, pointer cast or C AND.
 * GNU source I is a *value* immediate, never a GPR input. */
/* Original Linux 6.6.143 __kern_my_cpu_offset() uses the GNU AArch64
 * ALTERNATIVE patch table. This is NOT equivalent to a single MRS: its
 * .altinstructions metadata is consumed by Linux at boot to patch MRS
 * TPIDR_EL1 to TPIDR_EL2 when feature 47 is available. Preserve the exact
 * section, relocations, relative label offsets and instruction sizes. */
/* Genuine original arm64 Linux atomic_{add,sub}_return and atomic64_
 * variants: four memory orders × two sizes × two operations (16).
 * Reuse the AArch64 target ISA operation and memory-order catalogs:
 * Common Core only carries typed source ASM operand roles. */
static bool aarch64_atomic_return_form(
    const char *text, size_t length, Aarch64AtomicForm *result) {
    char compact[240], expected[240];
    size_t i, n = 0U, width, op_index, mode;
    if (!text || !length || !result || length >= sizeof(compact)) return false;
    for (i = 0U; i < length; ++i) {
        const char ch = text[i];
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') continue;
        if (n + 1U >= sizeof(compact)) return false;
        compact[n++] = ch;
    }
    compact[n] = '\0';
    for (width = 0U; width < 2U; ++width) {
        const char *word = width ? "%0" : "%w0";
        const char *value = width ? "%3" : "%w3";
        for (op_index = 0U;
             op_index < sizeof(aarch64_atomic_operations) /
                            sizeof(aarch64_atomic_operations[0]); ++op_index) {
            const Aarch64AtomicOperation *op = &aarch64_atomic_operations[op_index];
            if (strcmp(op->name, "add") != 0 && strcmp(op->name, "sub") != 0)
                continue;
            for (mode = 0U; mode < 4U; ++mode) {
                const int written = snprintf(expected, sizeof(expected),
                    "//atomic%s_%s_return%s"
                    "prfmpstl1strm,%%2"
                    "1:%s%s,%%2"
                    "%s%s,%s,%s"
                    "%s%%w1,%s,%%2"
                    "cbnz%%w1,1b%s",
                    width ? "64" : "", op->name,
                    aarch64_atomic_mode_suffix[mode],
                    aarch64_atomic_load[mode], word,
                    op->alu, word, word, value,
                    aarch64_atomic_store[mode], word,
                    mode == 0U ? "dmbish" : "");
                if (written > 0 && (size_t)written < sizeof(expected) &&
                    strcmp(compact, expected) == 0) {
                    result->op = op;
                    result->width = width ? 8U : 4U;
                    result->mode = (unsigned int)mode;
                    return true;
                }
            }
        }
    }
    return false;
}
/* Frozen original GNU Linux 6.6.143 arm64 cmpxchg.h __xchg_case
 * 8/16/32/64 × relaxed/acquire/release/full: ONE strict decoder reused
 * by C source admission and native AArch64 LL/SC instruction emission.
 * The frozen config expands these as LL/SC, not LSE alternatives. */
typedef struct Aarch64XchgForm {
    size_t width;
    unsigned int mode;
} Aarch64XchgForm;
static const char *const aarch64_xchg_ld[4] = {"ld", "lda", "ld", "ld"};
static const char *const aarch64_xchg_st[4] = {"st", "st", "stl", "stl"};
static bool aarch64_linux_xchg_form(
    const char *text, size_t len, Aarch64XchgForm *result) {
    static const char *const suffix[] = {"b", "h", "", ""};
    static const char *const old_reg[] = {"%w0", "%w0", "%w0", "%0"};
    static const char *const new_reg[] = {"%w3", "%w3", "%w3", "%3"};
    char compact[240], expected[240];
    size_t i, n = 0U, width, mode;
    {
        static const char *const order[] = {"", "a", "l", "al"};
        char old_body[256], new_body[256];
        Aarch64XchgForm old;
        if (aarch64_linux_alt_lse_partition(
                text, len, old_body, sizeof(old_body),
                new_body, sizeof(new_body))) {
            unsigned int nop_style;
            if (!aarch64_linux_xchg_form(
                    old_body, strlen(old_body), &old)) return false;
            width = old.width == 1U ? 0U : old.width == 2U ? 1U :
                    old.width == 4U ? 2U : 3U;
            for (nop_style = 0U; nop_style < 2U; ++nop_style) {
                const int written = snprintf(expected, sizeof(expected),
                    ".arch_extensionlseswp%s%s%s,%s,%%2"
                    "%s%s",
                    order[old.mode], suffix[width], new_reg[width],
                    old_reg[width],
                    nop_style ? ".rept3nop.endr" : "nopnopnop",
                    old.mode == 3U ? "nop" : "");
                if (written > 0 && (size_t)written < sizeof(expected) &&
                    strcmp(new_body, expected) == 0) {
                    *result = old;
                    return true;
                }
            }
            return false;
        }
    }
    if (!text || !len || !result || len >= sizeof(compact)) return false;
    for (i = 0U; i < len; ++i) {
        const char ch = text[i];
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') continue;
        if (n + 1U >= sizeof(compact)) return false;
        compact[n++] = ch;
    }
    compact[n] = '\0';
    for (width = 0U; width < 4U; ++width)
        for (mode = 0U; mode < 4U; ++mode) {
            const int count = snprintf(expected, sizeof(expected),
                "prfmpstl1strm,%%2"
                "1:%sxr%s%s,%%2"
                "%sxr%s%%w1,%s,%%2"
                "cbnz%%w1,1b%s",
                aarch64_xchg_ld[mode], suffix[width], old_reg[width],
                aarch64_xchg_st[mode], suffix[width], new_reg[width],
                mode == 3U ? "dmbish" : "");
            if (count > 0 && (size_t)count < sizeof(expected) &&
                strcmp(compact, expected) == 0) {
                result->width = (size_t)1U << width;
                result->mode = (unsigned int)mode;
                return true;
            }
        }
    return false;
}
/* Original Linux 6.6.143 __ll_sc__cmpxchg_case_* uses real hardware
 * LDXR/LDAXR + EOR + conditional STXR/STLXR.  The narrow 8/16-bit
 * forms use LDAXRB/H and STLXRB/H and compare the ZERO-EXTENDED old
 * word with an explicitly truncated unsigned-long source operand.
 * GNU source contains named %[oldval] etc; Core normalizes them to
 * positional %1 etc.  Match both for all four C widths and all four
 * memory orders, never an arbitrary inline ASM suffix or passthrough. */
static size_t aarch64_linux_cmpxchg_width(unsigned int form) {
    return form >= 1U && form <= 16U ?
        (size_t)1U << ((form - 1U) / 4U) : 0U;
}
static unsigned int aarch64_linux_cmpxchg_form(
    const char *text, size_t length, bool numeric) {
    static const char *const loads[] = {"ldxr", "ldaxr", "ldxr", "ldxr"};
    static const char *const stores[] = {"stxr", "stxr", "stlxr", "stlxr"};
    static const char *const suffixes[] = {"b", "h", "", ""};
    char compact[256], expected[256];
    size_t i, n = 0U, word, mode;
    if (!text || !length || length >= sizeof(compact)) return 0U;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (n + 1U >= sizeof(compact)) return 0U;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (word = 0U; word < 4U; ++word) {
        const char *mem = numeric ? "%2" : "%[v]";
        const char *oldval = numeric ?
            (word == 3U ? "%1" : "%w1") :
            (word == 3U ? "%[oldval]" : "%w[oldval]");
        const char *tmp = numeric ?
            (word == 3U ? "%0" : "%w0") :
            (word == 3U ? "%[tmp]" : "%w[tmp]");
        const char *status = numeric ? "%w0" : "%w[tmp]";
        const char *old = numeric ?
            (word == 3U ? "%3" : "%w3") :
            (word == 3U ? "%[old]" : "%w[old]");
        const char *newval = numeric ?
            (word == 3U ? "%4" : "%w4") :
            (word == 3U ? "%[new]" : "%w[new]");
        for (mode = 0U; mode < 4U; ++mode) {
            const int count = snprintf(expected, sizeof(expected),
                "prfmpstl1strm,%s1:%s%s%s,%s"
                "eor%s,%s,%scbnz%s,2f"
                "%s%s%s,%s,%scbnz%s,1b%s2:",
                mem, loads[mode], suffixes[word], oldval, mem,
                tmp, oldval, old, tmp,
                stores[mode], suffixes[word], status, newval, mem,
                status, mode == 3U ? "dmbish" : "");
            if (count > 0 && (size_t)count < sizeof(expected) &&
                strcmp(compact, expected) == 0)
                return (unsigned int)(word * 4U + mode + 1U);
        }
    }
    return 0U;
}
static bool aarch64_linux_percpu_alternative_template(
    const char *text, size_t length) {
    static const char exact[] =
        ".if1==1661:mrs%0,tpidr_el1662:"
        ".pushsection.altinstructions,\"a\""
        ".word661b-..word663f-..hword47"
        ".byte662b-661b.byte664f-663f"
        ".popsection.subsection1663:mrs%0,tpidr_el2664:"
        ".org.-(664b-663b)+(662b-661b)"
        ".org.-(662b-661b)+(664b-663b)"
        ".previous.endif";
    char normalized[512];
    size_t i, n = 0U;
    if (!text || !length || length >= sizeof(normalized)) return false;
    for (i = 0U; i < length; ++i) {
        const char ch = text[i];
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n')
            continue;
        if (n + 1U >= sizeof(normalized)) return false;
        normalized[n++] = ch;
    }
    normalized[n] = '\0';
    return strcmp(normalized, exact) == 0;
}

/* Write-side twin of Linux __kern_my_cpu_offset(): the same 12-byte feature
 * 47 patch-table record selects TPIDR_EL1 vs TPIDR_EL2 at boot. It changes
 * processor-local state rather than an ordinary C memory object. Accept
 * exactly this source template, including the two length equality .orgs. */
static bool aarch64_linux_percpu_write_alternative_template(
    const char *text, size_t length) {
    static const char expected[] =
        ".if1==1661:msrtpidr_el1,%0662:"
        ".pushsection.altinstructions,\"a\""
        ".word661b-..word663f-..hword47"
        ".byte662b-661b.byte664f-663f"
        ".popsection.subsection1663:msrtpidr_el2,%0664:"
        ".org.-(664b-663b)+(662b-661b)"
        ".org.-(662b-661b)+(664b-663b)"
        ".previous.endif";
    char normalized[512];
    size_t i, n = 0U;
    if (!text || length == 0U || length >= sizeof(normalized))
        return false;
    for (i = 0U; i < length; ++i) {
        const char ch = text[i];
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n')
            continue;
        if (ch == '\0' || n + 1U >= sizeof(normalized)) return false;
        normalized[n++] = ch;
    }
    normalized[n] = '\0';
    return strcmp(normalized, expected) == 0;
}
/* Linux 6.6 arm64 KVM barrier alternatives: the feature-71
 * workaround 1508412 replaces NOP by DMB SY around PAR_EL1; the feature-95
 * stage-2 page-table update replaces NOP by ISB. Both must retain their
 * original boot-time CPU patch records, two PREL32 offsets and both GNU
 * equal-length .org guards. Match the *whole* instruction family, not an
 * arbitrary replacement sequence or ignored compiler barrier. */
typedef struct Aarch64LinuxBarrierAlternative {
    unsigned int cap;
    const char *new_instruction;
} Aarch64LinuxBarrierAlternative;
static const Aarch64LinuxBarrierAlternative aarch64_linux_barrier_alternatives[] = {
    {71U, "dmbsy"},
    {95U, "isb"}
};
static const Aarch64LinuxBarrierAlternative *aarch64_linux_barrier_alternative(
    const char *text, size_t length) {
    char normalized[512], expected[512];
    size_t i, n = 0U, k;
    if (!text || !length || length >= sizeof(normalized))
        return NULL;
    for (i = 0U; i < length; ++i) {
        const char ch = text[i];
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r')
            continue;
        if (ch == '\0' || n + 1U >= sizeof(normalized))
            return NULL;
        normalized[n++] = ch;
    }
    normalized[n] = '\0';
    for (k = 0U; k < sizeof(aarch64_linux_barrier_alternatives) /
                        sizeof(aarch64_linux_barrier_alternatives[0]); ++k) {
        const Aarch64LinuxBarrierAlternative *form =
            &aarch64_linux_barrier_alternatives[k];
        const int count = snprintf(expected, sizeof(expected),
            ".if1==1661:nop662:"
            ".pushsection.altinstructions,\"a\""
            ".word661b-..word663f-..hword%u"
            ".byte662b-661b.byte664f-663f"
            ".popsection.subsection1663:%s664:"
            ".org.-(664b-663b)+(662b-661b)"
            ".org.-(662b-661b)+(664b-663b)"
            ".previous.endif",
            form->cap, form->new_instruction);
        if (count > 0 && (size_t)count < sizeof(expected) &&
            strcmp(normalized, expected) == 0) return form;
    }
    return NULL;
}

static bool aarch64_linux_barrier_alternative_emit(
    FILE *out, const Aarch64LinuxBarrierAlternative *form) {
    if (!form) return false;
    return out != NULL && fprintf(out,
        "  .if 1 == 1\n"
        "661:\n"
        "  nop\n"
        "662:\n"
        "  .pushsection .altinstructions,\"a\"\n"
        "  .word 661b - .\n"
        "  .word 663f - .\n"
        "  .hword %u\n"
        "  .byte 662b - 661b\n"
        "  .byte 664f - 663f\n"
        "  .popsection\n"
        "  .subsection 1\n"
        "663:\n"
        "  %s\n"
        "664:\n"
        "  .org . - (664b-663b) + (662b-661b)\n"
        "  .org . - (662b-661b) + (664b-663b)\n"
        "  .previous\n"
        "  .endif\n",
        form->cap,
        form->cap == 71U ? "dmb sy" : "isb") >= 0;
}
bool minic_aarch64_linux_barrier_alternative_opaque(
    const MinicCoreInlineAsm *a) {
    return a != NULL && a->is_volatile && !a->is_goto &&
           a->register_clobber_count == 0U && !a->has_memory_clobber &&
           aarch64_linux_barrier_alternative(
               a->template_text, a->template_length) != NULL;
}
bool minic_aarch64_emit_linux_barrier_alternative_opaque(
    FILE *out, const MinicCoreInlineAsm *a) {
    return minic_aarch64_linux_barrier_alternative_opaque(a) &&
           aarch64_linux_barrier_alternative_emit(out,
               aarch64_linux_barrier_alternative(
                   a->template_text, a->template_length));
}
static bool constraint(const MinicInlineAsmOperand *op, const char *word);
/* Linux 6.6 KVM __translate_far_to_hpfar: AT S1E1R can trap while running
 * at EL2. The original GNU inline assembly snapshots SPSR_EL2/ELR_EL2,
 * issues AT, restores the exception state on its fixup path, and emits a
 * two-PREL32 hyp exception-table record. Exactly this complete sequence is
 * required: dropping the table or modelling AT as a normal read is unsafe. */
static bool aarch64_linux_kvm_at_fixup_template(
    const char *text, size_t length) {
    static const char exact[] =
        "mrs%1,spsr_el2mrs%2,elr_el21:ats1e1r,%3"
        "isbb9f2:msrspsr_el2,%1msrelr_el2,%2mov%w0,%4"
        "9:.pushsection__kvm_ex_table,\"a\""
        ".align3.long(1b-.),(2b-.).popsection";
    char compact[256];
    size_t i, n = 0U;
    if (!text || !length || length >= sizeof(compact)) return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return false;
        compact[n++] = c;
    }
    compact[n] = '\0';
    return strcmp(compact, exact) == 0;
}

static bool aarch64_linux_kvm_at_fixup_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    static const char *const output_constraint[] = {"+r", "=&r", "=&r"};
    static const char *const input_constraint[] = {"r", "i"};
    const MinicTargetInfo *target = minic_target_info_for_architecture(
        MINIC_TARGET_ARCH_AARCH64);
    MinicConstValue immediate;
    size_t i;
    if (!program || !src || !contract || !src->is_volatile || src->is_goto ||
        !src->outputs || !src->inputs || src->output_count != 3U ||
        src->input_count != 2U || src->label_count || src->clobber_count ||
        src->register_clobber_count || src->has_memory_clobber ||
        !aarch64_linux_kvm_at_fixup_template(
            src->template_text, src->template_length)) return false;
    for (i = 0U; i < 5U; ++i) {
        const bool output = i < 3U;
        const MinicInlineAsmOperand *operand = output ?
            &src->outputs[i] : &src->inputs[i - 3U];
        const MinicExpression *expr = minic_c0_program_expression(
            program, operand->expression);
        MinicType plain;
        if (!constraint(operand, output ? output_constraint[i] :
                        input_constraint[i - 3U]) || !expr ||
            !minic_type_unqualified(expr->type, &plain) ||
            (output && (expr->value_category != MINIC_VALUE_LVALUE ||
                        minic_type_is_const(expr->type))) ||
            (i == 0U || i == 4U ?
                !aarch64_stlr_type(plain, 4U) :
                !aarch64_linux_sysreg_u64_type(plain))) return false;
        contract->kinds[i] = i == 0U ?
            MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE :
            output ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
                     MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    }
    /* The `i` operand in Linux's hyp exception fixup is -EFAULT (-14).
     * Do not turn any run-time scalar into a GNU compile-time immediate. */
    if (!target || !minic_const_eval_integer(
            program, target, src->inputs[1].expression, &immediate) ||
        (uint32_t)immediate.bits != UINT32_C(0xfffffff2)) return false;
    contract->operand_count = 5U;
    contract->implicit_memory_effect = true;
    return true;
}

static bool aarch64_linux_kvm_at_fixup_emit(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreFunction *fn = ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *ops =
        ins->value.structured_inline_asm.operands;
    size_t i;
    if (!out || !fn || !ops || !info || ctx->dynamic_frame ||
        !info->is_volatile || info->is_goto ||
        !info->has_memory_clobber || info->register_clobber_count ||
        !aarch64_linux_kvm_at_fixup_template(
            info->template_text, info->template_length) ||
        ins->value.structured_inline_asm.operand_count != 5U)
        return false;
    for (i = 0U; i < 5U; ++i) {
        const MinicCoreStructuredInlineAsmOperandKind expected = i == 0U ?
            MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE :
            i < 3U ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
                     MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        MinicType type;
        if (ops[i].operand_index != i || ops[i].kind != expected ||
            ops[i].has_fixed_register_binding ||
            ops[i].value >= fn->value_count ||
            !aarch64_linux_structured_spill_slot_valid(ops[i].value) ||
            (i < 3U && !minic_type_pointee(
                fn->values[ops[i].value].type, &type)) ||
            (i >= 3U && !minic_type_unqualified(
                fn->values[ops[i].value].type, &type)) ||
            (i == 0U || i == 4U ?
                !aarch64_stlr_type(type, 4U) :
                !aarch64_linux_sysreg_u64_type(type))) return false;
    }
    return fprintf(out,
        "  ldr x13, [sp, #%zu]\n"
        "  ldr w9, [x13]\n"
        "  ldr x14, [sp, #%zu]\n"
        "  mrs x10, spsr_el2\n"
        "  mrs x11, elr_el2\n"
        "1: at s1e1r, x14\n"
        "  isb\n"
        "  b 9f\n"
        "2: msr spsr_el2, x10\n"
        "  msr elr_el2, x11\n"
        "  mov w9, #-14\n"
        "9:\n"
        "  .pushsection __kvm_ex_table, \"a\"\n"
        "  .align 3\n"
        "  .long (1b - .), (2b - .)\n"
        "  .popsection\n"
        "  str w9, [x13]\n"
        "  ldr x12, [sp, #%zu]\n"
        "  str x10, [x12]\n"
        "  ldr x12, [sp, #%zu]\n"
        "  str x11, [x12]\n",
        (size_t)ops[0].value * 8U,
        (size_t)ops[3].value * 8U,
        (size_t)ops[1].value * 8U,
        (size_t)ops[2].value * 8U) >= 0;
}
/* AT S1E1R explicitly requests a stage-1 EL1 address translation and
 * updates PAR_EL1. The operand is a virtual address, not an ordinary load;
 * keep the privileged side effect and reject arbitrary AT operations. */
static bool aarch64_linux_at_s1e1r_template(
    const char *text, size_t length) {
    static const char exact[] = "ats1e1r,%0";
    char normalized[64];
    size_t i, n = 0U;
    if (!text || !length || length >= sizeof(normalized)) return false;
    for (i = 0U; i < length; ++i) {
        const char ch = text[i];
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n')
            continue;
        if (ch == '\0' || n + 1U >= sizeof(normalized)) return false;
        normalized[n++] = ch;
    }
    normalized[n] = '\0';
    return strcmp(normalized, exact) == 0;
}

static bool aarch64_linux_at_s1e1r_source(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *address;
    MinicType plain;
    if (!program || !source || !contract || !source->is_volatile ||
        source->is_goto || source->output_count || source->input_count != 1U ||
        !source->inputs || source->label_count || source->clobber_count ||
        source->register_clobber_count || source->has_memory_clobber ||
        !constraint(&source->inputs[0], "r") ||
        !aarch64_linux_at_s1e1r_template(
            source->template_text, source->template_length))
        return false;
    address = minic_c0_program_expression(
        program, source->inputs[0].expression);
    if (!address || !minic_type_unqualified(address->type, &plain) ||
        !minic_type_equal(plain, minic_type_unsigned_long()))
        return false;
    contract->operand_count = 1U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    return true;
}

static bool aarch64_linux_at_s1e1r_emit(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreFunction *fn;
    const MinicCoreStructuredInlineAsmOperand *op;
    if (!out || !ctx || !ctx->fn || !ins || !info) return false;
    fn = ctx->fn;
    op = ins->value.structured_inline_asm.operands;
    if (!info->is_volatile || info->is_goto || info->has_memory_clobber ||
        info->register_clobber_count || ctx->dynamic_frame ||
        !aarch64_linux_at_s1e1r_template(
            info->template_text, info->template_length) ||
        ins->value.structured_inline_asm.operand_count != 1U || !op ||
        op[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
        op[0].operand_index != 0U || op[0].early_clobber ||
        op[0].has_fixed_register_binding || op[0].value >= fn->value_count ||
        !aarch64_linux_structured_spill_slot_valid(op[0].value) ||
        !minic_type_equal(fn->values[op[0].value].type,
                          minic_type_unsigned_long()))
        return false;
    return fprintf(out, "  ldr x9, [sp, #%zu]\n  at s1e1r, x9\n",
                   (size_t)op[0].value * 8U) >= 0;
}
static bool aarch64_linux_uaccess_bic_template(
    const char *text, size_t length) {
    static const char expected[] = "bic%0,%1,%2";
    char normalized[64];
    size_t i, n = 0U;
    if (!text || length == 0U || length >= sizeof(normalized)) return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        /* Original Linux embeds a trailing GNU newline and a leading
         * tab. Exact-token normalization strips whitespace, including
         * line breaks; an appended executable suffix still differs from
         * "bic%0,%1,%2" and is rejected, never passed through. */
        if (c == ';') return false;
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (n + 1U >= sizeof(normalized)) return false;
        normalized[n++] = c;
    }
    normalized[n] = '\0';
    return strcmp(normalized, expected) == 0;
}
/* Original Linux arm64 sysreg.h write_sysreg(): a privileged hardware
 * effect with one 64-bit rZ input. Keep the exact named EL0/EL1/EL2
 * register allowlist and reject arbitrary substituted GNU ASM. Source and
 * machine emission consult this one target ISA contract. */
static const char *aarch64_linux_msr_register(const char *text, size_t len) {
    /* Linux debug-sr.h expands restore_debug() into the full 16-slot
     * architectural breakpoint/watchpoint register bank.  Keep the
     * finite set of named GNU registers, never a generic `msr %s` gate. */
#define AARCH64_LINUX_DEBUG_WRITES(n) \
    "dbgbcr" #n "_el1", "dbgbvr" #n "_el1", \
    "dbgwcr" #n "_el1", "dbgwvr" #n "_el1"
    /* PMUv3 event registers are architecturally indexed 0..30; KVM and
     * arm_pmuv3.c write both counter and event-selector banks. */
#define AARCH64_LINUX_PMU_WRITES(n) \
    "pmevcntr" #n "_el0", "pmevtyper" #n "_el0"
    static const char *const registers[] = {
        "daif", "ttbr0_el1", "ttbr1_el1", "tcr_el1", "sctlr_el1",
        "contextidr_el1", "cpacr_el1", "vbar_el1", "mdscr_el1",
        "osdlr_el1", "oslar_el1", "cntkctl_el1",
        "cntp_ctl_el0", "cntp_cval_el0", "cntv_ctl_el0",
        "cntv_cval_el0", "tpidr_el0", "tpidrro_el0",
        "tpidr_el2", "hcr_el2", "cptr_el2", "vtcr_el2",
        "vttbr_el2", "fpexc32_el2", "hstr_el2", "mdcr_el2",
        /* Linux 6.6.143 KVM hyp/PMU writes use the same one-input rZ
         * sysreg contract.  These names are GNU AArch64 hardware operands,
         * not ordinary C variables or aliases; preserve the MSR effect. */
        "par_el1", "elr_el2", "spsr_el2", "spsr_abt", "spsr_und",
        "vmpidr_el2", "cnthctl_el2", "cntvoff_el2", "pmuserenr_el0",
        "pmcntenset_el0", "pmcntenclr_el0", "pmovsclr_el0",
        "pmselr_el0",
        "pmintenset_el1", "pmintenclr_el1", "pmccntr_el0",
        "pmccfiltr_el0", "pmcr_el0", "CPACR_EL1",
        "mdccint_el1", "tpidr_el1", "sp_el1",
        "spsr_irq", "spsr_fiq", "dacr32_el2", "ifsr32_el2",
        "dbgvcr32_el2", "mair_el1", "vbar_el2",
        AARCH64_LINUX_DEBUG_WRITES(0), AARCH64_LINUX_DEBUG_WRITES(1),
        AARCH64_LINUX_DEBUG_WRITES(2), AARCH64_LINUX_DEBUG_WRITES(3),
        AARCH64_LINUX_DEBUG_WRITES(4), AARCH64_LINUX_DEBUG_WRITES(5),
        AARCH64_LINUX_DEBUG_WRITES(6), AARCH64_LINUX_DEBUG_WRITES(7),
        AARCH64_LINUX_DEBUG_WRITES(8), AARCH64_LINUX_DEBUG_WRITES(9),
        AARCH64_LINUX_DEBUG_WRITES(10), AARCH64_LINUX_DEBUG_WRITES(11),
        AARCH64_LINUX_DEBUG_WRITES(12), AARCH64_LINUX_DEBUG_WRITES(13),
        AARCH64_LINUX_DEBUG_WRITES(14), AARCH64_LINUX_DEBUG_WRITES(15),
        AARCH64_LINUX_PMU_WRITES(0), AARCH64_LINUX_PMU_WRITES(1),
        AARCH64_LINUX_PMU_WRITES(2), AARCH64_LINUX_PMU_WRITES(3),
        AARCH64_LINUX_PMU_WRITES(4), AARCH64_LINUX_PMU_WRITES(5),
        AARCH64_LINUX_PMU_WRITES(6), AARCH64_LINUX_PMU_WRITES(7),
        AARCH64_LINUX_PMU_WRITES(8), AARCH64_LINUX_PMU_WRITES(9),
        AARCH64_LINUX_PMU_WRITES(10), AARCH64_LINUX_PMU_WRITES(11),
        AARCH64_LINUX_PMU_WRITES(12), AARCH64_LINUX_PMU_WRITES(13),
        AARCH64_LINUX_PMU_WRITES(14), AARCH64_LINUX_PMU_WRITES(15),
        AARCH64_LINUX_PMU_WRITES(16), AARCH64_LINUX_PMU_WRITES(17),
        AARCH64_LINUX_PMU_WRITES(18), AARCH64_LINUX_PMU_WRITES(19),
        AARCH64_LINUX_PMU_WRITES(20), AARCH64_LINUX_PMU_WRITES(21),
        AARCH64_LINUX_PMU_WRITES(22), AARCH64_LINUX_PMU_WRITES(23),
        AARCH64_LINUX_PMU_WRITES(24), AARCH64_LINUX_PMU_WRITES(25),
        AARCH64_LINUX_PMU_WRITES(26), AARCH64_LINUX_PMU_WRITES(27),
        AARCH64_LINUX_PMU_WRITES(28), AARCH64_LINUX_PMU_WRITES(29),
        AARCH64_LINUX_PMU_WRITES(30)
    };
#undef AARCH64_LINUX_DEBUG_WRITES
#undef AARCH64_LINUX_PMU_WRITES
    char compact[96];
    size_t i, n = 0U;
    if (!text || !len || len >= sizeof(compact)) return NULL;
    for (i = 0U; i < len; ++i) {
        const char c = text[i];
        if (c == '\r' || c == '\n' || c == ';' || c == '\0') return NULL;
        if (c == ' ' || c == '\t') continue;
        if (n + 1U >= sizeof(compact)) return NULL;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(registers) / sizeof(registers[0]); ++i) {
        const size_t rlen = strlen(registers[i]);
        if (n == rlen + sizeof("msr,%x0") - 1U &&
            memcmp(compact, "msr", 3U) == 0 &&
            memcmp(compact + 3U, registers[i], rlen) == 0 &&
            memcmp(compact + 3U + rlen, ",%x0", 4U) == 0)
            return registers[i];
    }
    return NULL;
}
static bool constraint(const MinicInlineAsmOperand *op, const char *word) {
    size_t n = strlen(word);
    return op && op->constraint_text && op->constraint_length == n &&
           memcmp(op->constraint_text, word, n) == 0;
}
/* Original Linux arm64 io.h __io_par is a physical data dependency
 * (EOR + CBNZ), NOT an ordinary compiler barrier or a C assignment. */
static bool aarch64_linux_io_dependency_form(const char *text,size_t length){
    static const char exact[]="eor%0,%1,%1cbnz%0,.";
    char compact[80];size_t i,n=0U;
    if(!text||length==0U||length>=sizeof(compact))return false;
    for(i=0U;i<length;++i){
        const char c=text[i];
        if(c==' '||c=='\t'||c=='\n'||c=='\r')continue;
        if(c==';'||c=='\0'||n+1U>=sizeof(compact))return false;
        compact[n++]=c;
    }
    compact[n]='\0';
    return strcmp(compact,exact)==0;
}
static bool aarch64_linux_io_dependency_source(
    const MinicC0Program *program,const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract){
    const MinicExpression *dst,*arg;
    MinicType plain;
    if(!program||!src||!contract||!src->is_volatile||src->is_goto||
       !src->outputs||!src->inputs||src->output_count!=1U||
       src->input_count!=1U||src->label_count!=0U||
       !src->has_memory_clobber||src->clobber_count!=1U||
       src->register_clobber_count!=0U||
       !aarch64_linux_io_dependency_form(
           src->template_text,src->template_length)||
       !constraint(&src->outputs[0],"=r")||
       !constraint(&src->inputs[0],"r"))return false;
    dst=minic_c0_program_expression(program,src->outputs[0].expression);
    arg=minic_c0_program_expression(program,src->inputs[0].expression);
    if(!dst||!arg||dst->value_category!=MINIC_VALUE_LVALUE||
       minic_type_is_const(dst->type)||
       !minic_type_unqualified(dst->type,&plain)||
       !minic_type_equal(plain,minic_type_unsigned_long())||
       !minic_type_unqualified(arg->type,&plain)||
       !minic_type_equal(plain,minic_type_unsigned_long()))return false;
    contract->operand_count=2U;
    contract->kinds[0]=MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[1]=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    return true;
}
static bool aarch64_linux_io_dependency_emit(
    FILE *out,const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins,const MinicCoreInlineAsm *info){
    const MinicCoreFunction *fn=ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *b=
        ins->value.structured_inline_asm.operands;
    MinicType type;
    size_t i;
    if(!out||!ctx||!fn||!b||!info||ctx->dynamic_frame||
       !info->is_volatile||info->is_goto||
       !info->has_memory_clobber||info->register_clobber_count!=0U||
       !aarch64_linux_io_dependency_form(
           info->template_text,info->template_length)||
       ins->value.structured_inline_asm.operand_count!=2U)return false;
    for(i=0U;i<2U;++i)
        if(b[i].operand_index!=i||b[i].value>=fn->value_count||
           !aarch64_linux_structured_spill_slot_valid(b[i].value)||b[i].early_clobber||
           b[i].has_fixed_register_binding||
           b[i].kind!=(i==0U?
            MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
            MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT))return false;
    if(!minic_type_pointee(fn->values[b[0].value].type,&type)||
       !minic_type_equal(type,minic_type_unsigned_long())||
       !minic_type_equal(fn->values[b[1].value].type,
                         minic_type_unsigned_long()))return false;
    return fprintf(out,
        "  ldr x10, [sp, #%zu]\n"
        "  eor x9, x10, x10\n"
        "  cbnz x9, .\n"
        "  ldr x11, [sp, #%zu]\n"
        "  str x9, [x11]\n",
        (size_t)b[1].value*8U,(size_t)b[0].value*8U)>=0;
}

/* Exact original Linux 6.6.143 arm64 sysreg.h read_sysreg_s SYS_*:
 * a full GNU .irp/.equ/.macro/.inst/.purgem expansion, NOT a simple MRS.
 * Keep all source directives and SYS_* hardware encodings on target side.
 * Never pass privileged GNU macro statements through unqualified. */
static unsigned int aarch64_linux_encoded_sysreg_form(
    const char *text, size_t length) {
    static const char prefix[] = ".irpnum,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30.equ.L__gpr_num_x\\num,\\num.equ.L__gpr_num_w\\num,\\num.endr.equ.L__gpr_num_xzr,31.equ.L__gpr_num_wzr,31.macromrs_s,rt,sreg.inst(0xd5200000|(\\sreg)|(.L__gpr_num_\\rt)).endm";
    static const char suffix[] = ".purgemmrs_s";
    char normalized[768],expected[768];
    unsigned int op0,op1,crn,crm,op2;
    int parsed=0, count;
    size_t i,n=0U,prefix_len=sizeof(prefix)-1U;
    if(!text||!length||length>=sizeof(normalized))return 0U;
    for(i=0U;i<length;++i){
        const char ch=text[i];
        if(ch==' '||ch=='\t'||ch=='\r'||ch=='\n')continue;
        if(ch=='\0'||n+1U>=sizeof(normalized))return 0U;
        normalized[n++]=ch;
    }
    normalized[n]='\0';
    if(n<=prefix_len || memcmp(normalized,prefix,prefix_len)!=0)return 0U;
    if(sscanf(normalized+prefix_len,
              "mrs_s%%0,(((%u)<<19)|((%u)<<16)|((%u)<<12)|"
              "((%u)<<8)|((%u)<<5)).purgemmrs_s%n",
              &op0,&op1,&crn,&crm,&op2,&parsed)!=5 || parsed<=0 ||
       normalized[prefix_len+(size_t)parsed]!='\0' ||
       op0!=3U || op1>7U || crn>15U || crm>15U || op2>7U)
        return 0U;
    count=snprintf(expected,sizeof(expected),
          "%smrs_s%%0,(((%u)<<19)|((%u)<<16)|((%u)<<12)|"
          "((%u)<<8)|((%u)<<5))%s",
          prefix,op0,op1,crn,crm,op2,suffix);
    if(count<=0||(size_t)count>=sizeof(expected)||
       strcmp(normalized,expected)!=0)return 0U;
    return 0xd5200000U |
           (op0<<19) | (op1<<16) | (crn<<12) | (crm<<8) | (op2<<5) |
           9U; /* x9 is the known emitted scratch register */
}
static bool aarch64_linux_encoded_sysreg_source(
    const MinicC0Program *program,const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *dst;
    MinicType plain;
    if(!program||!src||!contract||!src->is_volatile||src->is_goto||
       !src->outputs||src->output_count!=1U||src->input_count!=0U||
       src->label_count||src->clobber_count||src->register_clobber_count||
       src->has_memory_clobber||!constraint(&src->outputs[0],"=r")||
       !aarch64_linux_encoded_sysreg_form(
           src->template_text,src->template_length))return false;
    dst=minic_c0_program_expression(program,src->outputs[0].expression);
    if(!dst||dst->value_category!=MINIC_VALUE_LVALUE||
       minic_type_is_const(dst->type)||
       !minic_type_unqualified(dst->type,&plain)||
       !aarch64_linux_sysreg_u64_type(plain))return false;
    contract->operand_count=1U;
    contract->kinds[0]=MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    return true;
}
static bool aarch64_linux_encoded_sysreg_emit(
    FILE *out,const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins,const MinicCoreInlineAsm *info) {
    const unsigned int opcode=aarch64_linux_encoded_sysreg_form(
        info->template_text,info->template_length);
    const MinicCoreFunction *fn=ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *op=
        ins->value.structured_inline_asm.operands;
    MinicType type;
    if(!out||!ctx||!fn||!info||!op||!opcode||ctx->dynamic_frame||
       !info->is_volatile||info->is_goto||info->has_memory_clobber||
       info->register_clobber_count||
       ins->value.structured_inline_asm.operand_count!=1U||
       op[0].operand_index!=0U||op[0].value>=fn->value_count||
       !aarch64_linux_structured_spill_slot_valid(op[0].value)||op[0].early_clobber||
       op[0].has_fixed_register_binding||
       op[0].kind!=MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT||
       !minic_type_pointee(fn->values[op[0].value].type,&type)||
       !aarch64_linux_sysreg_u64_type(type))return false;
    return fprintf(out,"  .inst 0x%08x\n"
                       "  ldr x10, [sp, #%zu]\n"
                       "  str x9, [x10]\n",
           opcode,
           (size_t)op[0].value*8U)>=0;
}

typedef struct Aarch64LinuxWriteSysreg {
    unsigned int op0, op1, crn, crm, op2;
} Aarch64LinuxWriteSysreg;

/* Hardware-visible privileged SYS register effects remain an explicit
 * target ISA catalog. GICv3 ICC_* accesses have the same GNU .irp macro
 * template as ICC_SRE_EL1, with distinct encoded register fields. */
static const Aarch64LinuxWriteSysreg aarch64_linux_write_sysregs[] = {
    {3U,0U,12U,12U,5U}, /* ICC_SRE_EL1 */
    {3U,0U,4U,6U,0U},  /* ICC_PMR_EL1 */
    {3U,3U,4U,2U,2U},  /* SVCR */
    {3U,3U,13U,0U,5U}, /* TPIDR2_EL0 */
    /* kvm_host.h __vcpu_write_sys_reg_to_cpu(): complete VHE-only
     * runtime switch includes these seven additional SYS_* writes. */
    {3U,3U,13U,0U,2U}, /* TPIDR_EL0 */
    {3U,3U,13U,0U,3U}, /* TPIDRRO_EL0 */
    {3U,0U,13U,0U,4U}, /* TPIDR_EL1 */
    {3U,0U,7U,4U,0U},  /* PAR_EL1 */
    {3U,4U,3U,0U,0U},  /* DACR32_EL2 */
    {3U,4U,5U,0U,1U},  /* IFSR32_EL2 */
    {2U,4U,0U,7U,0U},  /* DBGVCR32_EL2 */
    /* Qualcomm Kryo's original Linux indirect L2 accessors use two
     * vendor-defined S3_3_C15_C0 registers, both authenticated against
     * the full GNU MSR_S wrapper and these exact tuples. */
    {3U,3U,15U,0U,6U}, /* L2CPUSRSELR_EL1 */
    {3U,3U,15U,0U,7U}, /* L2CPUSRDR_EL1 */
    {3U,0U,12U,11U,1U}, /* ICC_DIR_EL1 */
    {3U,0U,12U,11U,5U}, /* ICC_SGI1R_EL1 */
    {3U,0U,12U,12U,1U}, /* ICC_EOIR1_EL1 */
    {3U,0U,12U,12U,3U}, /* ICC_BPR1_EL1 */
    {3U,0U,12U,12U,4U}, /* ICC_CTLR_EL1 */
    {3U,0U,12U,12U,7U}, /* ICC_IGRPEN1_EL1 */
    /* Linux sysreg.h SYS_ICC_AP0Rn is CRm=8, op2=(4|n);
     * SYS_ICC_AP1Rn is CRm=9, op2=n (previous table was reversed). */
    {3U,0U,12U,8U,4U}, /* ICC_AP0R0_EL1 */
    {3U,0U,12U,8U,5U}, /* ICC_AP0R1_EL1 */
    {3U,0U,12U,8U,6U}, /* ICC_AP0R2_EL1 */
    {3U,0U,12U,8U,7U}, /* ICC_AP0R3_EL1 */
    {3U,0U,12U,9U,0U}, /* ICC_AP1R0_EL1 */
    {3U,0U,12U,9U,1U}, /* ICC_AP1R1_EL1 */
    {3U,0U,12U,9U,2U}, /* ICC_AP1R2_EL1 */
    {3U,0U,12U,9U,3U}, /* ICC_AP1R3_EL1 */
    {3U,4U,12U,9U,5U}, /* ICC_SRE_EL2 */
    /* Linux 6.6 arch/arm64/tools/sysreg + KVM hyp/switch.h: explicit
     * hypervisor trap controls. Each exact 5-field instruction encoding
     * is lowered as privileged MSR, never as a discarded GNU macro. */
    {3U,4U,1U,1U,4U}, /* HFGRTR_EL2 */
    {3U,4U,1U,1U,5U}, /* HFGWTR_EL2 */
    {3U,4U,1U,1U,6U}, /* HFGITR_EL2 */
    {3U,4U,3U,1U,4U}, /* HDFGRTR_EL2 */
    {3U,4U,3U,1U,5U}, /* HDFGWTR_EL2 */
    {3U,4U,1U,2U,2U}, /* HCRX_EL2 */
    {3U,4U,5U,2U,3U}, /* VSESR_EL2 */
    /* Linux original KVM PAUTH key save/restore: ten architectural
     * SYS_AP*KEY*EL1 names, with exact op1/CRn/CRm/op2 pairs. */
    {3U,0U,2U,1U,0U}, /* APIAKEYLO_EL1 */
    {3U,0U,2U,1U,1U}, /* APIAKEYHI_EL1 */
    {3U,0U,2U,1U,2U}, /* APIBKEYLO_EL1 */
    {3U,0U,2U,1U,3U}, /* APIBKEYHI_EL1 */
    {3U,0U,2U,2U,0U}, /* APDAKEYLO_EL1 */
    {3U,0U,2U,2U,1U}, /* APDAKEYHI_EL1 */
    {3U,0U,2U,2U,2U}, /* APDBKEYLO_EL1 */
    {3U,0U,2U,2U,3U}, /* APDBKEYHI_EL1 */
    {3U,0U,2U,3U,0U}, /* APGAKEYLO_EL1 */
    {3U,0U,2U,3U,1U}, /* APGAKEYHI_EL1 */
    /* The real Linux 6.6 KVM protected-hyp and VHE sources use these
     * additional architectural system registers through the same GNU
     * write_sysreg_s() machinery.  The tuple is the hardware encoding;
     * these are not template-string or function-name based exceptions.
     * EL1 control, translation, exception, timer and debug state. */
    {3U,0U,1U,0U,0U}, /* SCTLR_EL1 */
    {3U,0U,1U,0U,2U}, /* CPACR_EL1 */
    {3U,0U,1U,0U,5U}, /* RGSR_EL1 */
    {3U,0U,1U,0U,6U}, /* GCR_EL1 */
    {3U,0U,1U,2U,0U}, /* ZCR_EL1 */
    {3U,0U,1U,2U,1U}, /* TRFCR_EL1 */
    {3U,0U,12U,1U,1U}, /* DISR_EL1 */
    {3U,0U,2U,0U,0U}, /* TTBR0_EL1 */
    {3U,0U,2U,0U,1U}, /* TTBR1_EL1 */
    {3U,0U,2U,0U,2U}, /* TCR_EL1 */
    {3U,0U,2U,0U,3U}, /* TCR2_EL1 */
    {3U,0U,4U,0U,0U}, /* SPSR_EL1 */
    {3U,0U,4U,0U,1U}, /* ELR_EL1 */
    {3U,0U,5U,2U,0U}, /* ESR_EL1 */
    {3U,0U,5U,1U,0U}, /* AFSR0_EL1 */
    {3U,0U,5U,1U,1U}, /* AFSR1_EL1 */
    {3U,0U,5U,6U,0U}, /* TFSR_EL1 */
    {3U,0U,5U,6U,1U}, /* TFSRE0_EL1 */
    {3U,0U,6U,0U,0U}, /* FAR_EL1 */
    {3U,0U,10U,2U,0U}, /* MAIR_EL1 */
    {3U,0U,10U,2U,2U}, /* PIRE0_EL1 */
    {3U,0U,10U,2U,3U}, /* PIR_EL1 */
    {3U,0U,10U,3U,0U}, /* AMAIR_EL1 */
    {3U,0U,12U,0U,0U}, /* VBAR_EL1 */
    {3U,0U,13U,0U,1U}, /* CONTEXTIDR_EL1 */
    {3U,0U,14U,1U,0U}, /* CNTKCTL_EL1 */
    {3U,0U,9U,9U,0U}, /* PMSCR_EL1 */
    /* EL2 control, stage-2 translation, virtual GIC and timer registers. */
    {3U,4U,1U,0U,0U}, /* SCTLR_EL2 */
    {3U,4U,1U,2U,0U}, /* ZCR_EL2 */
    {3U,4U,12U,1U,1U}, /* DISR_EL2 */
    {3U,4U,12U,11U,0U}, /* ICH_HCR_EL2 */
    {3U,4U,12U,11U,7U}, /* ICH_VMCR_EL2 */
    /* arch/arm64/include/asm/sysreg.h: AP0R is CRm=8, AP1R is CRm=9;
     * list registers LR0..7 are CRm=12, LR8..15 are CRm=13. The former
     * abbreviated table accidentally swapped CRm for these families,
     * producing wrong hardware effects despite syntactically valid .inst. */
    {3U,4U,12U,8U,0U}, /* ICH_AP0R0_EL2 */
    {3U,4U,12U,8U,1U}, /* ICH_AP0R1_EL2 */
    {3U,4U,12U,8U,2U}, /* ICH_AP0R2_EL2 */
    {3U,4U,12U,8U,3U}, /* ICH_AP0R3_EL2 */
    {3U,4U,12U,9U,0U}, /* ICH_AP1R0_EL2 */
    {3U,4U,12U,9U,1U}, /* ICH_AP1R1_EL2 */
    {3U,4U,12U,9U,2U}, /* ICH_AP1R2_EL2 */
    {3U,4U,12U,9U,3U}, /* ICH_AP1R3_EL2 */
    {3U,4U,12U,12U,0U}, /* ICH_LR0_EL2 */
    {3U,4U,12U,12U,1U}, /* ICH_LR1_EL2 */
    {3U,4U,12U,12U,2U}, /* ICH_LR2_EL2 */
    {3U,4U,12U,12U,3U}, /* ICH_LR3_EL2 */
    {3U,4U,12U,12U,4U}, /* ICH_LR4_EL2 */
    {3U,4U,12U,12U,5U}, /* ICH_LR5_EL2 */
    {3U,4U,12U,12U,6U}, /* ICH_LR6_EL2 */
    {3U,4U,12U,12U,7U}, /* ICH_LR7_EL2 */
    {3U,4U,12U,13U,0U}, /* ICH_LR8_EL2 */
    {3U,4U,12U,13U,1U}, /* ICH_LR9_EL2 */
    {3U,4U,12U,13U,2U}, /* ICH_LR10_EL2 */
    {3U,4U,12U,13U,3U}, /* ICH_LR11_EL2 */
    {3U,4U,12U,13U,4U}, /* ICH_LR12_EL2 */
    {3U,4U,12U,13U,5U}, /* ICH_LR13_EL2 */
    {3U,4U,12U,13U,6U}, /* ICH_LR14_EL2 */
    {3U,4U,12U,13U,7U}, /* ICH_LR15_EL2 */
    {3U,4U,14U,0U,6U}, /* CNTVOFF_EL2 */
    {3U,4U,4U,0U,0U}, /* SPSR_EL2 */
    {3U,4U,4U,0U,1U}, /* ELR_EL2 */
    {3U,5U,1U,0U,0U}, /* SCTLR_EL12 */
    {3U,5U,1U,0U,2U}, /* CPACR_EL12 */
    {3U,5U,1U,2U,0U}, /* ZCR_EL12 */
    {3U,5U,14U,2U,2U}, /* EL12 virtualized timer control */
    {3U,5U,2U,0U,0U}, /* TTBR0_EL12 */
    {3U,5U,2U,0U,1U}, /* TTBR1_EL12 */
    {3U,5U,2U,0U,2U}, /* TCR_EL12 */
    {3U,5U,2U,0U,3U}, /* TCR2_EL12 */
    {3U,5U,4U,0U,0U}, /* SPSR_EL12 */
    {3U,5U,4U,0U,1U}, /* ELR_EL12 */
    {3U,5U,5U,1U,0U}, /* AFSR0_EL12 */
    {3U,5U,5U,1U,1U}, /* AFSR1_EL12 */
    {3U,5U,5U,2U,0U}, /* ESR_EL12 */
    {3U,5U,5U,6U,0U}, /* TFSR_EL12 */
    {3U,5U,6U,0U,0U}, /* FAR_EL12 */
    {3U,5U,10U,2U,0U}, /* MAIR_EL12 */
    {3U,5U,10U,2U,2U}, /* PIRE0_EL12 */
    {3U,5U,10U,2U,3U}, /* PIR_EL12 */
    {3U,5U,10U,3U,0U}, /* AMAIR_EL12 */
    {3U,5U,12U,0U,0U}, /* VBAR_EL12 */
    {3U,5U,13U,0U,1U}, /* CONTEXTIDR_EL12 */
    {3U,5U,14U,1U,0U}  /* CNTKCTL_EL12 */
};

/* Linux 6.6 arm64 sysreg.h write_sysreg_s() is the WRITE companion to
 * read_sysreg_s() above. Its real source uses .irp, .equ and a GNU macro
 * to assemble a privileged MSR system-register opcode. Unlike a direct
 * `msr foo, xN`, the named sreg is a constant expression encoded inside
 * .inst; preserving the complete verified expression is mandatory.
 * The kernel GICv3 SRE register is S3_0_C12_C12_5. Other arbitrary
 * opcodes and unknown SYS_* values remain outside this exact catalog. */
static unsigned int aarch64_linux_encoded_sysreg_write_form(
    const char *text, size_t length) {
    static const char prefix[] =
        ".irpnum,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30"
        ".equ.L__gpr_num_x\\num,\\num"
        ".equ.L__gpr_num_w\\num,\\num"
        ".endr"
        ".equ.L__gpr_num_xzr,31"
        ".equ.L__gpr_num_wzr,31"
        ".macromsr_s,sreg,rt"
        ".inst(0xd5000000|(\\sreg)|(.L__gpr_num_\\rt))"
        ".endm";
    char compact[768], expected[768];
    size_t i, used = 0U;
    if (!text || !length || length >= sizeof(compact)) return 0U;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || used + 1U >= sizeof(compact)) return 0U;
        compact[used++] = c;
    }
    compact[used] = '\0';
    for (i = 0U; i < sizeof(aarch64_linux_write_sysregs) /
                       sizeof(aarch64_linux_write_sysregs[0]); ++i) {
        const Aarch64LinuxWriteSysreg *reg = &aarch64_linux_write_sysregs[i];
        char op2_source[24];
        const int op2_n = reg->crn == 12U && reg->crm == 8U &&
                          reg->op2 >= 4U && reg->op2 <= 7U
            ? snprintf(op2_source, sizeof(op2_source), "4|%u", reg->op2 - 4U)
            : snprintf(op2_source, sizeof(op2_source), "%u", reg->op2);
        if (op2_n < 1 || (size_t)op2_n >= sizeof(op2_source)) return 0U;
        const int n = snprintf(expected, sizeof(expected),
            "%smsr_s(((%u)<<19)|((%u)<<16)|((%u)<<12)|"
            "((%u)<<8)|((%s)<<5)),%%x0.purgemmsr_s",
            prefix, reg->op0, reg->op1, reg->crn,
            reg->crm, op2_source);
        if (n > 0 && (size_t)n < sizeof(expected) &&
            strcmp(compact, expected) == 0)
            return (unsigned int)i + 1U;
    }
    return 0U;
}

static bool aarch64_linux_encoded_sysreg_write_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *value;
    MinicType plain;
    if (!program || !src || !contract || !src->is_volatile ||
        src->is_goto || src->output_count != 0U || src->input_count != 1U ||
        !src->inputs || src->clobber_count != 0U ||
        src->register_clobber_count || src->has_memory_clobber ||
        src->label_count != 0U || !constraint(&src->inputs[0], "rZ") ||
        !aarch64_linux_encoded_sysreg_write_form(
            src->template_text, src->template_length)) return false;
    value = minic_c0_program_expression(program, src->inputs[0].expression);
    if (!value || !minic_type_unqualified(value->type, &plain) ||
        (!minic_type_equal(plain, minic_type_unsigned_long()) &&
         !minic_type_equal(plain, minic_type_unsigned_long_long())))
        return false;
    contract->operand_count = 1U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    return true;
}

static bool aarch64_linux_encoded_sysreg_write_emit(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreFunction *fn = ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *op =
        ins->value.structured_inline_asm.operands;
    const unsigned int form = aarch64_linux_encoded_sysreg_write_form(
        info->template_text, info->template_length);
    const Aarch64LinuxWriteSysreg *reg;
    unsigned int encoding;
    MinicType plain;
    if (!form || form > sizeof(aarch64_linux_write_sysregs) /
                          sizeof(aarch64_linux_write_sysregs[0]) ||
        !info->is_volatile || info->is_goto ||
        info->has_memory_clobber || info->register_clobber_count ||
        ctx->dynamic_frame ||
        ins->value.structured_inline_asm.operand_count != 1U || !op ||
        op->operand_index != 0U ||
        op->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
        op->has_fixed_register_binding || op->early_clobber ||
        op->value >= fn->value_count ||
        !aarch64_linux_structured_spill_slot_valid(op->value) ||
        !minic_type_unqualified(fn->values[op->value].type, &plain) ||
        (!minic_type_equal(plain, minic_type_unsigned_long()) &&
         !minic_type_equal(plain, minic_type_unsigned_long_long())))
        return false;
    reg = &aarch64_linux_write_sysregs[form - 1U];
    encoding = (reg->op0 << 19) | (reg->op1 << 16) |
               (reg->crn << 12) | (reg->crm << 8) | (reg->op2 << 5);
    return fprintf(out, "  ldr x9, [sp, #%zu]\n  .inst 0x%08x\n",
        (size_t)op->value * 8U,
        0xd5000000U | encoding | 9U) >= 0;
}

/* Linux KVM's VHE feature-50 alternative is an ALTERNATIVE around two full
 * GNU .irp/.equ/.macro/.inst constructions, not an ordinary read_sysreg_s
 * instruction. Both arms MUST be emitted: VHE reads/writes SPSR_EL2/ELR_EL2
 * while non-VHE uses SPSR_EL1/ELR_EL1. Accept only exact architectural
 * register pairs with identical read/write direction, and preserve the
 * original capability record. Reuse the single-register GNU decoders. */
typedef struct Aarch64LinuxVheSysregAlternative {
    bool is_read;
    unsigned int capability;
    unsigned int old_opcode, new_opcode;
} Aarch64LinuxVheSysregAlternative;
/* Only register pairs corresponding to the exact KVM EL1/EL12 or EL2/EL1
 * alias move may be re-encoded.  Both must be known architectural SYS_*
 * tuple encodings. This is deliberately not an arbitrary GNU macro bridge. */
static bool aarch64_linux_vhe_sysreg_pair(
    const Aarch64LinuxWriteSysreg *old_reg,
    const Aarch64LinuxWriteSysreg *new_reg) {
    return old_reg && new_reg && old_reg->op0 == 3U &&
           new_reg->op0 == 3U && old_reg->crn == new_reg->crn &&
           old_reg->crm == new_reg->crm && old_reg->op2 == new_reg->op2 &&
           ((old_reg->op1 == 0U && new_reg->op1 == 5U) ||
            (old_reg->op1 == 4U && new_reg->op1 == 0U));
}
static const Aarch64LinuxWriteSysreg *aarch64_linux_known_sysreg_opcode(
    unsigned int opcode) {
    const unsigned int reg_bits = opcode & 0x001fffe0U;
    size_t i;
    for (i = 0U; i < sizeof(aarch64_linux_write_sysregs) /
                       sizeof(aarch64_linux_write_sysregs[0]); ++i) {
        const Aarch64LinuxWriteSysreg *reg = &aarch64_linux_write_sysregs[i];
        if (reg_bits == ((reg->op0 << 19) | (reg->op1 << 16) |
                         (reg->crn << 12) | (reg->crm << 8) |
                         (reg->op2 << 5))) return reg;
    }
    return NULL;
}
static bool aarch64_linux_vhe_sysreg_alternative_form(
    const char *text, size_t length,
    Aarch64LinuxVheSysregAlternative *result) {
    char old_body[768], new_body[768];
    unsigned int cap;
    if (!result) return false;
    for (cap = 47U; cap <= 50U; cap += 3U) {
        unsigned int old_opcode, new_opcode, old_write, new_write;
        const Aarch64LinuxWriteSysreg *old_reg, *new_reg;
        if (!aarch64_linux_alt_partition(
                text, length, cap, old_body, sizeof(old_body),
                new_body, sizeof(new_body))) continue;
        old_opcode = aarch64_linux_encoded_sysreg_form(
            old_body, strlen(old_body));
        new_opcode = aarch64_linux_encoded_sysreg_form(
            new_body, strlen(new_body));
        old_reg = aarch64_linux_known_sysreg_opcode(old_opcode);
        new_reg = aarch64_linux_known_sysreg_opcode(new_opcode);
        if (old_opcode && new_opcode &&
            aarch64_linux_vhe_sysreg_pair(old_reg, new_reg)) {
            result->is_read = true;
            result->capability = cap;
            result->old_opcode = old_opcode;
            result->new_opcode = new_opcode;
            return true;
        }
        old_write = aarch64_linux_encoded_sysreg_write_form(
            old_body, strlen(old_body));
        new_write = aarch64_linux_encoded_sysreg_write_form(
            new_body, strlen(new_body));
        if (old_write && new_write) {
            old_reg = &aarch64_linux_write_sysregs[old_write - 1U];
            new_reg = &aarch64_linux_write_sysregs[new_write - 1U];
            if (aarch64_linux_vhe_sysreg_pair(old_reg, new_reg)) {
                result->is_read = false;
                result->capability = cap;
                result->old_opcode = 0xd5000000U |
                    (old_reg->op0 << 19) | (old_reg->op1 << 16) |
                    (old_reg->crn << 12) | (old_reg->crm << 8) |
                    (old_reg->op2 << 5) | 9U;
                result->new_opcode = 0xd5000000U |
                    (new_reg->op0 << 19) | (new_reg->op1 << 16) |
                    (new_reg->crn << 12) | (new_reg->crm << 8) |
                    (new_reg->op2 << 5) | 9U;
                return true;
            }
        }
    }
    return false;
}

static bool aarch64_linux_vhe_sysreg_alternative_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    Aarch64LinuxVheSysregAlternative form;
    const MinicInlineAsmOperand *operand;
    const MinicExpression *value;
    MinicType plain;
    if (!program || !src || !contract || !src->is_volatile || src->is_goto ||
        src->label_count || src->clobber_count || src->register_clobber_count ||
        src->has_memory_clobber ||
        !aarch64_linux_vhe_sysreg_alternative_form(
            src->template_text, src->template_length, &form) ||
        src->output_count != (form.is_read ? 1U : 0U) ||
        src->input_count != (form.is_read ? 0U : 1U)) return false;
    operand = form.is_read ? src->outputs : src->inputs;
    if (!operand || !constraint(operand, form.is_read ? "=r" : "rZ"))
        return false;
    value = minic_c0_program_expression(program, operand->expression);
    if (!value || (form.is_read &&
        (value->value_category != MINIC_VALUE_LVALUE ||
         minic_type_is_const(value->type))) ||
        !minic_type_unqualified(value->type, &plain) ||
        !aarch64_linux_sysreg_u64_type(plain)) return false;
    contract->operand_count = 1U;
    contract->kinds[0] = form.is_read ?
        MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
        MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

static bool aarch64_linux_vhe_sysreg_alternative_emit(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info,
    const Aarch64LinuxVheSysregAlternative *form) {
    const MinicCoreFunction *fn = ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *op =
        ins->value.structured_inline_asm.operands;
    MinicType type;
    if (!out || !fn || !op || !info || !form || ctx->dynamic_frame ||
        !info->is_volatile || info->is_goto || !info->has_memory_clobber ||
        info->register_clobber_count ||
        ins->value.structured_inline_asm.operand_count != 1U ||
        op->operand_index != 0U || op->has_fixed_register_binding ||
        op->early_clobber || op->value >= fn->value_count ||
        !aarch64_linux_structured_spill_slot_valid(op->value) ||
        op->kind != (form->is_read ?
            MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
            MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT) ||
        (form->is_read ?
            !minic_type_pointee(fn->values[op->value].type, &type) :
            !minic_type_unqualified(fn->values[op->value].type, &type)) ||
        !aarch64_linux_sysreg_u64_type(type)) return false;
    if (fprintf(out, "  ldr %s, [sp, #%zu]\n661:\n  .inst 0x%08x\n",
                form->is_read ? "x10" : "x9",
                (size_t)op->value * 8U,
                form->old_opcode) < 0 ||
        !aarch64_linux_alt_emit_middle(out, form->capability, false) ||
        fprintf(out, "  .inst 0x%08x\n", form->new_opcode) < 0 ||
        !aarch64_linux_alt_emit_end(out)) return false;
    return !form->is_read || fputs("  str x9, [x10]\n", out) != EOF;
}

/* Entire original Linux 6.6.143 arm64 TLBI v8.5 control family.
 * The original source uses .arch armv8.5-a followed by TLBI and an rZ
 * GNU register-or-zero operand. This is a KERNEL HARDWARE invalidation,
 * not a C function call or an ordinary compiler memory fence. Keep all
 * original TLBI opcodes in this ISA target, never generic Core. */
enum { AARCH64_LINUX_TLBI_ADDR_FORM_COUNT = 13U };
static const char *const aarch64_linux_tlbi_names[] = {
    /* All forms before ADDR_FORM_COUNT have one rZ input. */
    "vale1is", "vale2is", "aside1is", "rvale1is",
    "vae1is", "rvae1is", "vaale1is", "vaae1is",
    /* KVM stage-2 and EL2 address translations. */
    "ipas2e1is", "ipas2e1", "vae2is", "ripas2e1is", "ripas2e1",
    /* Whole-context invalidations have no explicit operand. */
    "vmalle1", "vmalle1is", "alle1is", "vmalls12e1is",
    "vmalls12e1"
};
static unsigned int aarch64_linux_tlbi_form(const char *text,size_t length) {
    char normalized[128],expected[128];
    size_t i,n=0U;
    if(!text||!length||length>=sizeof(normalized))return 0U;
    for(i=0U;i<length;++i){
        const char ch=text[i];
        if(ch==' '||ch=='\t'||ch=='\r'||ch=='\n')continue;
        if(ch=='\0'||n+1U>=sizeof(normalized))return 0U;
        normalized[n++]=ch;
    }
    normalized[n]='\0';
    for(i=0U;i<sizeof(aarch64_linux_tlbi_names)/
                  sizeof(aarch64_linux_tlbi_names[0]);++i){
        const int written=snprintf(expected,sizeof(expected),
            ".archarmv8.5-atlbi%s%s",aarch64_linux_tlbi_names[i],
            i<AARCH64_LINUX_TLBI_ADDR_FORM_COUNT?",%x0":"");
        if(written>0&&(size_t)written<sizeof(expected)&&
           strcmp(normalized,expected)==0)return (unsigned int)i+1U;
    }
    return 0U;
}
static const char *aarch64_linux_tlbi_mnemonic(unsigned int form) {
    return !form || (size_t)form > sizeof(aarch64_linux_tlbi_names) /
            sizeof(aarch64_linux_tlbi_names[0])
        ? NULL : aarch64_linux_tlbi_names[form - 1U];
}
static bool aarch64_linux_tlbi_source(
    const MinicC0Program *program,const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const unsigned int form=src?aarch64_linux_tlbi_form(
        src->template_text,src->template_length):0U;
    const bool has_address=form>=1U&&
        form<=AARCH64_LINUX_TLBI_ADDR_FORM_COUNT;
    const MinicExpression *arg;
    if(!program||!src||!contract||!form||src->is_goto||
       src->output_count!=0U||src->input_count!=(has_address?1U:0U)||
       src->label_count||src->clobber_count||src->register_clobber_count||
       src->has_memory_clobber||
       (has_address&&(!src->inputs||!constraint(&src->inputs[0],"rZ"))))
        return false;
    if(has_address){
        arg=minic_c0_program_expression(program,src->inputs[0].expression);
        if(!arg||!minic_type_is_integer(arg->type)||
           minic_type_is_int128_integer(arg->type))return false;
        contract->operand_count=1U;
        contract->kinds[0]=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    }else contract->operand_count=0U;
    /* Original GNU asm without output is implicitly volatile even if not
     * spelled volatile. Hardware TLB invalidation must not be DCE'd. */
    contract->implicit_memory_effect=true;
    return true;
}
static bool aarch64_linux_tlbi_emit(
    FILE *out,const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins,const MinicCoreInlineAsm *info) {
    const unsigned int form=aarch64_linux_tlbi_form(
        info->template_text,info->template_length);
    const MinicCoreFunction *fn=ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *b=
        ins->value.structured_inline_asm.operands;
    const char *mnemonic=aarch64_linux_tlbi_mnemonic(form);
    if(!out||!ctx||!fn||!info||!mnemonic||ctx->dynamic_frame||
       info->is_goto||!info->is_volatile||
       !info->has_memory_clobber||info->register_clobber_count||
       ins->value.structured_inline_asm.operand_count!=
           (form<=AARCH64_LINUX_TLBI_ADDR_FORM_COUNT?1U:0U))
        return false;
    if(form>AARCH64_LINUX_TLBI_ADDR_FORM_COUNT)
        return fprintf(out,"  .arch armv8.5-a\n  tlbi %s\n",mnemonic)>=0;
    if(!b||b[0].operand_index!=0U||b[0].value>=fn->value_count||
       !aarch64_linux_structured_spill_slot_valid(b[0].value)||b[0].early_clobber||
       b[0].has_fixed_register_binding||
       b[0].kind!=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT||
       !minic_type_is_integer(fn->values[b[0].value].type))return false;
    return fprintf(out,
            "  ldr x9, [sp, #%zu]\n"
            "  .arch armv8.5-a\n"
            "  tlbi %s, x9\n",
            (size_t)b[0].value*8U,mnemonic)>=0;
}

/* Original Linux arch/arm64/kvm/hyp/include/nvhe/spinlock.h uses a ticket
 * lock with a feature-33 LSE replacement.  The output value of all three
 * temporary registers is observable to C, and the owner load has acquire
 * ordering.  The .altinstructions PREL32 relocation/length descriptors
 * must survive together with the LL/SC and LSE instruction sequences. */
typedef enum Aarch64LinuxHypSpinForm {
    AARCH64_LINUX_HYP_SPIN_NONE,
    AARCH64_LINUX_HYP_SPIN_LOCK,
    AARCH64_LINUX_HYP_SPIN_UNLOCK
} Aarch64LinuxHypSpinForm;
static Aarch64LinuxHypSpinForm aarch64_linux_hyp_spin_form(
    const char *text, size_t length) {
    static const char lock_old[] =
        "prfmpstl1strm,%31:ldaxr%w0,%3add%w1,%w0,#(1<<16)"
        "stxr%w2,%w1,%3cbnz%w2,1b";
    static const char lock_new[] =
        ".arch_extensionlsemov%w2,#(1<<16)ldadda%w2,%w0,%3"
        ".rept3nop.endr";
    static const char lock_tail[] =
        "eor%w1,%w0,%w0,ror#16cbz%w1,3fsevl2:wfe"
        "ldaxrh%w2,%4eor%w1,%w2,%w0,lsr#16cbnz%w1,2b3:";
    static const char unlock_old[] =
        "ldrh%w1,%0add%w1,%w1,#1stlrh%w1,%0";
    static const char unlock_new[] =
        ".arch_extensionlsemov%w1,#1staddlh%w1,%0.rept1nop.endr";
    static const char boundary[] = ".previous.endif";
    char compact[1900], old_body[320], new_body[320];
    const char *end;
    size_t i, n = 0U, alt_len;
    if (!text || !length || length >= sizeof(compact)) return AARCH64_LINUX_HYP_SPIN_NONE;
    for (i = 0U; i < length; ++i) {
        const char ch = text[i];
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') continue;
        if (ch == '\0' || ch == ';' || n + 1U >= sizeof(compact))
            return AARCH64_LINUX_HYP_SPIN_NONE;
        compact[n++] = ch;
    }
    compact[n] = '\0';
    end = strstr(compact, boundary);
    if (!end) return AARCH64_LINUX_HYP_SPIN_NONE;
    alt_len = (size_t)(end - compact) + sizeof(boundary) - 1U;
    if (!aarch64_linux_alt_partition(compact, alt_len, 33U,
            old_body, sizeof(old_body), new_body, sizeof(new_body)))
        return AARCH64_LINUX_HYP_SPIN_NONE;
    if (strcmp(old_body, lock_old) == 0 &&
        strcmp(new_body, lock_new) == 0 &&
        strcmp(end + sizeof(boundary) - 1U, lock_tail) == 0)
        return AARCH64_LINUX_HYP_SPIN_LOCK;
    if (strcmp(old_body, unlock_old) == 0 &&
        strcmp(new_body, unlock_new) == 0 &&
        end[sizeof(boundary) - 1U] == '\0')
        return AARCH64_LINUX_HYP_SPIN_UNLOCK;
    return AARCH64_LINUX_HYP_SPIN_NONE;
}

static bool aarch64_linux_hyp_spin_type(
    const MinicC0Program *program, MinicType type,
    size_t width, bool record) {
    size_t size, alignment;
    MinicType plain;
    return program && minic_type_unqualified(type, &plain) &&
           (record ? minic_type_is_record(plain) :
                     minic_type_is_unsigned_integer(plain)) &&
           minic_data_layout_type(minic_default_data_layout(), program,
                                  plain, &size, &alignment) && size == width;
}

static bool aarch64_linux_hyp_spin_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const Aarch64LinuxHypSpinForm form = src ? aarch64_linux_hyp_spin_form(
        src->template_text, src->template_length) : AARCH64_LINUX_HYP_SPIN_NONE;
    const bool lock = form == AARCH64_LINUX_HYP_SPIN_LOCK;
    size_t i, count;
    if (!program || !src || !contract || form == AARCH64_LINUX_HYP_SPIN_NONE ||
        !src->is_volatile || src->is_goto || src->label_count ||
        src->clobber_count != 1U || !src->has_memory_clobber ||
        src->register_clobber_count || !src->outputs ||
        src->output_count != (lock ? 4U : 2U) ||
        src->input_count != (lock ? 1U : 0U) ||
        (lock && !src->inputs)) return false;
    count = src->output_count + src->input_count;
    for (i = 0U; i < count; ++i) {
        const bool output = i < src->output_count;
        const MinicInlineAsmOperand *operand = output ? &src->outputs[i] :
            &src->inputs[i - src->output_count];
        const MinicExpression *expr = minic_c0_program_expression(
            program, operand->expression);
        const char *expected = lock ?
            (i < 3U ? "=&r" : i == 3U ? "+Q" : "Q") :
            (i == 0U ? "=Q" : "=&r");
        const size_t width = lock ? (i == 4U ? 2U : 4U) :
            (i == 0U ? 2U : 8U);
        const bool record = lock && (i == 0U || i == 1U || i == 3U);
        if (!constraint(operand, expected) || !expr ||
            expr->value_category != MINIC_VALUE_LVALUE ||
            (output && minic_type_is_const(expr->type)) ||
            !aarch64_linux_hyp_spin_type(program, expr->type,
                                         width, record)) return false;
        /* GNU can return a 32-bit union through one GPR; Core's ordinary
         * register-output kind assumes scalar pointees.  Transport this
         * source-verified union via an addressable output, then this target
         * alone assigns w9/w10 and stores their full bit patterns. */
        contract->kinds[i] = lock ?
            (i < 2U ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT :
             i == 2U ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
             i == 3U ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE :
                       MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT) :
            (i == 0U ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT :
                       MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT);
    }
    contract->operand_count = count;
    contract->implicit_memory_effect = true;
    return true;
}

static bool aarch64_linux_hyp_spin_emit(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const Aarch64LinuxHypSpinForm form = aarch64_linux_hyp_spin_form(
        info->template_text, info->template_length);
    const MinicCoreFunction *fn = ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *ops =
        ins->value.structured_inline_asm.operands;
    const bool lock = form == AARCH64_LINUX_HYP_SPIN_LOCK;
    size_t i, count;
    if (!out || !fn || !info || !ops ||
        form == AARCH64_LINUX_HYP_SPIN_NONE || ctx->dynamic_frame ||
        !info->is_volatile || info->is_goto ||
        !info->has_memory_clobber || info->register_clobber_count ||
        ins->value.structured_inline_asm.operand_count != (lock ? 5U : 2U))
        return false;
    count = ins->value.structured_inline_asm.operand_count;
    for (i = 0U; i < count; ++i) {
        MinicType pointee;
        size_t size, alignment;
        const MinicCoreStructuredInlineAsmOperandKind expected = lock ?
            (i < 2U ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT :
             i == 2U ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
             i == 3U ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE :
                       MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT) :
            (i == 0U ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT :
                       MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT);
        const size_t width = lock ? (i == 4U ? 2U : 4U) :
                                     (i == 0U ? 2U : 8U);
        const bool record = lock && (i == 0U || i == 1U || i == 3U);
        if (ops[i].kind != expected || ops[i].operand_index != i ||
            ops[i].has_fixed_register_binding ||
            ops[i].early_clobber ||
            ops[i].value >= fn->value_count ||
            !aarch64_linux_structured_spill_slot_valid(ops[i].value) ||
            !minic_type_pointee(fn->values[ops[i].value].type, &pointee) ||
            (record ? !minic_type_is_record(pointee) :
                      !minic_type_is_unsigned_integer(pointee)) ||
            !ctx->target || !ctx->target->data_layout || !ctx->program ||
            !minic_data_layout_type(ctx->target->data_layout, ctx->program,
                                    pointee, &size, &alignment) ||
            size != width) return false;
    }
    if (lock) {
        if (fprintf(out,
            "  ldr x13, [sp, #%zu]\n  ldr x14, [sp, #%zu]\n"
            "661:\n  prfm pstl1strm, [x13]\n"
            "1: ldaxr w9, [x13]\n  add w10, w9, #65536\n"
            "  stxr w11, w10, [x13]\n  cbnz w11, 1b\n",
            (size_t)ops[3].value * 8U, (size_t)ops[4].value * 8U) < 0 ||
            !aarch64_linux_alt_emit_middle(out, 33U, true) ||
            fprintf(out,
            "  mov w11, #65536\n  ldadda w11, w9, [x13]\n"
            "  .rept 3\n  nop\n  .endr\n") < 0 ||
            !aarch64_linux_alt_emit_end(out)) return false;
        return fprintf(out,
            "  eor w10, w9, w9, ror #16\n  cbz w10, 3f\n"
            "  sevl\n2: wfe\n  ldaxrh w11, [x14]\n"
            "  eor w10, w11, w9, lsr #16\n  cbnz w10, 2b\n3:\n"
            "  ldr x15, [sp, #%zu]\n  str w9, [x15]\n"
            "  ldr x15, [sp, #%zu]\n  str w10, [x15]\n"
            "  ldr x15, [sp, #%zu]\n  str w11, [x15]\n",
            (size_t)ops[0].value * 8U,
            (size_t)ops[1].value * 8U,
            (size_t)ops[2].value * 8U) >= 0;
    }
    if (fprintf(out,
        "  ldr x13, [sp, #%zu]\n661:\n"
        "  ldrh w9, [x13]\n  add w9, w9, #1\n"
        "  stlrh w9, [x13]\n",
        (size_t)ops[0].value * 8U) < 0 ||
        !aarch64_linux_alt_emit_middle(out, 33U, true) ||
        fprintf(out, "  mov w9, #1\n  staddlh w9, [x13]\n  nop\n") < 0 ||
        !aarch64_linux_alt_emit_end(out)) return false;
    return fprintf(out,
        "  ldr x14, [sp, #%zu]\n  str x9, [x14]\n",
        (size_t)ops[1].value * 8U) >= 0;
}

/* Linux __cmpwait_case_{8,16,32,64}: typed three-operand GNU ASM
 * uses exclusive loads of the actual memory width, not an ordinary load. */
static unsigned int aarch64_linux_cmpwait_form(const char *s, size_t length) {
    static const struct {
        const char *named; const char *numeric; unsigned int width;
    } forms[] = {
        {"sevlwfeldxrb%w[tmp],%[v]eor%w[tmp],%w[tmp],%w[val]cbnz%w[tmp],1fwfe1:",
         "sevlwfeldxrb%w0,%1eor%w0,%w0,%w2cbnz%w0,1fwfe1:",1U},
        {"sevlwfeldxrh%w[tmp],%[v]eor%w[tmp],%w[tmp],%w[val]cbnz%w[tmp],1fwfe1:",
         "sevlwfeldxrh%w0,%1eor%w0,%w0,%w2cbnz%w0,1fwfe1:",2U},
        {"sevlwfeldxr%w[tmp],%[v]eor%w[tmp],%w[tmp],%w[val]cbnz%w[tmp],1fwfe1:",
         "sevlwfeldxr%w0,%1eor%w0,%w0,%w2cbnz%w0,1fwfe1:",4U},
        {"sevlwfeldxr%[tmp],%[v]eor%[tmp],%[tmp],%[val]cbnz%[tmp],1fwfe1:",
         "sevlwfeldxr%0,%1eor%0,%0,%2cbnz%0,1fwfe1:",8U}
    };
    char compact[160];
    size_t i,n=0U;
    if (!s || !length || length>=sizeof(compact)) return 0U;
    for (i=0U;i<length;++i) {
        const char c=s[i];
        if (c==' '||c=='\t'||c=='\r'||c=='\n') continue;
        if (c=='\0'||n+1U>=sizeof(compact))return 0U;
        compact[n++]=c;
    }
    compact[n]='\0';
    for(i=0U;i<sizeof(forms)/sizeof(forms[0]);++i)
        if(strcmp(compact,forms[i].named)==0 ||
           strcmp(compact,forms[i].numeric)==0) return forms[i].width;
    return 0U;
}

/* Linux 6.6.143 arm64 word-at-a-time.h, load_unaligned_zeropad(). The
 * instruction's faulting PC and recovery PC MUST remain in __ex_table; the
 * 4-byte fixup plus the two exact register indices tell the arm64 kernel
 * fixup handler how to reconstruct the zero-padded result. Replacing the
 * whole template by `ldr` silently breaks page-boundary reads in the kernel.
 *
 * Normalize only insignificant ASCII whitespace. Everything else, including
 * .irp/.equ definitions and the .short register expression, is exact and
 * target-owned. Unknown variants fail closed; no generic inline-asm escape. */
static bool aarch64_linux_zeropad_form(const char *s, size_t length) {
    static const char expected[] =
        "1:ldr%0,%22:"
        ".irpnum,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30"
        ".equ.L__gpr_num_x\\num,\\num"
        ".equ.L__gpr_num_w\\num,\\num"
        ".endr"
        ".equ.L__gpr_num_xzr,31"
        ".equ.L__gpr_num_wzr,31"
        ".pushsection__ex_table,\"a\""
        ".align2"
        ".long((1b)-.)"
        ".long((2b)-.)"
        ".short(4)"
        ".short((((.L__gpr_num_%0)<<0)|((.L__gpr_num_%1)<<5)))"
        ".popsection";
    char compact[1100];
    size_t i, n = 0U;
    if (!s || !length || length >= sizeof(compact)) return false;
    for (i = 0U; i < length; ++i) {
        const char ch = s[i];
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') continue;
        if (ch == '\0' || n + 1U >= sizeof(compact)) return false;
        compact[n++] = ch;
    }
    compact[n] = '\0';
    return strcmp(compact, expected) == 0;
}

static bool aarch64_linux_zeropad_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *result, *base, *memory;
    MinicType result_type, memory_type;
    if (!program || !src || !contract || src->is_volatile || src->is_goto ||
        src->output_count != 1U || src->input_count != 2U ||
        !src->outputs || !src->inputs || src->clobber_count != 0U ||
        src->register_clobber_count != 0U || src->has_memory_clobber ||
        src->label_count != 0U ||
        !constraint(&src->outputs[0], "=&r") ||
        !constraint(&src->inputs[0], "r") ||
        !constraint(&src->inputs[1], "Q") ||
        !aarch64_linux_zeropad_form(
            src->template_text, src->template_length)) return false;
    result = minic_c0_program_expression(
        program, src->outputs[0].expression);
    base = minic_c0_program_expression(
        program, src->inputs[0].expression);
    memory = minic_c0_program_expression(
        program, src->inputs[1].expression);
    if (!result || !base || !memory ||
        result->value_category != MINIC_VALUE_LVALUE ||
        memory->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(result->type) ||
        !minic_type_unqualified(result->type, &result_type) ||
        !minic_type_unqualified(memory->type, &memory_type) ||
        !minic_type_equal(result_type, minic_type_unsigned_long()) ||
        !minic_type_equal(memory_type, minic_type_unsigned_long()) ||
        !minic_type_is_pointer(base->type)) return false;
    contract->operand_count = 3U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
    /* A faulting load is a hardware memory read even without an explicit
     * GNU "memory" clobber. Keep ordering conservative in Core. */
    contract->implicit_memory_effect = true;
    return true;
}

static bool aarch64_linux_zeropad_emit(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreFunction *fn = ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *ops =
        ins->value.structured_inline_asm.operands;
    MinicType result, memory;
    size_t i;
    if (!info || !ops || ctx->dynamic_frame || !info->is_volatile ||
        info->is_goto || info->register_clobber_count != 0U ||
        !aarch64_linux_zeropad_form(
            info->template_text, info->template_length) ||
        ins->value.structured_inline_asm.operand_count != 3U) return false;
    for (i = 0U; i < 3U; ++i)
        if (ops[i].operand_index != i || ops[i].value >= fn->value_count ||
            ops[i].has_fixed_register_binding ||
            !aarch64_linux_structured_spill_slot_valid(ops[i].value) ||
            (i != 0U && ops[i].early_clobber) ||
            ops[i].kind != (i == 0U
                ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT
                : i == 1U ? MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT
                           : MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT))
            return false;
    /* Target-qualified Core contracts carry typed operand roles, while
     * the source-side =&r early-clobber is already checked above. */
    if (!minic_type_pointee(fn->values[ops[0].value].type, &result) ||
        !minic_type_pointee(fn->values[ops[2].value].type, &memory) ||
        !minic_type_equal(result, minic_type_unsigned_long()) ||
        !minic_type_equal(memory, minic_type_unsigned_long()) ||
        !minic_type_is_pointer(fn->values[ops[1].value].type)) return false;
    /* Operand %0 -> x9, %1 -> x10, %2 -> [x11]. The kernel exception
     * fixup's .short indexes remain EXACTLY (9,10); they are not a guessed
     * GNU allocator binding. Section-relative .long offsets are evaluated
     * by the independent target assembler at the actual instruction PC. */
    return fprintf(out,
        "  ldr x10, [sp, #%zu]\n"
        "  ldr x11, [sp, #%zu]\n"
        "1: ldr x9, [x11]\n"
        "2:\n"
        "  .pushsection __ex_table, \"a\"\n"
        "  .align 2\n"
        "  .long (1b - .)\n"
        "  .long (2b - .)\n"
        "  .short 4\n"
        "  .short ((9 << 0) | (10 << 5))\n"
        "  .popsection\n"
        "  ldr x12, [sp, #%zu]\n"
        "  str x9, [x12]\n",
        (size_t)ops[1].value * 8U, (size_t)ops[2].value * 8U,
        (size_t)ops[0].value * 8U) >= 0;
}

/* Linux 6.6 arm64 __get_user/__put_user: the unprivileged LDTR/STTR
 * instruction and its __ex_table fixup are one indivisible machine effect.
 * This is not an ordinary load/store: on a fault the exception handler
 * writes the error register (and, for a load, the destination register).
 * Validate the complete GNU template including the register-number .irp
 * table before choosing our own physical scratch registers. */
typedef struct Aarch64UaccessForm {
    bool is_load;
    unsigned int width;
    const char *mnemonic;
    unsigned int exception_type;
} Aarch64UaccessForm;

static bool aarch64_linux_uaccess_form(const char *text, size_t length,
                                      Aarch64UaccessForm *result) {
    static const struct {
        const char *prefix;
        bool load;
        unsigned int width;
        const char *mnemonic;
        unsigned int exception_type;
    } forms[] = {
        {"1:ldtrb%w1,[%2]2:", true, 1U, "ldtrb w10", 2U},
        {"1:ldtrh%w1,[%2]2:", true, 2U, "ldtrh w10", 2U},
        {"1:ldtr%w1,[%2]2:", true, 4U, "ldtr w10", 2U},
        {"1:ldtr%x1,[%2]2:", true, 8U, "ldtr x10", 2U},
        {"1:sttrb%w1,[%2]2:", false, 1U, "sttrb w10", 2U},
        {"1:sttrh%w1,[%2]2:", false, 2U, "sttrh w10", 2U},
        {"1:sttr%w1,[%2]2:", false, 4U, "sttr w10", 2U},
        {"1:sttr%x1,[%2]2:", false, 8U, "sttr x10", 2U},
        {"1:ldrb%w1,[%2]2:", true, 1U, "ldrb w10", 3U},
        {"1:ldrh%w1,[%2]2:", true, 2U, "ldrh w10", 3U},
        {"1:ldr%w1,[%2]2:", true, 4U, "ldr w10", 3U},
        {"1:ldr%x1,[%2]2:", true, 8U, "ldr x10", 3U},
        {"1:strb%w1,[%2]2:", false, 1U, "strb w10", 3U},
        {"1:strh%w1,[%2]2:", false, 2U, "strh w10", 3U},
        {"1:str%w1,[%2]2:", false, 4U, "str w10", 3U},
        {"1:str%x1,[%2]2:", false, 8U, "str x10", 3U}
    };
    static const char common[] =
        ".irpnum,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30"
        ".equ.L__gpr_num_x\\num,\\num"
        ".equ.L__gpr_num_w\\num,\\num"
        ".endr"
        ".equ.L__gpr_num_xzr,31"
        ".equ.L__gpr_num_wzr,31"
        ".pushsection__ex_table,\"a\""
        ".align2"
        ".long((1b)-.)"
        ".long((2b)-.)"
        ".short(2)";
    static const char common_kernel[] =
        ".irpnum,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30"
        ".equ.L__gpr_num_x\\num,\\num"
        ".equ.L__gpr_num_w\\num,\\num"
        ".endr"
        ".equ.L__gpr_num_xzr,31"
        ".equ.L__gpr_num_wzr,31"
        ".pushsection__ex_table,\"a\""
        ".align2"
        ".long((1b)-.)"
        ".long((2b)-.)"
        ".short(3)";
    static const char load_tail[] =
        ".short((((.L__gpr_num_%w0)<<0)|((.L__gpr_num_%w1)<<5)))"
        ".popsection";
    static const char store_tail[] =
        ".short((((.L__gpr_num_%w0)<<0)|((.L__gpr_num_wzr)<<5)))"
        ".popsection";
    char compact[1400];
    size_t i, n = 0U;

    if (!text || !length || length >= sizeof(compact) || !result) return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return false;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(forms) / sizeof(forms[0]); ++i) {
        const size_t prefix = strlen(forms[i].prefix);
        const char *middle_text = forms[i].exception_type == 2U ?
            common : common_kernel;
        const size_t middle = strlen(middle_text);
        const char *tail = forms[i].load ? load_tail : store_tail;
        const size_t suffix = strlen(tail);
        if (n != prefix + middle + suffix ||
            memcmp(compact, forms[i].prefix, prefix) != 0 ||
            memcmp(compact + prefix, middle_text, middle) != 0 ||
            memcmp(compact + prefix + middle, tail, suffix) != 0)
            continue;
        result->is_load = forms[i].load;
        result->width = forms[i].width;
        result->mnemonic = forms[i].mnemonic;
        result->exception_type = forms[i].exception_type;
        return true;
    }
    return false;
}

static bool aarch64_linux_uaccess_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    Aarch64UaccessForm form;
    const MinicExpression *err, *data, *pointer;
    MinicType err_type, data_type;

    if (!program || !src || !contract || !src->is_volatile || src->is_goto ||
        src->output_count != (size_t)(2U - (src->input_count == 2U)) ||
        (src->output_count != 1U && src->output_count != 2U) ||
        src->output_count + src->input_count != 3U ||
        !src->outputs || !src->inputs || src->clobber_count != 0U ||
        src->register_clobber_count != 0U || src->has_memory_clobber ||
        src->label_count != 0U ||
        !aarch64_linux_uaccess_form(src->template_text,
                                   src->template_length, &form))
        return false;
    if (src->output_count != (form.is_load ? 2U : 1U) ||
        !constraint(&src->outputs[0], "+r") ||
        !constraint(form.is_load ? &src->outputs[1] : &src->inputs[0],
                    form.is_load ? "=r" : "rZ") ||
        !constraint(&src->inputs[form.is_load ? 0U : 1U], "r"))
        return false;
    err = minic_c0_program_expression(program, src->outputs[0].expression);
    data = minic_c0_program_expression(
        program, form.is_load ? src->outputs[1].expression : src->inputs[0].expression);
    pointer = minic_c0_program_expression(
        program, src->inputs[form.is_load ? 0U : 1U].expression);
    if (!err || !data || !pointer ||
        err->value_category != MINIC_VALUE_LVALUE ||
        (form.is_load && data->value_category != MINIC_VALUE_LVALUE) ||
        minic_type_is_const(err->type) ||
        (form.is_load && minic_type_is_const(data->type)) ||
        !minic_type_unqualified(err->type, &err_type) ||
        !minic_type_unqualified(data->type, &data_type) ||
        !minic_type_equal(err_type, minic_type_int()) ||
        (!minic_type_is_integer(data_type) &&
         !(form.width == 8U && minic_type_is_pointer(data_type))) ||
        !minic_type_is_pointer(pointer->type))
        return false;
    contract->operand_count = 3U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
    contract->kinds[1] = form.is_load
        ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT
        : MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

static bool aarch64_linux_uaccess_emit(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info,
    const Aarch64UaccessForm *form) {
    const MinicCoreFunction *fn = ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *op =
        ins->value.structured_inline_asm.operands;
    MinicType err_type, data_type;
    size_t i;

    if (!out || !fn || !form || !info || !info->is_volatile || info->is_goto ||
        ctx->dynamic_frame || info->register_clobber_count != 0U ||
        ins->value.structured_inline_asm.operand_count != 3U ||
        !aarch64_linux_uaccess_form(info->template_text,
                                   info->template_length, &(Aarch64UaccessForm){0}))
        return false;
    for (i = 0U; i < 3U; ++i) {
        const MinicCoreStructuredInlineAsmOperandKind kind = i == 0U
            ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE
            : i == 1U && form->is_load
                ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT
                : MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        if (op[i].operand_index != i || op[i].kind != kind ||
            op[i].value >= fn->value_count || op[i].has_fixed_register_binding ||
            !aarch64_linux_structured_spill_slot_valid(op[i].value)) return false;
    }
    if (!minic_type_pointee(fn->values[op[0].value].type, &err_type) ||
        !minic_type_equal(err_type, minic_type_int()) ||
        (form->is_load &&
         (!minic_type_pointee(fn->values[op[1].value].type, &data_type) ||
          !minic_type_is_integer(data_type))) ||
        !minic_type_is_pointer(fn->values[op[2].value].type)) return false;

    /* x9 is the original +r error, x10 the transferred value, x11 the
     * user pointer, x12 the lvalue address. The fault fixup needs the
     * physical register indices, so these must agree with the instructions.
     * DirectLP64 owns the scratch GPRs and their spill boundaries. */
    if (fprintf(out,
            "  ldr x12, [sp, #%zu]\n"
            "  ldr w9, [x12]\n"
            "  ldr x11, [sp, #%zu]\n",
            (size_t)op[0].value * 8U,
            (size_t)op[2].value * 8U) < 0) return false;
    if (!form->is_load &&
        fprintf(out, "  ldr x10, [sp, #%zu]\n",
                (size_t)op[1].value * 8U) < 0) return false;
    if (fprintf(out,
            "1: %s, [x11]\n"
            "2:\n"
            "  .pushsection __ex_table, \"a\"\n"
            "  .align 2\n"
            "  .long (1b - .)\n"
            "  .long (2b - .)\n"
            "  .short %u\n"
            "  .short ((9 << 0) | (%u << 5))\n"
            "  .popsection\n"
            "  ldr x12, [sp, #%zu]\n"
            "  str w9, [x12]\n",
            form->mnemonic, form->exception_type,
            form->is_load ? 10U : 31U,
            (size_t)op[0].value * 8U) < 0) return false;
    if (form->is_load &&
        fprintf(out,
                "  ldr x12, [sp, #%zu]\n"
                "  str x10, [x12]\n",
                (size_t)op[1].value * 8U) < 0) return false;
    return true;
}

/* Linux arm_smccc_{smc,hvc} wrappers pass an x0/x1 argument vector and
 * receive x0..x3 results. GCC's local asm("rN") is a *binding of each GNU
 * operand*, not a C-level hard-register reservation. Validate both the
 * frontend bindings and the imported Core bindings before touching the
 * exception-level interface; unknown SMC/HVC variants remain unsupported. */
static unsigned int aarch64_linux_smccc_form(const char *s, size_t n) {
    if (s == NULL || n == 0U) return 0U;
    if (n == sizeof("hvc\t#0\n") - 1U &&
        memcmp(s, "hvc\t#0\n", n) == 0) return 1U;
    if (n == sizeof("smc\t#0\n") - 1U &&
        memcmp(s, "smc\t#0\n", n) == 0) return 2U;
    if (n == sizeof("hvc #0\n") - 1U &&
        memcmp(s, "hvc #0\n", n) == 0) return 1U;
    if (n == sizeof("smc #0\n") - 1U &&
        memcmp(s, "smc #0\n", n) == 0) return 2U;
    return 0U;
}

static bool aarch64_linux_fixed_gpr_local_binding(
    const MinicC0Program *program, const MinicExpression *e,
    unsigned int register_number, bool allow_pointer) {
    const MinicFixedRegisterBinding *binding;
    const MinicLocal *local;
    if (!program || !e || e->kind != MINIC_EXPRESSION_LOCAL ||
        e->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(e->type) || minic_type_is_volatile(e->type))
        return false;
    local = minic_c0_program_local(program, e->value.local_id);
    binding = minic_c0_program_local_fixed_register_binding(
        program, e->value.local_id);
    if (!local || !binding || local->is_array ||
        !minic_type_equal(local->type, e->type) ||
        !minic_type_equal(binding->type, e->type) ||
        binding->register_name_length != 2U ||
        (binding->register_name[0] != 'r' &&
         binding->register_name[0] != 'x') ||
        binding->register_name[1] != (char)('0' + register_number))
        return false;
    return (allow_pointer && minic_type_is_pointer(e->type)) ||
           (minic_type_is_integer(e->type) &&
           (minic_type_equal(e->type, minic_type_unsigned_long()) ||
            minic_type_equal(e->type, minic_type_long()) ||
            minic_type_equal(e->type, minic_type_unsigned_int()) ||
            minic_type_equal(e->type, minic_type_int())));
}

/* Linux 6.6 arch/arm64/include/asm/compiler.h:
 *
 *   register unsigned long ptr asm("x30") = value;
 *   asm(ARM64_ASM_PREAMBLE "hint #7\n" : "+r" (ptr));
 *
 * `hint #7` is XPACLRI: it operates on the physical *link register*, not
 * on a freely allocatable operand.  Treating the tied +r output as an
 * ordinary SSA operand silently strips PAC from the wrong register.  The
 * only local x30 binding admitted here must therefore be a complete, exact
 * instruction/operand contract.  GNU may prepend an .arch directive, but
 * the HINT encoding itself is available in the base A64 ISA. */
static bool aarch64_linux_xpaclri_form(const char *text, size_t length) {
    char compact[96];
    size_t i, n = 0U;
    const char *suffix;
    if (!text || !length || length >= sizeof(compact)) return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || c == ';' || n + 1U >= sizeof(compact)) return false;
        compact[n++] = c;
    }
    compact[n] = '\0';
    if (strcmp(compact, "hint#7") == 0) return true;
    /* These are the GNU ARM64_ASM_ARCH spelling variants for Linux arm64;
     * no arbitrary preamble, instruction, or assembler directive is accepted. */
    if (strncmp(compact, ".archarmv8", sizeof(".archarmv8") - 1U) != 0)
        return false;
    suffix = compact + sizeof(".archarmv8") - 1U;
    return strcmp(suffix, "-ahint#7") == 0 ||
           strcmp(suffix, ".1-ahint#7") == 0 ||
           strcmp(suffix, ".2-ahint#7") == 0 ||
           strcmp(suffix, ".3-ahint#7") == 0 ||
           strcmp(suffix, ".4-ahint#7") == 0 ||
           strcmp(suffix, ".5-ahint#7") == 0 ||
           strcmp(suffix, ".6-ahint#7") == 0 ||
           strcmp(suffix, ".7-ahint#7") == 0 ||
           strcmp(suffix, ".8-ahint#7") == 0 ||
           strcmp(suffix, ".9-ahint#7") == 0;
}

static bool aarch64_linux_xpaclri_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const MinicInlineAsmOperand *op;
    const MinicExpression *expr;
    const MinicFixedRegisterBinding *fixed;
    const MinicLocal *local;
    if (!program || !src || !contract || src->is_goto ||
        !aarch64_linux_xpaclri_form(src->template_text, src->template_length) ||
        src->output_count != 1U || !src->outputs || src->input_count != 0U ||
        src->label_count != 0U || src->clobber_count != 0U ||
        src->register_clobber_count != 0U || src->has_memory_clobber)
        return false;
    op = &src->outputs[0];
    if (!constraint(op, "+r") || op->access != MINIC_INLINE_ASM_OPERAND_READ_WRITE)
        return false;
    expr = minic_c0_program_expression(program, op->expression);
    if (!expr || expr->kind != MINIC_EXPRESSION_LOCAL ||
        expr->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_equal(expr->type, minic_type_unsigned_long()) ||
        minic_type_is_const(expr->type) || minic_type_is_volatile(expr->type))
        return false;
    local = minic_c0_program_local(program, expr->value.local_id);
    fixed = minic_c0_program_local_fixed_register_binding(
        program, expr->value.local_id);
    if (!local || !fixed || local->is_array ||
        !minic_type_equal(local->type, expr->type) ||
        !minic_type_equal(fixed->type, expr->type) ||
        fixed->register_name_length != 3U ||
        memcmp(fixed->register_name, "x30", 3U) != 0)
        return false;
    contract->operand_count = 1U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
    return true;
}

static bool aarch64_linux_xpaclri_emit(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreFunction *fn = ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *op =
        ins->value.structured_inline_asm.operands;
    const MinicCoreFixedRegisterBinding *fixed;
    MinicType pointee;
    if (!out || !fn || !op || !info || ctx->dynamic_frame || info->is_goto ||
        !aarch64_linux_xpaclri_form(info->template_text, info->template_length) ||
        ins->value.structured_inline_asm.operand_count != 1U ||
        info->register_clobber_count != 0U || info->has_memory_clobber ||
        op[0].operand_index != 0U || op[0].early_clobber ||
        op[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE ||
        !op[0].has_fixed_register_binding ||
        op[0].fixed_register_binding_id >= fn->fixed_register_binding_count ||
        op[0].value >= fn->value_count ||
        !aarch64_linux_structured_spill_slot_valid(op[0].value) ||
        !minic_type_pointee(fn->values[op[0].value].type, &pointee) ||
        !minic_type_equal(pointee, minic_type_unsigned_long()))
        return false;
    fixed = &fn->fixed_register_bindings[op[0].fixed_register_binding_id];
    if (!fixed->is_local || !fixed->register_name ||
        fixed->register_name_length != 3U ||
        memcmp(fixed->register_name, "x30", 3U) != 0 ||
        !minic_type_equal(fixed->type, minic_type_unsigned_long()))
        return false;
    /* The entry LR is saved by AArch64's stp x29, x30 frame prologue and
     * restored by the epilogue. No BL occurs while x30 contains the PAC
     * operand. Emit one contiguous read-modify-write of physical x30. */
    return fprintf(out,
        "  ldr x9, [sp, #%zu]\n"
        "  ldr x30, [x9]\n"
        "  hint #7\n"
        "  str x30, [x9]\n",
        (size_t)op[0].value * 8U) >= 0;
}

static bool aarch64_linux_smccc_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    size_t i;
    if (!program || !src || !contract ||
        !aarch64_linux_smccc_form(src->template_text, src->template_length) ||
        !src->is_volatile || src->is_goto ||
        src->output_count != 4U ||
        (src->input_count != 1U && src->input_count != 2U) ||
        !src->outputs || !src->inputs || src->label_count != 0U ||
        !src->has_memory_clobber || src->clobber_count != 1U ||
        src->register_clobber_count != 0U) return false;
    for (i = 0U; i < src->output_count; ++i) {
        const MinicInlineAsmOperand *op = &src->outputs[i];
        const MinicExpression *e = minic_c0_program_expression(
            program, op->expression);
        if (!constraint(op, "=r") ||
            !aarch64_linux_fixed_gpr_local_binding(program, e,
                                                  (unsigned int)i, false) ||
            !minic_type_equal(e->type, minic_type_unsigned_long()))
            return false;
        contract->kinds[i] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    }
    for (i = 0U; i < src->input_count; ++i) {
        const MinicInlineAsmOperand *op = &src->inputs[i];
        const MinicExpression *e = minic_c0_program_expression(
            program, op->expression);
        if (!constraint(op, "r") ||
            !aarch64_linux_fixed_gpr_local_binding(program, e,
                                                  (unsigned int)i, false))
            return false;
        contract->kinds[4U + i] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    }
    contract->operand_count = 4U + src->input_count;
    contract->implicit_memory_effect = true;
    return true;
}

static bool aarch64_linux_smccc_emit(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info,
    unsigned int form) {
    const MinicCoreFunction *fn = ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *op =
        ins->value.structured_inline_asm.operands;
    const size_t count = ins->value.structured_inline_asm.operand_count;
    size_t i;
    if (!out || !fn || !op || ctx->dynamic_frame || !info ||
        !info->is_volatile || info->is_goto ||
        info->register_clobber_count != 0U ||
        (form != 1U && form != 2U) || (count != 5U && count != 6U))
        return false;
    for (i = 0U; i < count; ++i) {
        const MinicCoreStructuredInlineAsmOperand *binding = &op[i];
        const MinicCoreFixedRegisterBinding *fixed;
        MinicType type;
        const unsigned int number = (unsigned int)(i < 4U ? i : i - 4U);
        if (binding->operand_index != i || binding->value >= fn->value_count ||
            !binding->has_fixed_register_binding ||
            binding->fixed_register_binding_id >= fn->fixed_register_binding_count ||
            binding->kind != (i < 4U ?
                MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
                MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT) ||
            binding->early_clobber ||
            !aarch64_linux_structured_spill_slot_valid(binding->value))
            return false;
        fixed = &fn->fixed_register_bindings[binding->fixed_register_binding_id];
        if (!fixed->is_local || !fixed->register_name ||
            fixed->register_name_length != 2U ||
            (fixed->register_name[0] != 'r' && fixed->register_name[0] != 'x') ||
            fixed->register_name[1] != (char)('0' + number)) return false;
        type = fn->values[binding->value].type;
        if (i < 4U) {
            if (!minic_type_pointee(type, &type) ||
                !minic_type_equal(type, minic_type_unsigned_long()) ||
                !minic_type_equal(type, fixed->type)) return false;
        } else if (!minic_type_equal(type, fixed->type) ||
                   (!minic_type_is_integer(type) ||
                    (type.integer_rank != MINIC_INTEGER_RANK_INT &&
                     type.integer_rank != MINIC_INTEGER_RANK_LONG))) return false;
    }
    /* DirectLP64 stores each scalar operand in an 8-byte SSA spill slot.
     * Use only x0/x1 for arguments and x0..x3 for result writes; all other
     * live Core values stay in the frame across the privileged transition. */
    for (i = 4U; i < count; ++i)
        if (fprintf(out, "  ldr x%zu, [sp, #%zu]\n",
                    i - 4U, (size_t)op[i].value * 8U) < 0) return false;
    if (fputs(form == 1U ? "  hvc #0\n" : "  smc #0\n", out) == EOF)
        return false;
    for (i = 0U; i < 4U; ++i)
        if (fprintf(out,
                    "  ldr x12, [sp, #%zu]\n"
                    "  str x%zu, [x12]\n",
                    (size_t)op[i].value * 8U, i) < 0) return false;
    return true;
}

/* The vDSO's original fallback syscall has a narrower, independently
 * checked GNU contract than SMCCC: x0/x1 carry the arguments, x8 the Linux
 * syscall number, and x0 the result.  The fixed local register bindings are
 * essential; a free-form `svc` or a registerless opaque barrier is unsafe. */
static bool aarch64_linux_svc_form(const char *s, size_t n) {
    char compact[16];
    size_t i, used = 0U;
    if (!s || n == 0U || n >= sizeof(compact)) return false;
    for (i = 0U; i < n; ++i) {
        const char c = s[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (used + 1U >= sizeof(compact)) return false;
        compact[used++] = c;
    }
    compact[used] = '\0';
    return strcmp(compact, "svc#0") == 0;
}

static bool aarch64_linux_svc_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    static const unsigned int registers[] = {0U, 1U, 8U};
    size_t i;
    const MinicExpression *ret;
    if (!program || !src || !contract ||
        !aarch64_linux_svc_form(src->template_text, src->template_length) ||
        !src->is_volatile || src->is_goto || src->output_count != 1U ||
        src->input_count != 3U || !src->outputs || !src->inputs ||
        src->label_count != 0U || src->clobber_count != 1U ||
        src->register_clobber_count != 0U || !src->has_memory_clobber ||
        !constraint(&src->outputs[0], "=r")) return false;
    ret = minic_c0_program_expression(program, src->outputs[0].expression);
    if (!aarch64_linux_fixed_gpr_local_binding(program, ret, 0U, false) ||
        !minic_type_equal(ret->type, minic_type_long())) return false;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    for (i = 0U; i < 3U; ++i) {
        const MinicExpression *arg = minic_c0_program_expression(
            program, src->inputs[i].expression);
        if (!constraint(&src->inputs[i], "r") ||
            !aarch64_linux_fixed_gpr_local_binding(program, arg,
                                                   registers[i], true) ||
            (i == 2U && !minic_type_equal(arg->type, minic_type_long())))
            return false;
        contract->kinds[i + 1U] =
            MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    }
    contract->operand_count = 4U;
    contract->implicit_memory_effect = true;
    return true;
}

static bool aarch64_linux_svc_emit(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    static const unsigned int registers[] = {0U, 1U, 8U};
    const MinicCoreFunction *fn = ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *ops =
        ins->value.structured_inline_asm.operands;
    size_t i;
    if (!out || !fn || !ops || !info || ctx->dynamic_frame ||
        !aarch64_linux_svc_form(info->template_text, info->template_length) ||
        !info->is_volatile || info->is_goto || !info->has_memory_clobber ||
        info->register_clobber_count != 0U ||
        ins->value.structured_inline_asm.operand_count != 4U)
        return false;
    for (i = 0U; i < 4U; ++i) {
        const MinicCoreStructuredInlineAsmOperand *op = &ops[i];
        const MinicCoreFixedRegisterBinding *fixed;
        const unsigned int reg = i == 0U ? 0U : registers[i - 1U];
        MinicType type;
        if (op->operand_index != i || op->value >= fn->value_count ||
            !op->has_fixed_register_binding ||
            op->fixed_register_binding_id >= fn->fixed_register_binding_count ||
            op->early_clobber ||
            op->kind != (i == 0U ?
                 MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
                 MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT) ||
            !aarch64_linux_structured_spill_slot_valid(op->value)) return false;
        fixed = &fn->fixed_register_bindings[op->fixed_register_binding_id];
        if (!fixed->is_local || !fixed->register_name ||
            fixed->register_name_length != 2U ||
            fixed->register_name[0] != 'x' ||
            fixed->register_name[1] != (char)('0' + reg)) return false;
        type = fn->values[op->value].type;
        if (i == 0U) {
            if (!minic_type_pointee(type, &type) ||
                !minic_type_equal(type, minic_type_long()) ||
                !minic_type_equal(type, fixed->type)) return false;
        } else if (!minic_type_equal(type, fixed->type) ||
                   (!minic_type_is_pointer(type) &&
                    !minic_type_is_integer(type)) ||
                   (i == 3U && !minic_type_equal(type, minic_type_long())))
            return false;
    }
    for (i = 1U; i < 4U; ++i)
        if (fprintf(out, "  ldr x%u, [sp, #%zu]\n", registers[i - 1U],
                    (size_t)ops[i].value * 8U) < 0) return false;
    return fputs("  svc #0\n", out) != EOF &&
           fprintf(out, "  ldr x9, [sp, #%zu]\n  str x0, [x9]\n",
                   (size_t)ops[0].value * 8U) >= 0;
}

/* FEAT_WFxT: original Linux arm64 delay.c uses the generic SYS-register
 * spelling for the two timeout wait instructions, WFIT and WFET.  Preserve
 * the precise GNU ISA instruction instead of synthesizing an ordinary WFI
 * or WFE; the input deadline is a physical 64-bit GPR value. */
static unsigned int aarch64_linux_wfxt_form(const char *text, size_t length) {
    static const char *const forms[] = {
        "msrs0_3_c1_c0_0,%0", "msrs0_3_c1_c0_1,%0"
    };
    char compact[64];
    size_t i,n=0U;
    if (!text || length==0U || length>=sizeof(compact))return 0U;
    for (i=0U;i<length;++i) {
        const char c=text[i];
        if (c==' '||c=='\t'||c=='\r'||c=='\n')continue;
        if (c=='\0'||c==';'||n+1U>=sizeof(compact))return 0U;
        compact[n++]=c;
    }
    compact[n]='\0';
    for(i=0U;i<sizeof(forms)/sizeof(forms[0]);++i)
        if(strcmp(compact,forms[i])==0)return (unsigned int)i+1U;
    return 0U;
}
static bool aarch64_linux_wfxt_source(
    const MinicC0Program *program,const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *value;
    if (!program||!src||!contract||!src->is_volatile||src->is_goto||
        !aarch64_linux_wfxt_form(src->template_text,src->template_length)||
        src->output_count!=0U||src->input_count!=1U||!src->inputs||
        src->label_count||src->clobber_count!=1U||
        !src->has_memory_clobber||src->register_clobber_count||
        !constraint(&src->inputs[0],"r"))return false;
    value=minic_c0_program_expression(program,src->inputs[0].expression);
    if(!value||!aarch64_stlr_type(value->type,8U))return false;
    contract->operand_count=1U;
    contract->kinds[0]=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect=true;
    return true;
}
static bool aarch64_linux_wfxt_emit(
    FILE *out,const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins,const MinicCoreInlineAsm *info,
    unsigned int form) {
    const MinicCoreFunction *fn=ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *op=
        ins->value.structured_inline_asm.operands;
    if(!out||!fn||!info||!op||ctx->dynamic_frame||
       (form!=1U&&form!=2U)||!info->is_volatile||info->is_goto||
       !info->has_memory_clobber||info->register_clobber_count||
       ins->value.structured_inline_asm.operand_count!=1U||
       op[0].operand_index!=0U||
       op[0].kind!=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT||
       op[0].value>=fn->value_count||op[0].early_clobber||
       op[0].has_fixed_register_binding||
       !aarch64_linux_structured_spill_slot_valid(op[0].value)||
       !aarch64_stlr_type(fn->values[op[0].value].type,8U))return false;
    return fprintf(out,"  ldr x9, [sp, #%zu]\n"
                       "  msr s0_3_c1_c0_%u, x9\n",
                   (size_t)op[0].value*8U,form-1U)>=0;
}

/* Linux atomic64_dec_if_positive has no input operand: its +Q memory is
 * the sole source value and destination. A negative result is returned
 * without an exclusive store; successful updates use release STXR and
 * a full DMB ISH, retaining the source's signed-flag branch. */
static bool aarch64_linux_atomic_dec_positive_form(
    const char *text, size_t length) {
    static const char expected[] =
        "//atomic64_dec_if_positive"
        "prfmpstl1strm,%2"
        "1:ldxr%0,%2"
        "subs%0,%0,#1"
        "b.lt2f"
        "stlxr%w1,%0,%2"
        "cbnz%w1,1b"
        "dmbish"
        "2:";
    char compact[256];
    size_t i, n = 0U;
    if (!text || !length || length >= sizeof(compact)) return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return false;
        compact[n++] = c;
    }
    compact[n] = '\0';
    return strcmp(compact, expected) == 0;
}

static bool aarch64_linux_atomic_dec_positive_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    size_t i;
    if (!program || !src || !contract || !src->is_volatile || src->is_goto ||
        src->output_count != 3U || !src->outputs || src->input_count != 0U ||
        src->label_count != 0U || src->clobber_count != 2U ||
        !src->has_memory_clobber || src->register_clobber_count != 1U ||
        !src->register_clobbers ||
        src->register_clobbers[0].name_length != 2U ||
        memcmp(src->register_clobbers[0].name, "cc", 2U) != 0 ||
        !constraint(&src->outputs[0], "=&r") ||
        !constraint(&src->outputs[1], "=&r") ||
        !constraint(&src->outputs[2], "+Q") ||
        !aarch64_linux_atomic_dec_positive_form(
            src->template_text, src->template_length)) return false;
    for (i = 0U; i < 3U; ++i) {
        const MinicExpression *e = minic_c0_program_expression(
            program, src->outputs[i].expression);
        if (!e || e->value_category != MINIC_VALUE_LVALUE ||
            minic_type_is_const(e->type) ||
            !aarch64_stlr_type(e->type, 8U)) return false;
    }
    contract->operand_count = 3U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
    return true;
}

static bool aarch64_linux_atomic_dec_positive_emit(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreFunction *fn = ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *ops =
        ins->value.structured_inline_asm.operands;
    size_t i;
    if (!out || !fn || !info || !ops || !info->is_volatile ||
        info->is_goto || !info->has_memory_clobber ||
        info->register_clobber_count != 1U ||
        !info->register_clobbers ||
        info->register_clobbers[0].name_length != 2U ||
        memcmp(info->register_clobbers[0].name, "cc", 2U) != 0 ||
        ins->value.structured_inline_asm.operand_count != 3U ||
        !aarch64_linux_atomic_dec_positive_form(
            info->template_text, info->template_length)) return false;
    for (i = 0U; i < 3U; ++i) {
        MinicType pointee;
        if (ops[i].operand_index != i || ops[i].early_clobber ||
            ops[i].has_fixed_register_binding ||
            ops[i].value >= fn->value_count ||
            !aarch64_linux_structured_spill_slot_valid(ops[i].value) ||
            ops[i].kind != (i == 2U ?
                MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE :
                MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT) ||
            !minic_type_pointee(fn->values[ops[i].value].type, &pointee) ||
            !aarch64_stlr_type(pointee, 8U)) return false;
    }
    return fprintf(out,
        "  ldr x10, [sp, #%zu]\n"
        "  prfm pstl1strm, [x10]\n"
        "1:\n"
        "  ldxr x9, [x10]\n"
        "  subs x9, x9, #1\n"
        "  b.lt 2f\n"
        "  stlxr w12, x9, [x10]\n"
        "  cbnz w12, 1b\n"
        "  dmb ish\n"
        "2:\n"
        "  ldr x13, [sp, #%zu]\n"
        "  str x9, [x13]\n"
        "  ldr x13, [sp, #%zu]\n"
        "  str x12, [x13]\n",
        (size_t)ops[2].value * 8U,
        (size_t)ops[0].value * 8U,
        (size_t)ops[1].value * 8U) >= 0;
}

/* Native AArch64 128-bit LL/SC compare-exchange. Its eight GNU named
 * operands are exactly four outputs (+Q, old low/high, retry status) and
 * four 64-bit scalar inputs (expected/replacement low/high). Preserve the
 * full compare + CCMP short-circuit and the release/full-barrier variant. */
static unsigned int aarch64_linux_cmpxchg128_form(
    const char *text, size_t length) {
    char compact[512], expected[512];
    size_t i, n = 0U;
    unsigned int mode;
    if (!text || !length || length >= sizeof(compact)) return 0U;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return 0U;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (mode = 1U; mode <= 4U; ++mode) {
        const bool core_numbered = mode > 2U;
        const bool release_barrier = (mode & 1U) == 0U;
        const int written = snprintf(expected, sizeof(expected),
            "%sprfmpstl1strm,%s"
            "1:ldxp%s,%s,%s"
            "cmp%s,%s"
            "ccmp%s,%s,0,eq"
            "b.ne2f"
            "%s%s,%s,%s,%s"
            "cbnz%s,1b"
            "%s2:",
            release_barrier ? "//__cmpxchg128_mb" : "//__cmpxchg128",
            core_numbered ? "%0" : "%[v]",
            core_numbered ? "%1" : "%[rl]",
            core_numbered ? "%2" : "%[rh]",
            core_numbered ? "%0" : "%[v]",
            core_numbered ? "%1" : "%[rl]",
            core_numbered ? "%4" : "%[ol]",
            core_numbered ? "%2" : "%[rh]",
            core_numbered ? "%5" : "%[oh]",
            release_barrier ? "stlxp" : "stxp",
            core_numbered ? "%w3" : "%w[tmp]",
            core_numbered ? "%6" : "%[nl]",
            core_numbered ? "%7" : "%[nh]",
            core_numbered ? "%0" : "%[v]",
            core_numbered ? "%w3" : "%w[tmp]",
            release_barrier ? "dmbish" : "");
        if (written > 0 && (size_t)written < sizeof(expected) &&
            strcmp(compact, expected) == 0) return release_barrier ? 2U : 1U;
    }
    return 0U;
}

static bool aarch64_linux_cmpxchg128_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const unsigned int mode = src ? aarch64_linux_cmpxchg128_form(
        src->template_text, src->template_length) : 0U;
    size_t i;
    if (!program || !src || !contract || !mode || !src->is_volatile ||
        src->is_goto || !src->outputs || !src->inputs ||
        src->output_count != 4U || src->input_count != 4U ||
        src->label_count != 0U ||
        src->clobber_count != (mode == 1U ? 1U : 2U) ||
        src->has_memory_clobber != (mode == 2U) ||
        src->register_clobber_count != 1U || !src->register_clobbers ||
        src->register_clobbers[0].name_length != 2U ||
        memcmp(src->register_clobbers[0].name, "cc", 2U) != 0 ||
        !constraint(&src->outputs[0], "+Q")) return false;
    for (i = 0U; i < 8U; ++i) {
        const MinicInlineAsmOperand *operand = i < 4U
            ? &src->outputs[i] : &src->inputs[i - 4U];
        const MinicExpression *e = minic_c0_program_expression(
            program, operand->expression);
        if (i > 0U && i < 4U &&
            !constraint(operand, "=&r")) return false;
        if (i >= 4U && !constraint(operand, "r")) return false;
        if (!e || (i < 4U &&
            (e->value_category != MINIC_VALUE_LVALUE ||
             minic_type_is_const(e->type))) ||
            (i == 0U ? !minic_type_is_int128_integer(e->type)
             : i == 3U ? !aarch64_stlr_type(e->type, 4U)
             : !aarch64_stlr_type(e->type, 8U))) return false;
    }
    contract->operand_count = 8U;
    for (i = 0U; i < 8U; ++i)
        contract->kinds[i] = i == 0U ?
            MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE :
            i < 4U ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
                     MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    return true;
}

static bool aarch64_linux_cmpxchg128_emit(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info,
    unsigned int mode) {
    const MinicCoreFunction *fn = ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *ops =
        ins->value.structured_inline_asm.operands;
    size_t i;
    if (!out || !fn || !info || !ops || mode < 1U || mode > 2U ||
        !info->is_volatile || info->is_goto ||
        /* Core conservatively sets memory for a +Q read/write operand,
         * regardless of the source's explicit "memory" clobber. */
        !info->has_memory_clobber ||
        info->register_clobber_count != 1U ||
        !info->register_clobbers ||
        info->register_clobbers[0].name_length != 2U ||
        memcmp(info->register_clobbers[0].name, "cc", 2U) != 0 ||
        ins->value.structured_inline_asm.operand_count != 8U ||
        aarch64_linux_cmpxchg128_form(
            info->template_text, info->template_length) != mode) return false;
    for (i = 0U; i < 8U; ++i) {
        MinicType pointee;
        MinicType type;
        if (ops[i].operand_index != i || ops[i].early_clobber ||
            ops[i].has_fixed_register_binding ||
            ops[i].value >= fn->value_count ||
            !aarch64_linux_structured_spill_slot_valid(ops[i].value) ||
            ops[i].kind != (i == 0U ?
                MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE :
                i < 4U ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
                         MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT))
            return false;
        type = fn->values[ops[i].value].type;
        if ((i < 4U && !minic_type_pointee(type, &pointee)) ||
            (i == 0U ? !minic_type_is_int128_integer(pointee)
             : i == 3U ? !aarch64_stlr_type(pointee, 4U)
             : i < 4U ? !aarch64_stlr_type(pointee, 8U)
             : !aarch64_stlr_type(type, 8U))) return false;
    }
    return fprintf(out,
        "  ldr x10, [sp, #%zu]\n"
        "  ldr x13, [sp, #%zu]\n"
        "  ldr x14, [sp, #%zu]\n"
        "  ldr x15, [sp, #%zu]\n"
        "  ldr x16, [sp, #%zu]\n"
        "  mov w12, wzr\n"
        "  prfm pstl1strm, [x10]\n"
        "1:\n"
        "  ldxp x9, x11, [x10]\n"
        "  cmp x9, x13\n"
        "  ccmp x11, x14, #0, eq\n"
        "  b.ne 2f\n"
        "  %s w12, x15, x16, [x10]\n"
        "  cbnz w12, 1b\n"
        "%s"
        "2:\n"
        "  ldr x17, [sp, #%zu]\n"
        "  str x9, [x17]\n"
        "  ldr x17, [sp, #%zu]\n"
        "  str x11, [x17]\n"
        "  ldr x17, [sp, #%zu]\n"
        "  str w12, [x17]\n",
        (size_t)ops[0].value * 8U,
        (size_t)ops[4].value * 8U,
        (size_t)ops[5].value * 8U,
        (size_t)ops[6].value * 8U,
        (size_t)ops[7].value * 8U,
        mode == 1U ? "stxp" : "stlxp",
        mode == 1U ? "" : "  dmb ish\n",
        (size_t)ops[1].value * 8U,
        (size_t)ops[2].value * 8U,
        (size_t)ops[3].value * 8U) >= 0;
}

/* Linux arm64 futex LL/SC exception recovery is part of the atomic
 * operation. The fault handler must observe both the failed load's and
 * failed store's PC, with the exact physical error register in the type-2
 * __ex_table entry. Qualify the complete GNU .irp + fixup templates; no
 * generic raw-asm pass-through is permitted. Modes 1..5 are SET, ADD,
 * OR, AND, XOR; mode 6 is compare-exchange with two different fixups. */
static unsigned int aarch64_linux_futex_form(
    const char *text, size_t length) {
    static const char gpr_table[] =
        ".irpnum,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30"
        ".equ.L__gpr_num_x\\num,\\num"
        ".equ.L__gpr_num_w\\num,\\num"
        ".endr"
        ".equ.L__gpr_num_xzr,31"
        ".equ.L__gpr_num_wzr,31";
    static const char *const operations[] = {
        "mov%w3,%w5", "add%w3,%w1,%w5", "orr%w3,%w1,%w5",
        "and%w3,%w1,%w5", "eor%w3,%w1,%w5"
    };
    char compact[3500], expected[3500], fixup[2][700];
    size_t i, n = 0U;
    unsigned int mode;
    if (!text || !length || length >= sizeof(compact)) return 0U;
    for (i = 0U; i < length; ++i) {
        const char ch = text[i];
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') continue;
        if (ch == '\0' || n + 1U >= sizeof(compact)) return 0U;
        compact[n++] = ch;
    }
    compact[n] = '\0';
    for (mode = 1U; mode <= 6U; ++mode) {
        const bool compare = mode == 6U;
        int a, written;
        for (i = 0U; i < 2U; ++i) {
            a = snprintf(fixup[i], sizeof(fixup[i]),
                "%s.pushsection__ex_table,\"a\""
                ".align2"
                ".long((%ub)-.)"
                ".long((%ub)-.)"
                ".short(2)"
                ".short((((.L__gpr_num_%%w0)<<0)|((.L__gpr_num_wzr)<<5)))"
                ".popsection",
                gpr_table, i == 0U ? 1U : 2U, compare ? 4U : 3U);
            if (a <= 0 || (size_t)a >= sizeof(fixup[i])) return 0U;
        }
        if (compare) {
            written = snprintf(expected, sizeof(expected),
                "//futex_atomic_cmpxchg_inatomic"
                "prfmpstl1strm,%%2"
                "1:ldxr%%w1,%%2"
                "sub%%w3,%%w1,%%w5"
                "cbnz%%w3,4f"
                "2:stlxr%%w3,%%w6,%%2"
                "cbz%%w3,3f"
                "sub%%w4,%%w4,%%w3"
                "cbnz%%w4,1b"
                "mov%%w0,%%w7"
                "3:dmbish4:%s%s", fixup[0], fixup[1]);
        } else {
            written = snprintf(expected, sizeof(expected),
                "prfmpstl1strm,%%2"
                "1:ldxr%%w1,%%2"
                "%s"
                "2:stlxr%%w0,%%w3,%%2"
                "cbz%%w0,3f"
                "sub%%w4,%%w4,%%w0"
                "cbnz%%w4,1b"
                "mov%%w0,%%w6"
                "3:dmbish%s%s",
                operations[mode - 1U], fixup[0], fixup[1]);
        }
        if (written > 0 && (size_t)written < sizeof(expected) &&
            strcmp(compact, expected) == 0) return mode;
    }
    return 0U;
}

static bool aarch64_linux_futex_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const unsigned int mode = src ? aarch64_linux_futex_form(
        src->template_text, src->template_length) : 0U;
    const size_t operand_count = mode == 6U ? 8U : 7U;
    size_t i;
    if (!program || !src || !contract || !mode || !src->is_volatile ||
        src->is_goto || !src->outputs || !src->inputs ||
        src->output_count != 5U ||
        src->input_count != operand_count - 5U ||
        src->label_count != 0U || src->clobber_count != 1U ||
        !src->has_memory_clobber || src->register_clobber_count != 0U ||
        !constraint(&src->outputs[0], mode == 6U ? "+r" : "=&r") ||
        !constraint(&src->outputs[1], "=&r") ||
        !constraint(&src->outputs[2], "+Q") ||
        !constraint(&src->outputs[3], "=&r") ||
        !constraint(&src->outputs[4], "+r")) return false;
    for (i = 0U; i < operand_count; ++i) {
        const MinicInlineAsmOperand *operand = i < 5U
            ? &src->outputs[i] : &src->inputs[i - 5U];
        const MinicExpression *e = minic_c0_program_expression(
            program, operand->expression);
        if (i >= 5U && !constraint(operand,
                i == operand_count - 1U ? "Ir" : "r")) return false;
        if (!e || (i < 5U &&
            (e->value_category != MINIC_VALUE_LVALUE ||
             minic_type_is_const(e->type))) ||
            !aarch64_stlr_type(e->type, 4U)) return false;
    }
    contract->operand_count = operand_count;
    for (i = 0U; i < operand_count; ++i)
        contract->kinds[i] = i == 2U ?
            MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE :
            (i == 0U && mode == 6U) || i == 4U ?
            MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE :
            i < 5U ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
                     MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

static bool aarch64_linux_futex_emit(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info,
    unsigned int mode) {
    const MinicCoreFunction *fn = ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *ops =
        ins->value.structured_inline_asm.operands;
    const size_t count = mode == 6U ? 8U : 7U;
    static const char *const alu[] = {
        "mov w12, w14", "add w12, w10, w14", "orr w12, w10, w14",
        "and w12, w10, w14", "eor w12, w10, w14"
    };
    size_t i;
    if (!out || !fn || !ops || !info || mode < 1U || mode > 6U ||
        !info->is_volatile || info->is_goto ||
        !info->has_memory_clobber || info->register_clobber_count != 0U ||
        ins->value.structured_inline_asm.operand_count != count ||
        aarch64_linux_futex_form(info->template_text,
                                 info->template_length) != mode) return false;
    for (i = 0U; i < count; ++i) {
        MinicType pointee;
        const MinicCoreStructuredInlineAsmOperandKind kind = i == 2U ?
            MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE :
            (i == 0U && mode == 6U) || i == 4U ?
            MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE :
            i < 5U ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
                     MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        if (ops[i].operand_index != i || ops[i].kind != kind ||
            ops[i].has_fixed_register_binding ||
            ops[i].value >= fn->value_count ||
            !aarch64_linux_structured_spill_slot_valid(ops[i].value))
            return false;
        if (i < 5U &&
            (!minic_type_pointee(fn->values[ops[i].value].type,
                                 &pointee) ||
             !aarch64_stlr_type(pointee, 4U))) return false;
        if (i >= 5U &&
            !aarch64_stlr_type(fn->values[ops[i].value].type, 4U))
            return false;
    }
    /* w9:error, w10:loaded value, x11:atomic memory, w12:retry status,
     * w13:loop budget, w14:operation/old, w15:new or EAGAIN, w16:EAGAIN.
     * A fault handler will write w9 (reg 9) and resume at 4: or 3:. */
    if (fprintf(out,
            "  ldr x11, [sp, #%zu]\n"
            "  ldr x17, [sp, #%zu]\n"
            "  ldr w13, [x17]\n"
            "  ldr w14, [sp, #%zu]\n"
            "  ldr w15, [sp, #%zu]\n",
            (size_t)ops[2].value * 8U,
            (size_t)ops[4].value * 8U,
            (size_t)ops[5].value * 8U,
            (size_t)ops[6].value * 8U) < 0) return false;
    if (mode == 6U &&
        fprintf(out,"  ldr w16, [sp, #%zu]\n",
                (size_t)ops[7].value * 8U) < 0) return false;
    if (mode == 6U &&
        fprintf(out,
            "  ldr x17, [sp, #%zu]\n"
            "  ldr w9, [x17]\n",
            (size_t)ops[0].value * 8U) < 0) return false;
    if (fprintf(out,
            "  prfm pstl1strm, [x11]\n"
            "1: ldxr w10, [x11]\n") < 0) return false;
    if (mode == 6U) {
        if (fprintf(out,
                "  sub w12, w10, w14\n"
                "  cbnz w12, 4f\n"
                "2: stlxr w12, w15, [x11]\n"
                "  cbz w12, 3f\n"
                "  sub w13, w13, w12\n"
                "  cbnz w13, 1b\n"
                "  mov w9, w16\n"
                "3:\n  dmb ish\n4:\n") < 0) return false;
    } else if (fprintf(out,
            "  %s\n"
            "2: stlxr w9, w12, [x11]\n"
            "  cbz w9, 3f\n"
            "  sub w13, w13, w9\n"
            "  cbnz w13, 1b\n"
            "  mov w9, w15\n"
            "3:\n  dmb ish\n",
            alu[mode - 1U]) < 0) return false;
    for (i = 0U; i < 2U; ++i)
        if (fprintf(out,
                "  .pushsection __ex_table, \"a\"\n"
                "  .align 2\n"
                "  .long (%ub - .)\n"
                "  .long (%ub - .)\n"
                "  .short 2\n"
                "  .short ((9 << 0) | (31 << 5))\n"
                "  .popsection\n",
                i == 0U ? 1U : 2U,
                mode == 6U ? 4U : 3U) < 0) return false;
    for (i = 0U; i < 5U; ++i) {
        const char *const regs[] = {"w9", "w10", "w11", "w12", "w13"};
        if (i == 2U) continue;
        if (fprintf(out,
                "  ldr x17, [sp, #%zu]\n"
                "  str %s, [x17]\n",
                (size_t)ops[i].value * 8U, regs[i]) < 0)
            return false;
    }
    return true;
}

/* With CONFIG_ARM64_PAN disabled the original kernel's
 * __uaccess_{enable,disable}_hw_pan() macros enclose their *entire*
 * alternative sequence in `.if 0 == 1`. GNU AS emits no instruction and
 * no alternative entry, including the inner `.inst` and sections. Accept
 * precisely those two complete templates as zero-sized target effects.
 * A future enabled `.if 1 == 1` must be implemented, never silently elided. */
static unsigned int aarch64_linux_disabled_hw_pan_form(
    const char *text, size_t length) {
    char compact[700], expected[700];
    size_t i, n = 0U;
    unsigned int bit;
    if (!text || !length || length >= sizeof(compact)) return 0U;
    for (i = 0U; i < length; ++i) {
        const char ch = text[i];
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') continue;
        if (ch == '\0' || n + 1U >= sizeof(compact)) return 0U;
        compact[n++] = ch;
    }
    compact[n] = '\0';
    for (bit = 0U; bit <= 1U; ++bit) {
        const int written = snprintf(expected, sizeof(expected),
            ".if0==1"
            "661:nop662:"
            ".pushsection.altinstructions,\"a\""
            ".word661b-."
            ".word663f-."
            ".hword38"
            ".byte662b-661b"
            ".byte664f-663f"
            ".popsection"
            ".subsection1"
            "663:.inst(0xd500401f|((0)<<16|(4)<<5)|((!!(%u))<<8))"
            "664:"
            ".org.-(664b-663b)+(662b-661b)"
            ".org.-(662b-661b)+(664b-663b)"
            ".previous"
            ".endif", bit);
        if (written > 0 && (size_t)written < sizeof(expected) &&
            strcmp(compact, expected) == 0) return bit + 1U;
    }
    return 0U;
}

static bool aarch64_linux_disabled_hw_pan_source(
    const MinicInlineAsm *src, MinicTargetInlineAsmContract *contract) {
    if (!src || !contract || src->is_volatile || src->is_goto ||
        src->output_count || src->input_count || src->label_count ||
        src->clobber_count || src->has_memory_clobber ||
        src->register_clobber_count ||
        !aarch64_linux_disabled_hw_pan_form(
            src->template_text, src->template_length)) return false;
    contract->operand_count = 0U;
    return true;
}

static bool aarch64_linux_disabled_hw_pan_emit(
    FILE *out, const MinicCoreInstruction *ins,
    const MinicCoreInlineAsm *info) {
    if (!out || !ins || !info || info->is_goto ||
        info->has_memory_clobber || info->register_clobber_count ||
        ins->value.structured_inline_asm.operand_count ||
        !aarch64_linux_disabled_hw_pan_form(
            info->template_text, info->template_length)) return false;
    return fprintf(out, "  // original GNU .if 0 == 1 PAN alternative: no machine effect\n") >= 0;
}

/* Linux arch_timer_read_cnt{p,v}ct_el0: feature 19 replaces an ISB+MRS
 * pair with NOP+MRS_S. The replacement reads CNTPOCTSS/CNTVCTSS (op2
 * 5/6), so treating this as an ordinary MRS would silently break the
 * CNTVCTSS clocksource semantics and discard boot-time alternatives.
 * Match the complete GNU .irp/.macro/.inst source and both .org checks. */
static unsigned int aarch64_linux_timer_alternative_form(
    const char *text, size_t length) {
    static const char macro_table[] =
        ".irpnum,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30"
        ".equ.L__gpr_num_x\\num,\\num"
        ".equ.L__gpr_num_w\\num,\\num"
        ".endr"
        ".equ.L__gpr_num_xzr,31"
        ".equ.L__gpr_num_wzr,31"
        ".macromrs_s,rt,sreg"
        ".inst(0xd5200000|(\\sreg)|(.L__gpr_num_\\rt))"
        ".endm";
    static const char prefix[] =
        ".if1==1661:isbmrs%0,";
    static const char bridge[] =
        "662:.pushsection.altinstructions,\"a\""
        ".word661b-..word663f-..hword19"
        ".byte662b-661b.byte664f-663f"
        ".popsection.subsection1663:nop";
    static const char suffix[] =
        ".purgemmrs_s664:"
        ".org.-(664b-663b)+(662b-661b)"
        ".org.-(662b-661b)+(664b-663b)"
        ".previous.endif";
    static const char *const old_regs[] = {"cntpct_el0", "cntvct_el0"};
    char normalized[1600], expected[1600];
    size_t i, n = 0U;
    if (!text || !length || length >= sizeof(normalized)) return 0U;
    for (i = 0U; i < length; ++i) {
        const char ch = text[i];
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') continue;
        if (ch == '\0' || n + 1U >= sizeof(normalized)) return 0U;
        normalized[n++] = ch;
    }
    normalized[n] = '\0';
    for (i = 0U; i < 2U; ++i) {
        const int count = snprintf(expected, sizeof(expected),
            "%s%s%s%smrs_s%%0,(((3)<<19)|((3)<<16)|((14)<<12)|"
            "((0)<<8)|((%u)<<5))%s",
            prefix, old_regs[i], bridge, macro_table,
            (unsigned int)(5U + i), suffix);
        if (count > 0 && (size_t)count < sizeof(expected) &&
            strcmp(normalized, expected) == 0) return (unsigned int)(i + 1U);
    }
    return 0U;
}

static bool aarch64_linux_timer_alternative_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *result;
    if (!program || !src || !contract ||
        !aarch64_linux_timer_alternative_form(
            src->template_text, src->template_length) ||
        !src->is_volatile || src->is_goto ||
        src->output_count != 1U || src->input_count != 0U ||
        !src->outputs || src->label_count != 0U ||
        src->clobber_count != (src->has_memory_clobber ? 1U : 0U) ||
        src->register_clobber_count != 0U ||
        !constraint(&src->outputs[0], "=r"))
        return false;
    result = minic_c0_program_expression(program, src->outputs[0].expression);
    if (!result || result->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(result->type) ||
        !aarch64_linux_sysreg_u64_type(result->type)) return false;
    contract->operand_count = 1U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    return true;
}

static bool aarch64_linux_timer_alternative_emit(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info,
    unsigned int form) {
    const MinicCoreFunction *fn = ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *op =
        ins->value.structured_inline_asm.operands;
    MinicType result;
    if (!out || !fn || !info || !op || ctx->dynamic_frame ||
        !info->is_volatile || info->is_goto ||
        info->register_clobber_count != 0U ||
        (form != 1U && form != 2U) ||
        ins->value.structured_inline_asm.operand_count != 1U ||
        op[0].operand_index != 0U ||
        op[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT ||
        op[0].early_clobber || op[0].has_fixed_register_binding ||
        op[0].value >= fn->value_count ||
        !aarch64_linux_structured_spill_slot_valid(op[0].value) ||
        !minic_type_pointee(fn->values[op[0].value].type, &result) ||
        !aarch64_linux_sysreg_u64_type(result)) return false;
    return fprintf(out,
        "  .if 1 == 1\n"
        "661:\n  isb\n  mrs x9, %s\n"
        "662:\n"
        "  .pushsection .altinstructions,\"a\"\n"
        "  .word 661b - .\n"
        "  .word 663f - .\n"
        "  .hword 19\n"
        "  .byte 662b-661b\n"
        "  .byte 664f-663f\n"
        "  .popsection\n"
        "  .subsection 1\n"
        "663:\n  nop\n  .inst 0x%08x\n"
        "664:\n"
        "  .org . - (664b-663b) + (662b-661b)\n"
        "  .org . - (662b-661b) + (664b-663b)\n"
        "  .previous\n  .endif\n"
        "  ldr x10, [sp, #%zu]\n"
        "  str x9, [x10]\n",
        form == 1U ? "cntpct_el0" : "cntvct_el0",
        0xd53be000U | ((form == 1U ? 5U : 6U) << 5U) | 9U,
        (size_t)op[0].value * 8U) >= 0;
}

/* Linux arch_counter_enforce_ordering() is a real dependent LOAD from SP.
 * The EOR dependency on the timer result controls execution ordering even
 * though its mathematical result is zero; do not replace with a C cast,
 * empty compiler barrier, or a substitute DMB. GNU has no explicit memory
 * clobber, so record its implicit hardware load in Core's effect metadata. */
static bool aarch64_linux_counter_ordering_form(const char *text, size_t len) {
    static const char exact[] = "eor%0,%1,%1add%0,sp,%0ldrxzr,[%0]";
    char compact[100];
    size_t i, n = 0U;
    if (!text || !len || len >= sizeof(compact)) return false;
    for (i = 0U; i < len; ++i) {
        const char ch = text[i];
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') continue;
        if (ch == '\0' || n + 1U >= sizeof(compact)) return false;
        compact[n++] = ch;
    }
    compact[n] = '\0';
    return strcmp(compact, exact) == 0;
}

static bool aarch64_linux_counter_ordering_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *out, *input;
    if (!program || !src || !contract || !src->is_volatile || src->is_goto ||
        !aarch64_linux_counter_ordering_form(
            src->template_text, src->template_length) ||
        src->output_count != 1U || src->input_count != 1U ||
        !src->outputs || !src->inputs || src->clobber_count != 0U ||
        src->register_clobber_count != 0U || src->has_memory_clobber ||
        src->label_count != 0U || !constraint(&src->outputs[0], "=r") ||
        !constraint(&src->inputs[0], "r")) return false;
    out = minic_c0_program_expression(program, src->outputs[0].expression);
    input = minic_c0_program_expression(program, src->inputs[0].expression);
    if (!out || !input || out->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(out->type) ||
        !aarch64_linux_sysreg_u64_type(out->type) ||
        !aarch64_linux_sysreg_u64_type(input->type)) return false;
    contract->operand_count = 2U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

static bool aarch64_linux_counter_ordering_emit(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreFunction *fn = ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *op =
        ins->value.structured_inline_asm.operands;
    MinicType result;
    if (!out || !fn || !info || !op || ctx->dynamic_frame ||
        !aarch64_linux_counter_ordering_form(
            info->template_text, info->template_length) ||
        !info->is_volatile || info->is_goto ||
        !info->has_memory_clobber || info->register_clobber_count != 0U ||
        ins->value.structured_inline_asm.operand_count != 2U ||
        op[0].operand_index != 0U || op[1].operand_index != 1U ||
        op[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT ||
        op[1].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
        op[0].early_clobber || op[1].early_clobber ||
        op[0].has_fixed_register_binding || op[1].has_fixed_register_binding ||
        op[0].value >= fn->value_count || op[1].value >= fn->value_count ||
        !aarch64_linux_structured_spill_slot_valid(op[0].value) ||
        !aarch64_linux_structured_spill_slot_valid(op[1].value) ||
        !minic_type_pointee(fn->values[op[0].value].type, &result) ||
        !aarch64_linux_sysreg_u64_type(result) ||
        !aarch64_linux_sysreg_u64_type(fn->values[op[1].value].type))
        return false;
    return fprintf(out,
        "  ldr x10, [sp, #%zu]\n"
        "  eor x9, x10, x10\n"
        "  add x9, sp, x9\n"
        "  ldr xzr, [x9]\n"
        "  ldr x11, [sp, #%zu]\n"
        "  str x9, [x11]\n",
        (size_t)op[1].value * 8U,
        (size_t)op[0].value * 8U) >= 0;
}

/* ARMv8.5 RNDR and RNDRRS return their success state in NZCV. The CSET
 * must consume the flags from the SAME system-register read, with no
 * intervening flag-setting instructions or compiler-generated tests.
 * Linux emits the GNU .irp register table and MRS_S macro to make the
 * encoding portable across assemblers lacking named RNDR support. */
static unsigned int aarch64_linux_rng_sysreg_form(
    const char *text, size_t length) {
    static const char reg_table[] =
        ".irpnum,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30"
        ".equ.L__gpr_num_x\\num,\\num"
        ".equ.L__gpr_num_w\\num,\\num"
        ".endr"
        ".equ.L__gpr_num_xzr,31"
        ".equ.L__gpr_num_wzr,31"
        ".macromrs_s,rt,sreg"
        ".inst(0xd5200000|(\\sreg)|(.L__gpr_num_\\rt))"
        ".endm";
    char normalized[850], expected[850];
    size_t i, n = 0U;
    if (!text || !length || length >= sizeof(normalized)) return 0U;
    for (i = 0U; i < length; ++i) {
        const char ch = text[i];
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') continue;
        if (ch == '\0' || n + 1U >= sizeof(normalized)) return 0U;
        normalized[n++] = ch;
    }
    normalized[n] = '\0';
    for (i = 0U; i < 2U; ++i) {
        const int count = snprintf(expected, sizeof(expected),
            "%smrs_s%%0,(((3)<<19)|((3)<<16)|((2)<<12)|"
            "((4)<<8)|((%u)<<5)).purgemmrs_scset%%w1,ne",
            reg_table, (unsigned int)i);
        if (count > 0 && (size_t)count < sizeof(expected) &&
            strcmp(normalized, expected) == 0) return (unsigned int)(i + 1U);
    }
    return 0U;
}

static bool aarch64_linux_rng_sysreg_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *val, *ok;
    if (!program || !src || !contract ||
        !aarch64_linux_rng_sysreg_form(src->template_text, src->template_length) ||
        !src->is_volatile || src->is_goto ||
        !src->outputs || src->output_count != 2U || src->input_count != 0U ||
        src->label_count != 0U || src->has_memory_clobber ||
        src->clobber_count != 1U || src->register_clobber_count != 1U ||
        !src->register_clobbers ||
        src->register_clobbers[0].name_length != 2U ||
        memcmp(src->register_clobbers[0].name, "cc", 2U) != 0 ||
        !constraint(&src->outputs[0], "=r") ||
        !constraint(&src->outputs[1], "=r")) return false;
    val = minic_c0_program_expression(program, src->outputs[0].expression);
    ok = minic_c0_program_expression(program, src->outputs[1].expression);
    if (!val || !ok || val->value_category != MINIC_VALUE_LVALUE ||
        ok->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(val->type) || minic_type_is_const(ok->type) ||
        !minic_type_equal(val->type, minic_type_unsigned_long()) ||
        !minic_type_equal(ok->type, minic_type_bool())) return false;
    contract->operand_count = 2U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    return true;
}

static bool aarch64_linux_rng_sysreg_emit(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info,
    unsigned int form) {
    const MinicCoreFunction *fn = ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *op =
        ins->value.structured_inline_asm.operands;
    MinicType type;
    size_t i;
    if (!out || !fn || !info || !op || ctx->dynamic_frame ||
        !info->is_volatile || info->is_goto || info->has_memory_clobber ||
        info->register_clobber_count != 1U ||
        !info->register_clobbers ||
        info->register_clobbers[0].name_length != 2U ||
        memcmp(info->register_clobbers[0].name, "cc", 2U) != 0 ||
        (form != 1U && form != 2U) ||
        ins->value.structured_inline_asm.operand_count != 2U) return false;
    for (i = 0U; i < 2U; ++i) {
        if (op[i].operand_index != i ||
            op[i].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT ||
            op[i].early_clobber || op[i].has_fixed_register_binding ||
            op[i].value >= fn->value_count ||
            !aarch64_linux_structured_spill_slot_valid(op[i].value) ||
            !minic_type_pointee(fn->values[op[i].value].type, &type) ||
            !minic_type_equal(type, i == 0U
                ? minic_type_unsigned_long() : minic_type_bool())) return false;
    }
    /* MRS_S sets NZCV; CSET immediately captures Z=0 as 1. The subsequent
     * stack spills cannot change the already materialized success bit. */
    return fprintf(out,
        "  .inst 0x%08x\n"
        "  cset w10, ne\n"
        "  ldr x11, [sp, #%zu]\n"
        "  str x9, [x11]\n"
        "  ldr x11, [sp, #%zu]\n"
        "  strb w10, [x11]\n",
        0xd53b2409U | ((form == 2U ? 1U : 0U) << 5U),
        (size_t)op[0].value * 8U,
        (size_t)op[1].value * 8U) >= 0;
}

/* Qualcomm arm64 return-stack-buffer sanitization intentionally executes
 * sixteen BL instructions to poison the predictor while preserving x30.
 * The generated call targets are the *next* instruction, not C functions;
 * the usual direct-call ABI machinery must not turn these into thunks. */
static bool aarch64_linux_qcom_return_stack_form(
    const char *text, size_t length) {
    static const char exact[] = "mov%0,x30.rept16bl.+4.endrmovx30,%0";
    char compact[160];
    size_t i, n = 0U;
    if (!text || length == 0U || length >= sizeof(compact)) return false;
    for (i = 0U; i < length; ++i) {
        const char ch = text[i];
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') continue;
        if (ch == '\0' || n + 1U >= sizeof(compact)) return false;
        compact[n++] = ch;
    }
    compact[n] = '\0';
    return strcmp(compact, exact) == 0;
}

static bool aarch64_linux_qcom_return_stack_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *tmp;
    if (!program || !src || !contract || !src->is_volatile || src->is_goto ||
        !aarch64_linux_qcom_return_stack_form(
            src->template_text, src->template_length) ||
        src->output_count != 1U || src->input_count != 0U || !src->outputs ||
        src->label_count != 0U || src->clobber_count != 0U ||
        src->register_clobber_count != 0U || src->has_memory_clobber ||
        !constraint(&src->outputs[0], "=&r")) return false;
    tmp = minic_c0_program_expression(program, src->outputs[0].expression);
    if (!tmp || tmp->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(tmp->type) ||
        !aarch64_linux_sysreg_u64_type(tmp->type)) return false;
    contract->operand_count = 1U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    return true;
}

static bool aarch64_linux_qcom_return_stack_emit(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreFunction *fn = ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *op =
        ins->value.structured_inline_asm.operands;
    MinicType value;
    if (!out || !fn || !op || !info || ctx->dynamic_frame ||
        !info->is_volatile || info->is_goto || info->has_memory_clobber ||
        info->register_clobber_count != 0U ||
        !aarch64_linux_qcom_return_stack_form(
            info->template_text, info->template_length) ||
        ins->value.structured_inline_asm.operand_count != 1U ||
        op[0].operand_index != 0U ||
        op[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT ||
        op[0].early_clobber || op[0].has_fixed_register_binding ||
        op[0].value >= fn->value_count ||
        !aarch64_linux_structured_spill_slot_valid(op[0].value) ||
        !minic_type_pointee(fn->values[op[0].value].type, &value) ||
        !aarch64_linux_sysreg_u64_type(value)) return false;
    return fprintf(out,
        "  mov x9, x30\n"
        "  .rept 16\n"
        "  bl . + 4\n"
        "  .endr\n"
        "  mov x30, x9\n"
        "  ldr x10, [sp, #%zu]\n"
        "  str x9, [x10]\n",
        (size_t)op[0].value * 8U) >= 0;
}

/* Linux arm64 cache-maintenance operations are architectural side effects:
 * instruction/data cache to PoU/PoC, not a mere compiler "memory" fence.
 * These opcodes execute at the requested address in the target ISA. Accept
 * only this exact one-register GNU spelling and a typed 64-bit address; in
 * particular, a semicolon, register suffix or second instruction cannot
 * sneak through an open-ended GNU ASM passthrough. */
static const char *aarch64_linux_cache_op_form(
    const char *text, size_t length) {
    static const struct { const char *compact; const char *asm_text; } forms[] = {
        {"dcivac,%0", "dc ivac"},
        {"dccvac,%0", "dc cvac"},
        {"dccvau,%0", "dc cvau"},
        {"dccivac,%0", "dc civac"},
        {"icivau,%0", "ic ivau"}
    };
    char normalized[80];
    size_t i, n = 0U;
    if (!text || length == 0U || length >= sizeof(normalized)) return NULL;
    for (i = 0U; i < length; ++i) {
        const char ch = text[i];
        if (ch == ' ' || ch == '\t') continue;
        if (ch == '\0' || ch == '\n' || ch == '\r' || ch == ';' ||
            n + 1U >= sizeof(normalized)) return NULL;
        normalized[n++] = ch;
    }
    normalized[n] = '\0';
    for (i = 0U; i < sizeof(forms)/sizeof(forms[0]); ++i)
        if (strcmp(normalized, forms[i].compact) == 0)
            return forms[i].asm_text;
    return NULL;
}

static bool aarch64_linux_cache_op_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *address;
    MinicType plain;
    if (!program || !src || !contract || !src->is_volatile ||
        src->is_goto || src->output_count != 0U || src->input_count != 1U ||
        !src->inputs || src->clobber_count != 1U ||
        !src->has_memory_clobber || src->register_clobber_count != 0U ||
        src->label_count != 0U || !constraint(&src->inputs[0], "r") ||
        !aarch64_linux_cache_op_form(
            src->template_text, src->template_length)) return false;
    address = minic_c0_program_expression(program, src->inputs[0].expression);
    if (!address || !minic_type_unqualified(address->type, &plain) ||
        (!minic_type_is_pointer(plain) &&
         !minic_type_equal(plain, minic_type_unsigned_long()) &&
         !minic_type_equal(plain, minic_type_unsigned_long_long())))
        return false;
    contract->operand_count = 1U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

static bool aarch64_linux_cache_op_emit(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreFunction *fn = ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *op =
        ins->value.structured_inline_asm.operands;
    const char *mnemonic = aarch64_linux_cache_op_form(
        info->template_text, info->template_length);
    MinicType type;
    if (!mnemonic || !info->is_volatile || info->is_goto ||
        !info->has_memory_clobber || info->register_clobber_count ||
        ctx->dynamic_frame ||
        ins->value.structured_inline_asm.operand_count != 1U || !op ||
        op->operand_index != 0U ||
        op->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
        op->has_fixed_register_binding || op->early_clobber ||
        op->value >= fn->value_count ||
        !aarch64_linux_structured_spill_slot_valid(op->value) ||
        !minic_type_unqualified(fn->values[op->value].type, &type) ||
        (!minic_type_is_pointer(type) &&
         !minic_type_equal(type, minic_type_unsigned_long()) &&
         !minic_type_equal(type, minic_type_unsigned_long_long())))
        return false;
    return fprintf(out, "  ldr x9, [sp, #%zu]\n  %s, x9\n",
                   (size_t)op->value * 8U, mnemonic) >= 0;
}

/* User-cache maintenance is the exception-recovering companion of the
 * ordinary DC/IC API. In the kernel's undef trap emulator, the cache
 * operation can fault while accessing TTBR0. Its type-2 __ex_table entry
 * names the C return-status register; silently issuing a bare DC/IC would
 * turn -EFAULT into an unhandled kernel abort. The source uses precisely
 * four hardware operations (three CRM cases share DC CIVAC). */
static const char *aarch64_linux_user_cache_fault_form(
    const char *text, size_t length) {
    static const struct { const char *prefix; const char *mnemonic; } forms[] = {
        {"1:dccivac,%1mov%w0,#02:", "dc civac"},
        {"1:sys3,c7,c13,1,%1mov%w0,#02:", "sys 3, c7, c13, 1"},
        {"1:sys3,c7,c12,1,%1mov%w0,#02:", "sys 3, c7, c12, 1"},
        {"1:icivau,%1mov%w0,#02:", "ic ivau"}
    };
    static const char tail[] =
        ".irpnum,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30"
        ".equ.L__gpr_num_x\\num,\\num"
        ".equ.L__gpr_num_w\\num,\\num"
        ".endr"
        ".equ.L__gpr_num_xzr,31"
        ".equ.L__gpr_num_wzr,31"
        ".pushsection__ex_table,\"a\""
        ".align2"
        ".long((1b)-.)"
        ".long((2b)-.)"
        ".short(2)"
        ".short((((.L__gpr_num_%w0)<<0)|((.L__gpr_num_wzr)<<5)))"
        ".popsection";
    char compact[1400];
    size_t i, n = 0U;
    if (!text || !length || length >= sizeof(compact)) return NULL;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return NULL;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(forms) / sizeof(forms[0]); ++i) {
        const size_t prefix = strlen(forms[i].prefix), suffix = strlen(tail);
        if (n == prefix + suffix &&
            memcmp(compact, forms[i].prefix, prefix) == 0 &&
            memcmp(compact + prefix, tail, suffix) == 0)
            return forms[i].mnemonic;
    }
    return NULL;
}

static bool aarch64_linux_user_cache_fault_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *status, *address;
    MinicType plain;
    if (!program || !src || !contract || !src->is_volatile || src->is_goto ||
        src->output_count != 1U || !src->outputs ||
        src->input_count != 1U || !src->inputs ||
        src->label_count || src->clobber_count ||
        src->has_memory_clobber || src->register_clobber_count ||
        !constraint(&src->outputs[0], "=r") ||
        !constraint(&src->inputs[0], "r") ||
        !aarch64_linux_user_cache_fault_form(
            src->template_text, src->template_length))
        return false;
    status = minic_c0_program_expression(program, src->outputs[0].expression);
    address = minic_c0_program_expression(program, src->inputs[0].expression);
    if (!status || !address ||
        status->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(status->type) ||
        !minic_type_equal(status->type, minic_type_int()) ||
        !minic_type_unqualified(address->type, &plain) ||
        (!minic_type_equal(plain, minic_type_unsigned_long()) &&
         !minic_type_is_pointer(plain)))
        return false;
    contract->operand_count = 2U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

static bool aarch64_linux_user_cache_fault_emit(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreFunction *fn = ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *op =
        ins->value.structured_inline_asm.operands;
    const char *instruction = aarch64_linux_user_cache_fault_form(
        info->template_text, info->template_length);
    MinicType type;
    if (!instruction || !info->is_volatile || info->is_goto ||
        !info->has_memory_clobber || info->register_clobber_count ||
        ctx->dynamic_frame ||
        ins->value.structured_inline_asm.operand_count != 2U || !op ||
        op[0].operand_index != 0U || op[1].operand_index != 1U ||
        op[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT ||
        op[1].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
        op[0].early_clobber || op[1].early_clobber ||
        op[0].has_fixed_register_binding || op[1].has_fixed_register_binding ||
        op[0].value >= fn->value_count || op[1].value >= fn->value_count ||
        !aarch64_linux_structured_spill_slot_valid(op[0].value) ||
        !aarch64_linux_structured_spill_slot_valid(op[1].value) ||
        !minic_type_pointee(fn->values[op[0].value].type, &type) ||
        !minic_type_equal(type, minic_type_int()) ||
        !minic_type_unqualified(fn->values[op[1].value].type, &type) ||
        (!minic_type_equal(type, minic_type_unsigned_long()) &&
         !minic_type_is_pointer(type)))
        return false;
    /* W9 is the actual fault-rewritten result register, encoded as 9 in
     * type-2's data word. X10 is the cache-operation address operand. */
    return fprintf(out,
        "  ldr x10, [sp, #%zu]\n"
        "1: %s, x10\n"
        "  mov w9, #0\n"
        "2:\n"
        "  .pushsection __ex_table, \"a\"\n"
        "  .align 2\n"
        "  .long (1b) - .\n"
        "  .long (2b) - .\n"
        "  .short 2\n"
        "  .short %u\n"
        "  .popsection\n"
        "  ldr x11, [sp, #%zu]\n"
        "  str w9, [x11]\n",
        (size_t)op[1].value * 8U, instruction,
        9U | (31U << 5), (size_t)op[0].value * 8U) >= 0;
}
static bool aarch64_linux_bitrev_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *dst, *arg;
    static const char exact[] = "rbit %w0, %w1";
    if (!program || !src || !contract || src->is_volatile || src->is_goto ||
        src->template_length != sizeof(exact) - 1U ||
        memcmp(src->template_text, exact, sizeof(exact) - 1U) != 0 ||
        !src->outputs || !src->inputs || src->output_count != 1U ||
        src->input_count != 1U || src->label_count || src->clobber_count ||
        src->register_clobber_count || src->has_memory_clobber ||
        !constraint(&src->outputs[0], "=r") ||
        !constraint(&src->inputs[0], "r"))
        return false;
    dst = minic_c0_program_expression(program, src->outputs[0].expression);
    arg = minic_c0_program_expression(program, src->inputs[0].expression);
    if (!dst || !arg || dst->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(dst->type) ||
        !aarch64_stlr_type(dst->type, 4U) ||
        !aarch64_stlr_type(arg->type, 4U))
        return false;
    contract->operand_count = 2U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    return true;
}

static bool aarch64_linux_lse_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    Aarch64LseForm form;
    size_t i, count;
    if (!program || !src || !contract || !src->is_volatile || src->is_goto ||
        src->label_count || src->register_clobber_count || !src->outputs ||
        !src->inputs ||
        !aarch64_lse_form(src->template_text, src->template_length,
                          false, &form))
        return false;
    count = form.kind == AARCH64_LSE_STORE ? 2U : 3U;
    if (src->output_count != count - 1U || src->input_count != 1U ||
        src->clobber_count != (form.mode != 0U ? 1U : 0U) ||
        src->has_memory_clobber != (form.mode != 0U) ||
        !constraint(&src->outputs[0], "+Q") ||
        (form.kind == AARCH64_LSE_FETCH &&
         !constraint(&src->outputs[1], "=r")) ||
        (form.kind == AARCH64_LSE_CAS &&
         !constraint(&src->outputs[1], "+r")) ||
        !constraint(&src->inputs[0],
                    form.kind == AARCH64_LSE_CAS ? "rZ" : "r"))
        return false;
    for (i = 0U; i < count; ++i) {
        const bool output = i < src->output_count;
        const MinicInlineAsmOperand *op = output ? &src->outputs[i] :
            &src->inputs[i - src->output_count];
        const MinicExpression *value = minic_c0_program_expression(
            program, op->expression);
        if (!value || (output &&
            (value->value_category != MINIC_VALUE_LVALUE ||
             minic_type_is_const(value->type))) ||
            !aarch64_stlr_type(value->type, form.width))
            return false;
        contract->kinds[i] = i == 0U
            ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE
            : output
                ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE
                : MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        if (form.kind == AARCH64_LSE_FETCH && i == 1U)
            contract->kinds[i] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    }
    contract->operand_count = count;
    return true;
}

bool minic_aarch64_qualify_inline_asm(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *expr;
    MinicType plain;
    size_t i;
    /* AArch64 admits the GNU source roles only with exact types and
     * the original exclusive-load control-flow template. */
    if (program && src && contract && src->is_volatile && !src->is_goto &&
        src->outputs && src->inputs && src->output_count==2U &&
        src->input_count==1U && src->clobber_count==0U &&
        src->register_clobber_count==0U && !src->has_memory_clobber &&
        src->label_count==0U) {
        const unsigned int width=aarch64_linux_cmpwait_form(
            src->template_text,src->template_length);
        if (width!=0U && constraint(&src->outputs[0],"=&r") &&
            constraint(&src->outputs[1],"+Q") &&
            constraint(&src->inputs[0],"r")) {
            const MinicExpression *tmp=minic_c0_program_expression(
                program,src->outputs[0].expression);
            const MinicExpression *memory=minic_c0_program_expression(
                program,src->outputs[1].expression);
            const MinicExpression *value=minic_c0_program_expression(
                program,src->inputs[0].expression);
            if (tmp && memory && value &&
                tmp->value_category==MINIC_VALUE_LVALUE &&
                memory->value_category==MINIC_VALUE_LVALUE &&
                !minic_type_is_const(tmp->type) &&
                !minic_type_is_const(memory->type) &&
                aarch64_linux_sysreg_u64_type(tmp->type) &&
                aarch64_linux_sysreg_u64_type(value->type) &&
                aarch64_stlr_type(memory->type,width)) {
                contract->operand_count=3U;
                contract->kinds[0]=MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
                contract->kinds[1]=MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
                contract->kinds[2]=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
                return true;
            }
        }
    }
    if(aarch64_linux_bitrev_source(program,src,contract))return true;
    if(aarch64_linux_hyp_spin_source(program,src,contract))return true;
    if(aarch64_linux_lse_source(program,src,contract))return true;
    if(aarch64_linux_xpaclri_source(program,src,contract))return true;
    if(aarch64_linux_smccc_source(program,src,contract))return true;
    if(aarch64_linux_svc_source(program,src,contract))return true;
    if(aarch64_linux_wfxt_source(program,src,contract))return true;
    if(aarch64_linux_atomic_dec_positive_source(program,src,contract))return true;
    if(aarch64_linux_cmpxchg128_source(program,src,contract))return true;
    if(aarch64_linux_futex_source(program,src,contract))return true;
    if(aarch64_linux_disabled_hw_pan_source(src,contract))return true;
    if(aarch64_linux_timer_alternative_source(program,src,contract))return true;
    if(aarch64_linux_counter_ordering_source(program,src,contract))return true;
    if(aarch64_linux_rng_sysreg_source(program,src,contract))return true;
    if(aarch64_linux_qcom_return_stack_source(program,src,contract))return true;
    if(aarch64_linux_uaccess_source(program,src,contract))return true;
    if(aarch64_linux_zeropad_source(program,src,contract))return true;
    if(aarch64_linux_user_cache_fault_source(program,src,contract))return true;
    if(aarch64_linux_cache_op_source(program,src,contract))return true;
    if(aarch64_linux_encoded_sysreg_write_source(program,src,contract))
        return true;
    if(aarch64_linux_vhe_sysreg_alternative_source(program,src,contract))
        return true;
    if(aarch64_linux_io_dependency_source(program,src,contract))
        return true;
    if(aarch64_linux_encoded_sysreg_source(program,src,contract))
        return true;
    if(aarch64_linux_tlbi_source(program,src,contract))return true;
    if(aarch64_linux_kvm_at_fixup_source(program,src,contract))return true;
    if(aarch64_linux_at_s1e1r_source(program,src,contract))return true;
    if (src && contract && !src->is_goto && !src->is_volatile &&
        !src->output_count && !src->input_count && !src->label_count &&
        !src->clobber_count && !src->register_clobber_count &&
        !src->has_memory_clobber &&
        aarch64_linux_barrier_alternative(
            src->template_text, src->template_length)) {
        contract->operand_count = 0U;
        contract->implicit_memory_effect = true;
        return true;
    }
    /* The exact native AArch64 zero-input ISA list is shared by source
     * qualification and emission. The original Linux DAIF helpers use
     * implicit volatile ASM with no explicit memory clobber. Both that
     * source and explicit clobber variants emit the SAME physical hardware
     * instruction; rejecting the former loses original kernel semantics. */
    if (program && src && contract &&
        !src->is_goto && src->output_count == 0U && src->input_count == 0U &&
        src->register_clobber_count == 0U && src->label_count == 0U) {
        const char *form=aarch64_barrier_template(
            src->template_text,src->template_length);
        /* GNU extended/basic asm without outputs is implicitly volatile.
         * The original Linux `asm("ic ialluis")` omits the keyword, but
         * still executes a privileged I-cache invalidate. Admit this
         * exact ISA opcode without weakening other nonvolatile forms. */
        if(form && (src->is_volatile || strcmp(form,"ic ialluis")==0 ||
                    strcmp(form,"ic iallu")==0) &&
           src->clobber_count <= 1U &&
           src->has_memory_clobber==(src->clobber_count==1U)) {
            contract->operand_count=0U;
            return true;
        }
    }
    if (program && src && contract && !src->is_goto &&
        src->output_count == 0U && src->input_count == 1U && src->inputs &&
        src->clobber_count == 0U && src->register_clobber_count == 0U &&
        !src->has_memory_clobber && src->label_count == 0U &&
        aarch64_linux_brk_template(src->template_text, src->template_length) &&
        constraint(&src->inputs[0], "I")) {
        expr = minic_c0_program_expression(program, src->inputs[0].expression);
        if (expr && expr->kind == MINIC_EXPRESSION_INTEGER &&
            aarch64_linux_brk_value(expr->value.integer_value)) {
            contract->operand_count = 1U;
            contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
            return true;
        }
    }
    if (program && src && contract && !src->is_volatile &&
        !src->is_goto && src->outputs && src->inputs &&
        src->output_count == 1U && src->input_count == 1U &&
        src->clobber_count == 0U && src->register_clobber_count == 0U &&
        !src->has_memory_clobber && src->label_count == 0U &&
        aarch64_linux_percpu_alternative_template(
            src->template_text, src->template_length) &&
        constraint(&src->outputs[0], "=r") &&
        constraint(&src->inputs[0], "Q")) {
        const MinicExpression *dst = minic_c0_program_expression(
            program, src->outputs[0].expression);
        const MinicExpression *dependency = minic_c0_program_expression(
            program, src->inputs[0].expression);
        MinicType plain_dst, plain_dependency;
        if (dst && dependency &&
            dst->value_category == MINIC_VALUE_LVALUE &&
            dependency->value_category == MINIC_VALUE_LVALUE &&
            !minic_type_is_const(dst->type) &&
            minic_type_unqualified(dst->type, &plain_dst) &&
            minic_type_unqualified(dependency->type, &plain_dependency) &&
            minic_type_equal(plain_dst, minic_type_unsigned_long()) &&
            minic_type_equal(plain_dependency, minic_type_unsigned_long())) {
            contract->operand_count = 2U;
            contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
            contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
            return true;
        }
    }
    if (program && src && contract && src->is_volatile &&
        !src->is_goto && src->output_count == 0U &&
        src->input_count == 1U && src->inputs &&
        src->clobber_count == 1U && src->register_clobber_count == 0U &&
        src->has_memory_clobber && src->label_count == 0U &&
        aarch64_linux_percpu_write_alternative_template(
            src->template_text, src->template_length) &&
        constraint(&src->inputs[0], "r")) {
        const MinicExpression *value = minic_c0_program_expression(
            program, src->inputs[0].expression);
        if (value && minic_type_unqualified(value->type, &plain) &&
            minic_type_equal(plain, minic_type_unsigned_long())) {
            contract->operand_count = 1U;
            contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
            contract->implicit_memory_effect = true;
            return true;
        }
    }
     /* Preserve the whole original Linux MMIO load alternative and its
     * replaceable acquire variant. Source uses =r and a pointer input.
     * This is NOT a Core-native portable volatile-load instruction. */
    if (program && src && contract && src->is_volatile && !src->is_goto &&
        src->output_count == 1U && src->input_count == 1U &&
        src->outputs && src->inputs && src->label_count == 0U &&
        src->clobber_count == 0U && src->register_clobber_count == 0U &&
        !src->has_memory_clobber &&
        constraint(&src->outputs[0], "=r") &&
        constraint(&src->inputs[0], "r")) {
        const Aarch64RawIoRead *d = aarch64_raw_io_read(
            src->template_text, src->template_length);
        const MinicExpression *dst = minic_c0_program_expression(
            program, src->outputs[0].expression);
        const MinicExpression *ptr = minic_c0_program_expression(
            program, src->inputs[0].expression);
        if (d && dst && ptr &&
            dst->value_category == MINIC_VALUE_LVALUE &&
            !minic_type_is_const(dst->type) &&
            aarch64_stlr_type(dst->type, d->width) &&
            minic_type_is_pointer(ptr->type)) {
            contract->operand_count = 2U;
            contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
            contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
            return true;
        }
    }

    /* Full Linux load-acquire C source pattern: =r output, Q memory
     * dependency, and GNU memory barrier. Not a plain Core LOAD. */
    if(program&&src&&contract&&src->is_volatile&&!src->is_goto&&
       src->output_count==1U&&src->input_count==1U&&
       src->outputs&&src->inputs&&!src->label_count&&
       src->clobber_count==1U&&src->has_memory_clobber&&
       !src->register_clobber_count&&
       constraint(&src->outputs[0],"=r")&&
       constraint(&src->inputs[0],"Q")) {
        const size_t width=aarch64_load_acquire_width(
            src->template_text,src->template_length);
        const MinicExpression *dst=minic_c0_program_expression(
            program,src->outputs[0].expression);
        const MinicExpression *mem=minic_c0_program_expression(
            program,src->inputs[0].expression);
        if(width&&dst&&mem&&dst->value_category==MINIC_VALUE_LVALUE&&
           mem->value_category==MINIC_VALUE_LVALUE&&
           !minic_type_is_const(dst->type)&&
           aarch64_stlr_type(dst->type,width)&&
           aarch64_stlr_memory_type(mem->type,width)) {
            contract->operand_count=2U;
            contract->kinds[0]=MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
            contract->kinds[1]=MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
            return true;
        }
    }

    /* Linux arm64 hardware prefetch is an explicit volatile ISA hint,
     * transported as a generic Core pointer scalar. No generic PRFM
     * passthrough or hardcoded function names in Frontend/Core. */
    if (program && src && contract && src->is_volatile && !src->is_goto &&
        src->output_count == 0U && src->input_count == 1U &&
        src->inputs && src->label_count == 0U &&
        src->clobber_count == 0U && src->register_clobber_count == 0U &&
        !src->has_memory_clobber &&
        aarch64_linux_prefetch_template(
            src->template_text, src->template_length) &&
        constraint(&src->inputs[0], "p")) {
        const MinicExpression *arg = minic_c0_program_expression(
            program, src->inputs[0].expression);
        if (arg && minic_type_is_pointer(arg->type)) {
            contract->operand_count = 1U;
            contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
            return true;
        }
    }
    /* Linux's real GNU memory-address input is an r pointer, not Qo.
     * Keep original two scalar constraints and explicitly record the
     * hardware memory effect for Common Core. */
    if (program && src && contract && src->is_volatile && !src->is_goto &&
        src->output_count == 0U && src->input_count == 2U &&
        src->inputs != NULL && src->clobber_count == 0U &&
        src->register_clobber_count == 0U && !src->has_memory_clobber &&
        src->label_count == 0U &&
        constraint(&src->inputs[0], "rZ") &&
        constraint(&src->inputs[1], "r")) {
        const Aarch64RawIoStore *store = aarch64_raw_io_store(
            src->template_text, src->template_length);
        const MinicExpression *value = minic_c0_program_expression(
            program, src->inputs[0].expression);
        const MinicExpression *pointer = minic_c0_program_expression(
            program, src->inputs[1].expression);
        if (store && value && pointer &&
            minic_type_is_pointer(pointer->type) &&
            aarch64_stlr_type(value->type, store->width)) {
            contract->operand_count = 2U;
            contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
            contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
            contract->implicit_memory_effect = true;
            return true;
        }
    }
    /* Existing Core roles: =Q address, rZ scalar input. ISA ownership stays
     * in this AArch64 callback, not in Frontend or generic Core scanning. */
    if (program && src && contract && src->is_volatile && !src->is_goto &&
        src->outputs && src->inputs && src->output_count == 1U &&
        src->input_count == 1U && src->clobber_count == 1U &&
        src->has_memory_clobber && src->register_clobber_count == 0U &&
        src->label_count == 0U &&
        constraint(&src->outputs[0], "=Q") &&
        constraint(&src->inputs[0], "rZ")) {
        const size_t width = aarch64_stlr_width(
            src->template_text, src->template_length);
        const MinicExpression *dst = minic_c0_program_expression(
            program, src->outputs[0].expression);
        const MinicExpression *value = minic_c0_program_expression(
            program, src->inputs[0].expression);
        if (width && dst && value &&
            dst->value_category == MINIC_VALUE_LVALUE &&
            !minic_type_is_const(dst->type) &&
            aarch64_stlr_memory_type(dst->type, width) &&
            aarch64_stlr_type(value->type, width)) {
            contract->operand_count = 2U;
            contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT;
            contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
            return true;
        }
    }
    /* CMPXCHG reuses 5 Core roles: tmp old-value output, observed output,
     * +Q memory, old expected, new replacement. The target alone owns the
     * LL/SC instructions, widths, GPR constraints and ordering. */
    if (program && src && contract && src->is_volatile && !src->is_goto &&
        src->outputs && src->inputs && src->output_count == 3U &&
        src->input_count == 2U && src->label_count == 0U &&
        src->register_clobber_count == 0U &&
        constraint(&src->outputs[0], "=&r") &&
        constraint(&src->outputs[1], "=&r") &&
        constraint(&src->outputs[2], "+Q") &&
        constraint(&src->inputs[1], "r")) {
        const unsigned int form = aarch64_linux_cmpxchg_form(
            src->template_text, src->template_length, false);
        const size_t width = aarch64_linux_cmpxchg_width(form);
        const unsigned int mode = form ? (form - 1U) % 4U : 0U;
        bool accepted = form != 0U &&
            src->clobber_count == (mode == 0U ? 0U : 1U) &&
            src->has_memory_clobber == (mode != 0U) &&
            constraint(&src->inputs[0], width == 8U ? "Lr" : "Kr");
        for (i = 0U; i < 5U && accepted; ++i) {
            const MinicInlineAsmOperand *op = i < 3U ?
                &src->outputs[i] : &src->inputs[i - 3U];
            const MinicExpression *e =
                minic_c0_program_expression(program, op->expression);
            if (!e || (i < 3U &&
                (e->value_category != MINIC_VALUE_LVALUE ||
                 minic_type_is_const(e->type))) ||
                !aarch64_stlr_type(e->type, i == 0U || i == 3U ? 8U : width))
                accepted = false;
        }
        if (accepted) {
            contract->operand_count = 5U;
            contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
            contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
            contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
            contract->kinds[3] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
            contract->kinds[4] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
            return true;
        }
    }
    if (program && src && contract && src->is_volatile && !src->is_goto &&
        src->outputs && src->inputs && src->output_count == 3U &&
        src->input_count == 1U && src->label_count == 0U &&
        src->register_clobber_count == 0U &&
        constraint(&src->outputs[0], "=&r") &&
        constraint(&src->outputs[1], "=&r") &&
        constraint(&src->outputs[2], "+Q")) {
        Aarch64AtomicForm ret;
        bool accepted = aarch64_atomic_return_form(
            src->template_text, src->template_length, &ret);
        accepted = accepted &&
            src->clobber_count == (ret.mode == 1U ? 0U : 1U) &&
            src->has_memory_clobber == (ret.mode != 1U) &&
            constraint(&src->inputs[0], ret.op->rmw_constraint);
        for (i = 0U; i < 4U && accepted; ++i) {
            const MinicInlineAsmOperand *op = i < 3U ?
                &src->outputs[i] : &src->inputs[0];
            const MinicExpression *arg =
                minic_c0_program_expression(program, op->expression);
            if (!arg ||
                (i < 3U && (arg->value_category != MINIC_VALUE_LVALUE ||
                            minic_type_is_const(arg->type))) ||
                !aarch64_stlr_type(arg->type, i == 1U ? 8U : ret.width))
                accepted = false;
        }
        if (accepted) {
            contract->operand_count = 4U;
            contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
            contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
            contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
            contract->kinds[3] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
            return true;
        }
    }
    /* XCHG's 4 typed operands are identical Core roles to old atomic RMW,
     * but a distinct exact AArch64 ISA contract. No generic ASM fallback. */
    if (program && src && contract && src->is_volatile && !src->is_goto &&
        src->outputs && src->inputs && src->output_count == 3U &&
        src->input_count == 1U && src->label_count == 0U &&
        src->register_clobber_count == 0U &&
        constraint(&src->outputs[0], "=&r") &&
        constraint(&src->outputs[1], "=&r") &&
        constraint(&src->outputs[2], "+Q") &&
        constraint(&src->inputs[0], "r")) {
        Aarch64XchgForm xchg;
        bool eligible = aarch64_linux_xchg_form(
            src->template_text, src->template_length, &xchg);
        eligible = eligible &&
            src->clobber_count == (xchg.mode == 0U ? 0U : 1U) &&
            src->has_memory_clobber == (xchg.mode != 0U);
        for (i = 0U; i < 4U && eligible; ++i) {
            const MinicInlineAsmOperand *op = i < 3U ?
                &src->outputs[i] : &src->inputs[0];
            const MinicExpression *value =
                minic_c0_program_expression(program, op->expression);
            if (!value ||
                (i < 3U && (value->value_category != MINIC_VALUE_LVALUE ||
                            minic_type_is_const(value->type))) ||
                !aarch64_stlr_type(value->type, i == 1U ? 8U : xchg.width))
                eligible = false;
        }
        if (eligible) {
            contract->operand_count = 4U;
            contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
            contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
            contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
            contract->kinds[3] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
            return true;
        }
    }
    /* One source contract for 4 widths × 3 RMW operators and a
     * fourth add_return family. LL/SC status is u32 while tmp/value/memory
     * have the REAL 8/16/32/64-bit C width; never conflate those types. */
    if(program && src && contract && src->is_volatile && !src->is_goto &&
       src->outputs && src->inputs && src->output_count==3U &&
       src->input_count==1U && src->label_count==0U &&
       src->clobber_count==0U && src->register_clobber_count==0U &&
       !src->has_memory_clobber &&
       constraint(&src->outputs[0],"=&r") &&
       constraint(&src->outputs[1],"=&r") &&
       constraint(&src->outputs[2],"+Q") &&
       constraint(&src->inputs[0],"r")) {
        Aarch64PerCpuForm form;
        bool eligible=aarch64_linux_percpu_rmw_form(
            src->template_text,src->template_length,&form);
        for(i=0U;i<4U&&eligible;++i) {
            const MinicInlineAsmOperand *operand=i<3U?
                &src->outputs[i]:&src->inputs[0];
            const MinicExpression *value=minic_c0_program_expression(
                program,operand->expression);
            if(!value ||
               (i<3U&&(value->value_category!=MINIC_VALUE_LVALUE||
                       minic_type_is_const(value->type))) ||
               !aarch64_stlr_type(value->type,i==0U?4U:form.width))
                eligible=false;
        }
        if(eligible) {
            contract->operand_count=4U;
            contract->kinds[0]=MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
            contract->kinds[1]=MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
            contract->kinds[2]=MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
            contract->kinds[3]=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
            return true;
        }
    }
    if (program && src && contract && src->is_volatile && !src->is_goto &&
        src->output_count == 3U && src->input_count == 1U &&
        src->outputs && src->inputs && src->label_count == 0U &&
        src->clobber_count == 0U && src->register_clobber_count == 0U &&
        !src->has_memory_clobber &&
        constraint(&src->outputs[0], "=&r") &&
        constraint(&src->outputs[1], "=&r") &&
        constraint(&src->outputs[2], "+Q") &&
        (constraint(&src->inputs[0], "Lr") ||
         constraint(&src->inputs[0], "Kr") ||
         constraint(&src->inputs[0], "r") ||
         constraint(&src->inputs[0], "Ir") ||
         constraint(&src->inputs[0], "Jr"))) {
        Aarch64AtomicForm atomic;
        bool accepted = aarch64_atomic_form(
            src->template_text, src->template_length, false, &atomic);
        const size_t width = accepted ? atomic.width : 0U;
        accepted = accepted &&
            constraint(&src->inputs[0],
                       width == 8U ? atomic.op->rmw_constraint :
                                     atomic.op->fetch32_constraint);
        for (i = 0U; i < 4U && accepted; ++i) {
            const MinicInlineAsmOperand *operand = i < 3U ?
                &src->outputs[i] : &src->inputs[0];
            const MinicExpression *arg =
                minic_c0_program_expression(program, operand->expression);
            if (!arg ||
                (i < 3U && (arg->value_category != MINIC_VALUE_LVALUE ||
                            minic_type_is_const(arg->type))) ||
                !aarch64_stlr_type(arg->type, i == 1U ? 8U : width))
                accepted = false;
        }
        if (accepted) {
            contract->operand_count = 4U;
            contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
            contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
            contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
            contract->kinds[3] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
            return true;
        }
    }
    if (program && src && contract && src->is_volatile && !src->is_goto &&
        src->outputs && src->inputs && src->output_count == 4U &&
        src->input_count == 1U && src->label_count == 0U &&
        src->register_clobber_count == 0U &&
        constraint(&src->outputs[0], "=&r") &&
        constraint(&src->outputs[1], "=&r") &&
        constraint(&src->outputs[2], "=&r") &&
        constraint(&src->outputs[3], "+Q")) {
        Aarch64AtomicForm atomic;
        bool eligible = aarch64_atomic_form(
            src->template_text, src->template_length, true, &atomic);
        const size_t width = eligible ? atomic.width : 0U;
        const unsigned int mode = eligible ? atomic.mode : 0U;
        eligible = eligible &&
            src->clobber_count == (mode == 1U ? 0U : 1U) &&
            src->has_memory_clobber == (mode != 1U) &&
            constraint(&src->inputs[0],
                       width == 8U ? atomic.op->fetch64_constraint :
                                     atomic.op->fetch32_constraint);
        for (i = 0U; i < 5U && eligible; ++i) {
            const MinicInlineAsmOperand *operand = i < 4U ?
                &src->outputs[i] : &src->inputs[0];
            const MinicExpression *value = minic_c0_program_expression(
                program, operand->expression);
            if (!value ||
                (i < 4U && (value->value_category != MINIC_VALUE_LVALUE ||
                            minic_type_is_const(value->type))) ||
                !aarch64_stlr_type(
                    value->type, i == 2U ? 8U : width))
                eligible = false;
        }
        if (eligible) {
            contract->operand_count = 5U;
            contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
            contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
            contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
            contract->kinds[3] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
            contract->kinds[4] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
            return true;
        }
    }
    if (program && src && contract && src->is_volatile && !src->is_goto &&
        src->output_count == 0U && src->input_count == 1U && src->inputs &&
        src->clobber_count == 0U && src->register_clobber_count == 0U &&
        !src->has_memory_clobber && src->label_count == 0U &&
        aarch64_linux_msr_register(src->template_text, src->template_length) != NULL &&
        constraint(&src->inputs[0], "rZ")) {
        expr = minic_c0_program_expression(program, src->inputs[0].expression);
        if (expr && minic_type_unqualified(expr->type, &plain) &&
            /* The *original* Linux sysreg.h write_sysreg() materializes
             * u64 (__u64 = unsigned long long), whereas the interrupt
             * caller's flags and AAPCS64 GPR are unsigned long/64-bit.
             * They have equal machine width, not identical C type. */
            (minic_type_equal(plain, minic_type_unsigned_long()) ||
             minic_type_equal(plain, minic_type_unsigned_long_long()))) {
            contract->operand_count = 1U;
            contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
            return true;
        }
    }
    if (program && src && contract && src->is_volatile &&
        !src->is_goto && src->outputs && src->output_count == 1U &&
        src->input_count == 0U && src->clobber_count == 0U &&
        src->register_clobber_count == 0U && !src->has_memory_clobber &&
        src->label_count == 0U &&
        aarch64_linux_mrs_register(src->template_text, src->template_length) &&
        /* Original GNU Linux read_sysreg() uses asm volatile for all
         * whitelisted hardware status registers, not only DAIF.
         * Keep the exact register allowlist and typed output contract. */
        constraint(&src->outputs[0], "=r")) {
        expr = minic_c0_program_expression(program, src->outputs[0].expression);
        if (expr && expr->value_category == MINIC_VALUE_LVALUE &&
            !minic_type_is_const(expr->type) &&
            minic_type_unqualified(expr->type, &plain) &&
            aarch64_linux_sysreg_u64_type(plain)) {
            contract->operand_count = 1U;
            contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
            return true;
        }
    }
    if (program && src && contract &&
        !src->is_goto && !src->is_volatile &&
        src->outputs && src->output_count == 1U &&
        src->input_count == 0U && src->clobber_count == 0U &&
        src->register_clobber_count == 0U && !src->has_memory_clobber &&
        aarch64_linux_mrs_register(src->template_text, src->template_length) &&
        /* DAIF IRQ mask read is a volatile Linux sysreg operation; do not
         * expand the historical nonvolatile EL1 CPU-offset MRS allowlist. */
        strcmp(aarch64_linux_mrs_register(
                   src->template_text, src->template_length), "daif") != 0 &&
        constraint(&src->outputs[0], "=r")) {
        expr = minic_c0_program_expression(program, src->outputs[0].expression);
        if (expr && minic_type_unqualified(expr->type, &plain) &&
            aarch64_linux_sysreg_u64_type(plain) &&
            expr->value_category == MINIC_VALUE_LVALUE &&
            !minic_type_is_const(expr->type)) {
            contract->operand_count = 1U;
            contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
            return true;
        }
    }
    if (program && src && contract && src->is_volatile && !src->is_goto &&
        src->outputs && src->inputs && src->output_count == 1U &&
        src->input_count == 2U && src->clobber_count == 0U &&
        src->register_clobber_count == 0U && !src->has_memory_clobber &&
        src->label_count == 0U &&
        aarch64_linux_uaccess_bic_template(
            src->template_text, src->template_length) &&
        constraint(&src->outputs[0], "=r") &&
        constraint(&src->inputs[0], "r") &&
        constraint(&src->inputs[1], "i")) {
        const MinicExpression *out = minic_c0_program_expression(
            program, src->outputs[0].expression);
        const MinicExpression *arg = minic_c0_program_expression(
            program, src->inputs[0].expression);
        const MinicExpression *mask = minic_c0_program_expression(
            program, src->inputs[1].expression);
        MinicConstValue constant;
        const MinicTargetInfo *target = minic_target_info_for_architecture(
            MINIC_TARGET_ARCH_AARCH64);
        if (out && arg && mask &&
            out->value_category == MINIC_VALUE_LVALUE &&
            !minic_type_is_const(out->type) &&
            minic_type_is_pointer(out->type) &&
            minic_type_is_pointer(arg->type) &&
            minic_type_is_integer(mask->type) &&
            minic_const_eval_integer(program, target,
                                    src->inputs[1].expression, &constant) &&
            constant.bits == (UINT64_C(1) << 55)) {
            contract->operand_count = 3U;
            contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
            contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
            contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
            return true;
        }
    }
    if (!program || !src || !contract || !src->is_volatile ||
        src->is_goto || !src->outputs || !src->inputs ||
        src->output_count != 1U || src->input_count != 2U ||
        src->clobber_count != 1U || src->register_clobber_count != 1U ||
        src->has_memory_clobber || !src->register_clobbers ||
        !src->register_clobbers[0].name ||
        src->register_clobbers[0].name_length != 2U ||
        memcmp(src->register_clobbers[0].name, "cc", 2U) != 0 ||
        !mask_template(src->template_text, src->template_length) ||
        !constraint(&src->outputs[0], "=r") ||
        !constraint(&src->inputs[0], "r") ||
        !constraint(&src->inputs[1], "Ir"))
        return false;
    for (i = 0U; i < 3U; ++i) {
        const MinicInlineAsmOperand *op = i == 0U
            ? &src->outputs[0] : &src->inputs[i-1U];
        expr = minic_c0_program_expression(program, op->expression);
        if (!expr || !minic_type_unqualified(expr->type, &plain) ||
            !minic_type_equal(plain, minic_type_unsigned_long()) ||
            (i == 0U && (expr->value_category != MINIC_VALUE_LVALUE ||
                         minic_type_is_const(expr->type))))
            return false;
    }
    contract->operand_count = 3U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    return true;
}
static bool aarch64_linux_percpu_write_alternative_emit(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreFunction *fn;
    const MinicCoreStructuredInlineAsmOperand *operand;
    if (!out || !ctx || !ctx->fn || !ins || !info)
        return false;
    fn = ctx->fn;
    operand = ins->value.structured_inline_asm.operands;
    if (!operand ||
        !aarch64_linux_percpu_write_alternative_template(
            info->template_text, info->template_length) ||
        !info->is_volatile || info->is_goto ||
        !info->has_memory_clobber || info->register_clobber_count ||
        ctx->dynamic_frame ||
        ins->value.structured_inline_asm.operand_count != 1U ||
        operand[0].operand_index != 0U ||
        operand[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
        operand[0].has_fixed_register_binding || operand[0].early_clobber ||
        operand[0].value >= fn->value_count ||
        !aarch64_linux_structured_spill_slot_valid(operand[0].value) ||
        !minic_type_equal(fn->values[operand[0].value].type,
                          minic_type_unsigned_long()))
        return false;
    /* Both alternatives consume x9, a caller-clobbered AAPCS64 register.
     * Feature 47 owns the EL1-to-EL2 patch and its 12-byte relative table.
     * The paired .orgs reject any replacement instruction-size mismatch. */
    return fprintf(out,
        "  ldr x9, [sp, #%zu]\n"
        "  .if 1 == 1\n"
        "661:\n"
        "  msr tpidr_el1, x9\n"
        "662:\n"
        "  .pushsection .altinstructions,\"a\"\n"
        "  .word 661b - .\n"
        "  .word 663f - .\n"
        "  .hword 47\n"
        "  .byte 662b - 661b\n"
        "  .byte 664f - 663f\n"
        "  .popsection\n"
        "  .subsection 1\n"
        "663:\n"
        "  msr tpidr_el2, x9\n"
        "664:\n"
        "  .org . - (664b-663b) + (662b-661b)\n"
        "  .org . - (662b-661b) + (664b-663b)\n"
        "  .previous\n"
        "  .endif\n",
        (size_t)operand[0].value * 8U) >= 0;
}

static bool aarch64_linux_lse_emit(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info,
    const Aarch64LseForm *form) {
    static const char *const order[] = {"", "a", "l", "al"};
    const MinicCoreFunction *fn = ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *ops =
        ins->value.structured_inline_asm.operands;
    const size_t count = form->kind == AARCH64_LSE_STORE ? 2U : 3U;
    const char *reg = form->width == 8U ? "x" : "w";
    const char *width_suffix = form->width == 1U ? "b" :
                               form->width == 2U ? "h" : "";
    const char *load = form->width == 1U ? "ldrb w9" :
                       form->width == 2U ? "ldrh w9" :
                       form->width == 4U ? "ldr w9" : "ldr x9";
    const char *store = form->width == 1U ? "strb w9" :
                        form->width == 2U ? "strh w9" :
                        form->width == 4U ? "str w9" : "str x9";
    MinicType pointee;
    size_t i;
    if (!out || !fn || !info || !form || !ops || !info->is_volatile ||
        info->is_goto || !info->has_memory_clobber ||
        info->register_clobber_count || ctx->dynamic_frame ||
        ins->value.structured_inline_asm.operand_count != count ||
        form->mode >= sizeof(order) / sizeof(order[0]))
        return false;
    for (i = 0U; i < count; ++i) {
        const MinicCoreStructuredInlineAsmOperand *op = &ops[i];
        const MinicCoreStructuredInlineAsmOperandKind kind = i == 0U ?
            MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE :
            i == 1U && form->kind == AARCH64_LSE_FETCH ?
                MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
            i == 1U && form->kind == AARCH64_LSE_CAS ?
                MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE :
                MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        if (op->operand_index != i || op->kind != kind ||
            op->has_fixed_register_binding || op->early_clobber ||
            op->value >= fn->value_count ||
            !aarch64_linux_structured_spill_slot_valid(op->value) ||
            (i != count - 1U &&
             (!minic_type_pointee(fn->values[op->value].type, &pointee) ||
              !aarch64_stlr_type(pointee, form->width))) ||
            (i == count - 1U &&
             !aarch64_stlr_type(fn->values[op->value].type, form->width)))
            return false;
    }
    if (fprintf(out, "  .arch_extension lse\n  ldr x10, [sp, #%zu]\n",
                (size_t)ops[0].value * 8U) < 0)
        return false;
    if (form->kind == AARCH64_LSE_STORE) {
        return fprintf(out,
            "  ldr x11, [sp, #%zu]\n"
            "  %s %s11, [x10]\n",
            (size_t)ops[1].value * 8U, form->mnemonic, reg) >= 0;
    }
    if (form->kind == AARCH64_LSE_FETCH) {
        return fprintf(out,
            "  ldr x11, [sp, #%zu]\n"
            "  %s%s %s11, %s9, [x10]\n"
            "  ldr x12, [sp, #%zu]\n"
            "  str %s9, [x12]\n",
            (size_t)ops[2].value * 8U,
            form->mnemonic, order[form->mode], reg, reg,
            (size_t)ops[1].value * 8U, reg) >= 0;
    }
    /* GNU +r is an observable read/write local; the Core binding is its
     * address, unlike a write-only =r destination. CAS returns the observed
     * old value even when the comparison fails. */
    return fprintf(out,
        "  ldr x12, [sp, #%zu]\n"
        "  %s, [x12]\n"
        "  ldr x11, [sp, #%zu]\n"
        "  cas%s%s %s9, %s11, [x10]\n"
        "  %s, [x12]\n",
        (size_t)ops[1].value * 8U, load,
        (size_t)ops[2].value * 8U,
        order[form->mode], width_suffix, reg, reg, store) >= 0;
}

bool minic_aarch64_emit_qualified_structured_asm(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins) {
    const MinicCoreFunction *fn;
    const MinicCoreInlineAsm *info;
    const MinicCoreStructuredInlineAsmOperand *b;
    size_t i;
    if (!out || !ctx || !ctx->fn || !ins || ctx->dynamic_frame ||
        ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        !minic_type_is_void(ins->type))
        return false;
    fn = ctx->fn;
    if (ins->value.structured_inline_asm.inline_asm_id >= fn->inline_asm_count)
        return false;
    info = &fn->inline_asms[ins->value.structured_inline_asm.inline_asm_id];
    if (aarch64_linux_hyp_spin_form(
            info->template_text, info->template_length) !=
        AARCH64_LINUX_HYP_SPIN_NONE)
        return aarch64_linux_hyp_spin_emit(out, ctx, ins, info);
    if (info->template_length == sizeof("rbit %w0, %w1") - 1U &&
        memcmp(info->template_text, "rbit %w0, %w1",
               sizeof("rbit %w0, %w1") - 1U) == 0) {
        const MinicCoreStructuredInlineAsmOperand *op =
            ins->value.structured_inline_asm.operands;
        MinicType pointee;
        if (!info->is_volatile || info->is_goto ||
            info->register_clobber_count || info->has_memory_clobber ||
            ins->value.structured_inline_asm.operand_count != 2U ||
            op[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT ||
            op[1].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
            op[0].operand_index != 0U || op[1].operand_index != 1U ||
            op[0].early_clobber || op[1].early_clobber ||
            op[0].has_fixed_register_binding ||
            op[1].has_fixed_register_binding ||
            op[0].value >= fn->value_count || op[1].value >= fn->value_count ||
            !aarch64_linux_structured_spill_slot_valid(op[0].value) ||
            !aarch64_linux_structured_spill_slot_valid(op[1].value) ||
            !minic_type_pointee(fn->values[op[0].value].type, &pointee) ||
            !aarch64_stlr_type(pointee, 4U) ||
            !aarch64_stlr_type(fn->values[op[1].value].type, 4U))
            return false;
        return fprintf(out,
            "  ldr w9, [sp, #%zu]\n"
            "  rbit w9, w9\n"
            "  ldr x10, [sp, #%zu]\n"
            "  str w9, [x10]\n",
            (size_t)op[1].value * 8U,
            (size_t)op[0].value * 8U) >= 0;
    }
    {
        Aarch64LseForm lse;
        if (aarch64_lse_form(info->template_text, info->template_length,
                             true, &lse))
            return aarch64_linux_lse_emit(out, ctx, ins, info, &lse);
    }
    if (aarch64_linux_kvm_at_fixup_template(
            info->template_text, info->template_length))
        return aarch64_linux_kvm_at_fixup_emit(out, ctx, ins, info);
    {
        Aarch64LinuxVheSysregAlternative vhe;
        if (aarch64_linux_vhe_sysreg_alternative_form(
                info->template_text, info->template_length, &vhe))
            return aarch64_linux_vhe_sysreg_alternative_emit(
                out, ctx, ins, info, &vhe);
    }
    if (aarch64_linux_at_s1e1r_template(
            info->template_text, info->template_length))
        return aarch64_linux_at_s1e1r_emit(out, ctx, ins, info);
    {
        const Aarch64LinuxBarrierAlternative *alternative =
            aarch64_linux_barrier_alternative(
                info->template_text, info->template_length);
        if (alternative) {
            if (!info->is_volatile || info->is_goto ||
                !info->has_memory_clobber || info->register_clobber_count ||
                ins->value.structured_inline_asm.operand_count != 0U)
                return false;
            return aarch64_linux_barrier_alternative_emit(out, alternative);
        }
    }
    if (aarch64_linux_percpu_write_alternative_template(
            info->template_text, info->template_length))
        return aarch64_linux_percpu_write_alternative_emit(out, ctx, ins, info);
    if (aarch64_linux_qcom_return_stack_form(info->template_text,
                                            info->template_length))
        return aarch64_linux_qcom_return_stack_emit(out, ctx, ins, info);
    {
        const unsigned int rng = aarch64_linux_rng_sysreg_form(
            info->template_text, info->template_length);
        if (rng != 0U)
            return aarch64_linux_rng_sysreg_emit(out, ctx, ins, info, rng);
    }
    if (aarch64_linux_counter_ordering_form(info->template_text,
                                            info->template_length))
        return aarch64_linux_counter_ordering_emit(out, ctx, ins, info);
    {
        const unsigned int timer = aarch64_linux_timer_alternative_form(
            info->template_text, info->template_length);
        if (timer != 0U)
            return aarch64_linux_timer_alternative_emit(out, ctx, ins, info, timer);
    }
    if (aarch64_linux_xpaclri_form(info->template_text, info->template_length))
        return aarch64_linux_xpaclri_emit(out, ctx, ins, info);
    {
        const unsigned int smccc = aarch64_linux_smccc_form(
            info->template_text, info->template_length);
        if (smccc != 0U)
            return aarch64_linux_smccc_emit(out, ctx, ins, info, smccc);
    }
    if (aarch64_linux_svc_form(info->template_text, info->template_length))
        return aarch64_linux_svc_emit(out, ctx, ins, info);
    if (aarch64_linux_atomic_dec_positive_form(
            info->template_text, info->template_length))
        return aarch64_linux_atomic_dec_positive_emit(out, ctx, ins, info);
    {
        const unsigned int cmpxchg128 = aarch64_linux_cmpxchg128_form(
            info->template_text, info->template_length);
        if (cmpxchg128 != 0U)
            return aarch64_linux_cmpxchg128_emit(out, ctx, ins, info,
                                                  cmpxchg128);
    }
    {
        const unsigned int futex = aarch64_linux_futex_form(
            info->template_text, info->template_length);
        if (futex != 0U)
            return aarch64_linux_futex_emit(out, ctx, ins, info, futex);
    }
    if (aarch64_linux_disabled_hw_pan_form(info->template_text,
                                           info->template_length))
        return aarch64_linux_disabled_hw_pan_emit(out, ins, info);
    {
        const unsigned int wfxt = aarch64_linux_wfxt_form(
            info->template_text, info->template_length);
        if (wfxt != 0U)
            return aarch64_linux_wfxt_emit(out, ctx, ins, info, wfxt);
    }
    {
        Aarch64UaccessForm uaccess;
        if (aarch64_linux_uaccess_form(info->template_text,
                                      info->template_length, &uaccess))
            return aarch64_linux_uaccess_emit(out, ctx, ins, info, &uaccess);
    }
    if (aarch64_linux_zeropad_form(info->template_text,
                                  info->template_length))
        return aarch64_linux_zeropad_emit(out, ctx, ins, info);
    if (aarch64_linux_user_cache_fault_form(
            info->template_text, info->template_length))
        return aarch64_linux_user_cache_fault_emit(out, ctx, ins, info);
    if (aarch64_linux_cache_op_form(info->template_text,
                                   info->template_length))
        return aarch64_linux_cache_op_emit(out, ctx, ins, info);
    if (aarch64_linux_encoded_sysreg_write_form(info->template_text,
                                               info->template_length))
        return aarch64_linux_encoded_sysreg_write_emit(out, ctx, ins, info);
    if (aarch64_linux_tlbi_form(
            info->template_text,info->template_length))
        return aarch64_linux_tlbi_emit(out,ctx,ins,info);
    if (ins->value.structured_inline_asm.operand_count==3U &&
        aarch64_linux_cmpwait_form(info->template_text,info->template_length)) {
        const unsigned int width=aarch64_linux_cmpwait_form(
            info->template_text,info->template_length);
        const MinicCoreStructuredInlineAsmOperand *ops=
            ins->value.structured_inline_asm.operands;
        MinicType pointee;
        size_t k;
        /* The Core effect conservatively includes +Q RMW memory ordering. */
        if (!ops || !info->is_volatile || info->is_goto ||
            ctx->dynamic_frame || info->register_clobber_count ||
            ins->result != MINIC_CORE_VALUE_INVALID)
            return false;
        for (k=0U;k<3U;++k)
            if (ops[k].operand_index!=k ||
                ops[k].has_fixed_register_binding ||
                ops[k].value>=fn->value_count ||
                !aarch64_linux_structured_spill_slot_valid(ops[k].value) ||
                ops[k].kind!=(k==0U?
                    MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
                    k==1U?MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE:
                    MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT) ||
                (k!=0U && ops[k].early_clobber)) return false;
        if (!minic_type_pointee(fn->values[ops[0].value].type,&pointee)||
            !aarch64_linux_sysreg_u64_type(pointee) ||
            !minic_type_pointee(fn->values[ops[1].value].type,&pointee)||
            !aarch64_stlr_type(pointee,width) ||
            !aarch64_linux_sysreg_u64_type(fn->values[ops[2].value].type))
            return false;
        return fprintf(out,
            "  ldr x10, [sp, #%zu]\n"
            "  ldr x11, [sp, #%zu]\n"
            "  sevl\n"
            "  wfe\n"
            "  ldxr%s %s9, [x10]\n"
            "  eor %s9, %s9, %s11\n"
            "  cbnz %s9, 1f\n"
            "  wfe\n"
            "1:\n"
            "  ldr x12, [sp, #%zu]\n"
            "  str x9, [x12]\n",
            (size_t)ops[1].value*8U,
            (size_t)ops[2].value*8U,
            width==1U?"b":width==2U?"h":"",
            width==8U?"x":"w",width==8U?"x":"w",
            width==8U?"x":"w",width==8U?"x":"w",
            width==8U?"x":"w",(size_t)ops[0].value*8U)>=0;
    }
    if (ins->value.structured_inline_asm.operand_count == 0U) {
        const char *barrier = aarch64_barrier_template(
            info->template_text, info->template_length);
        if (!barrier || !info->is_volatile || info->is_goto ||
            info->register_clobber_count != 0U)
            return false;
        return fprintf(out, "  %s\n", barrier) >= 0;
    }
    if (ins->value.structured_inline_asm.operand_count == 1U &&
        aarch64_linux_encoded_sysreg_form(
            info->template_text,info->template_length))
        return aarch64_linux_encoded_sysreg_emit(out,ctx,ins,info);
    if (ins->value.structured_inline_asm.operand_count == 1U) {
        const MinicCoreStructuredInlineAsmOperand *operand =
            ins->value.structured_inline_asm.operands;
        const char *reg = aarch64_linux_mrs_register(
            info->template_text, info->template_length);
        MinicType pointee;
        if (operand != NULL &&
            operand[0].kind == MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT &&
            aarch64_linux_prefetch_template(
                info->template_text, info->template_length)) {
            const char *hint = aarch64_linux_prefetch_template(
                info->template_text, info->template_length);
            if (!info->is_volatile || info->is_goto ||
                info->has_memory_clobber || info->register_clobber_count ||
                operand[0].operand_index != 0U ||
                operand[0].has_fixed_register_binding || operand[0].early_clobber ||
                operand[0].value >= fn->value_count ||
                !aarch64_linux_structured_spill_slot_valid(operand[0].value) ||
                !minic_type_is_pointer(fn->values[operand[0].value].type))
                return false;
            return fprintf(out,
                "  ldr x9, [sp, #%zu]\n"
                "  prfm %s, [x9]\n",
                (size_t)operand[0].value * 8U, hint) >= 0;
        }
        if (operand != NULL &&
            operand[0].kind == MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT &&
            aarch64_linux_msr_register(info->template_text, info->template_length)) {
            if (!info->is_volatile || info->is_goto ||
                info->has_memory_clobber || info->register_clobber_count ||
                operand[0].operand_index != 0U ||
                operand[0].has_fixed_register_binding || operand[0].early_clobber ||
                operand[0].value >= fn->value_count ||
                !aarch64_linux_structured_spill_slot_valid(operand[0].value) ||
                (!minic_type_equal(fn->values[operand[0].value].type,
                                   minic_type_unsigned_long()) &&
                 !minic_type_equal(fn->values[operand[0].value].type,
                                   minic_type_unsigned_long_long())))
                return false;
            /* ABI scratch x9 holds the original rZ operand. These
             * privileged instructions are GNU-AS-only verified, never
             * executed under unprivileged QEMU user mode. */
            return fprintf(out, "  ldr x9, [sp, #%zu]\n"
                                "  msr %s, x9\n",
                           (size_t)operand[0].value * 8U,
                           aarch64_linux_msr_register(
                               info->template_text, info->template_length)) >= 0;
        }
        if (operand != NULL &&
            operand[0].kind == MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT) {
            const MinicCoreInstruction *definition;
            MinicCoreInstructionId def_id;
            int64_t imm;
            if (!aarch64_linux_brk_template(
                    info->template_text, info->template_length) ||
                info->is_goto || info->has_memory_clobber ||
                info->register_clobber_count != 0U ||
                operand[0].operand_index != 0U ||
                operand[0].has_fixed_register_binding ||
                operand[0].early_clobber ||
                operand[0].value >= fn->value_count)
                return false;
            def_id = fn->values[operand[0].value].definition;
            if (def_id >= fn->instruction_count) return false;
            definition = &fn->instructions[def_id];
            if (definition->kind != MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT ||
                definition->result != operand[0].value)
                return false;
            imm = definition->value.integer_value;
            if (!aarch64_linux_brk_value(imm)) return false;
            /* Never execute this in QEMU-user validation: the original
             * Linux BUG instruction intentionally enters a trap. */
            return fprintf(out, "  brk #%lld\n", (long long)imm) >= 0;
        }
        if (reg == NULL || info->is_goto ||
            info->has_memory_clobber || info->register_clobber_count != 0U ||
            operand == NULL || operand[0].operand_index != 0U ||
            operand[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT ||
            operand[0].early_clobber || operand[0].has_fixed_register_binding ||
            operand[0].value >= fn->value_count ||
            !aarch64_linux_structured_spill_slot_valid(operand[0].value) ||
            !minic_type_pointee(fn->values[operand[0].value].type, &pointee) ||
            !aarch64_linux_sysreg_u64_type(pointee))
            return false;
        /* The output SSA operand is an address. The backend reads the real
         * privileged register into an internal GPR and stores the exact LP64
         * word back to the existing Core object. No external symbol/alias. */
        return fprintf(out, "  mrs x9, %s\n"
                            "  ldr x10, [sp, #%zu]\n"
                            "  str x9, [x10]\n",
                       reg, (size_t)operand[0].value * 8U) >= 0;
    }
    if (ins->value.structured_inline_asm.operand_count == 2U &&
        aarch64_linux_io_dependency_form(
            info->template_text,info->template_length))
        return aarch64_linux_io_dependency_emit(out,ctx,ins,info);
    if (ins->value.structured_inline_asm.operand_count == 2U &&
        aarch64_raw_io_read(info->template_text, info->template_length)) {
        const Aarch64RawIoRead *d = aarch64_raw_io_read(
            info->template_text, info->template_length);
        const MinicCoreStructuredInlineAsmOperand *ops =
            ins->value.structured_inline_asm.operands;
        MinicType output_type;
        if (!d || !info->is_volatile || info->is_goto ||
            info->has_memory_clobber || info->register_clobber_count ||
            ops[0].operand_index != 0U ||
            ops[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT ||
            ops[1].operand_index != 1U ||
            ops[1].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
            ops[0].early_clobber || ops[1].early_clobber ||
            ops[0].has_fixed_register_binding || ops[1].has_fixed_register_binding ||
            ops[0].value >= fn->value_count || ops[1].value >= fn->value_count ||
            !aarch64_linux_structured_spill_slot_valid(ops[0].value) || !aarch64_linux_structured_spill_slot_valid(ops[1].value) ||
            !minic_type_pointee(fn->values[ops[0].value].type, &output_type) ||
            !aarch64_stlr_type(output_type, d->width) ||
            !minic_type_is_pointer(fn->values[ops[1].value].type))
            return false;
        /* x9/x10/x11 are AAPCS64 caller-scratch. GNU linker consumes the
         * preserved 2×PREL32, 12-byte .altinstructions feature-91 record.
         * User QEMU runs LDR; Linux runtime can replace it with LDAR. */
        return fprintf(out,
            "  ldr x10, [sp, #%zu]\n"
            "  .if 1 == 1\n"
            "661:\n  %s\n"
            "662:\n"
            "  .pushsection .altinstructions,\"a\"\n"
            "  .word 661b - .\n"
            "  .word 663f - .\n"
            "  .hword 91\n"
            "  .byte 662b - 661b\n"
            "  .byte 664f - 663f\n"
            "  .popsection\n"
            "  .subsection 1\n"
            "663:\n  %s\n"
            "664:\n"
            "  .org . - (664b-663b) + (662b-661b)\n"
            "  .org . - (662b-661b) + (664b-663b)\n"
            "  .previous\n"
            "  .endif\n"
            "  ldr x11, [sp, #%zu]\n"
            "  %s\n",
            (size_t)ops[1].value * 8U, d->old_asm, d->new_asm,
            (size_t)ops[0].value * 8U, d->store_asm) >= 0;
    }
    if (ins->value.structured_inline_asm.operand_count == 2U &&
        aarch64_linux_percpu_alternative_template(
            info->template_text, info->template_length)) {
        const MinicCoreStructuredInlineAsmOperand *operands =
            ins->value.structured_inline_asm.operands;
        MinicType out_type, dependency;
        /* The Core target-ASM admission intentionally promotes ALL
         * observable structured output ASM to a conservative side effect,
         * even if original GNU source asm() lacks volatile. Hence Core
         * info->is_volatile records the effect, not source GNU spelling.
         * Source qualification above independently checked !src->is_volatile
         * and its entire .altinstructions template. Requiring !info here
         * previously rejected every genuine __kern_my_cpu_offset(). */
        if (!info->is_volatile || info->is_goto ||
            info->has_memory_clobber || info->register_clobber_count)
            return false;
        for (i = 0U; i < 2U; ++i)
            if (operands[i].operand_index != i ||
                operands[i].kind != (i == 0U ?
                    MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
                    MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT) ||
                operands[i].has_fixed_register_binding ||
                operands[i].early_clobber ||
                operands[i].value >= fn->value_count ||
                !aarch64_linux_structured_spill_slot_valid(operands[i].value))
                return false;
        if (!minic_type_pointee(fn->values[operands[0].value].type,
                                &out_type) ||
            !minic_type_pointee(fn->values[operands[1].value].type,
                                &dependency) ||
            !minic_type_equal(out_type, minic_type_unsigned_long()) ||
            !minic_type_is_integer(dependency))
            return false;
        /* One original 4-byte instruction at 661, one replacement at 663.
         * Relative relocations live in .altinstructions (12-byte entry):
         * first 2×rel32, feature=47 (u16), old/new lengths (u8 each).
         * Preserve original GNU .subsection 1 and dual .org size contracts.
         * x9 is AAPCS64 scratch and receives the finally selected MRS. */
        return fprintf(out,
            "  .if 1 == 1\n"
            "661:\n"
            "  mrs x9, tpidr_el1\n"
            "662:\n"
            "  .pushsection .altinstructions,\"a\"\n"
            "  .word 661b - .\n"
            "  .word 663f - .\n"
            "  .hword 47\n"
            "  .byte 662b - 661b\n"
            "  .byte 664f - 663f\n"
            "  .popsection\n"
            "  .subsection 1\n"
            "663:\n"
            "  mrs x9, tpidr_el2\n"
            "664:\n"
            "  .org . - (664b-663b) + (662b-661b)\n"
            "  .org . - (662b-661b) + (664b-663b)\n"
            "  .previous\n"
            "  .endif\n"
            "  ldr x10, [sp, #%zu]\n"
            "  str x9, [x10]\n",
            (size_t)operands[0].value * 8U) >= 0;
    }
    if (ins->value.structured_inline_asm.operand_count == 2U &&
        aarch64_raw_io_store(info->template_text, info->template_length)) {
        const Aarch64RawIoStore *store = aarch64_raw_io_store(
            info->template_text, info->template_length);
        const MinicCoreStructuredInlineAsmOperand *ops =
            ins->value.structured_inline_asm.operands;
        if (!store || !info->is_volatile || info->is_goto ||
            info->register_clobber_count != 0U ||
            /* The target contract marks the physical MMIO store as an
             * implicit effect even though GNU has no "memory" clobber. */
            !info->has_memory_clobber ||
            ops[0].operand_index != 0U ||
            ops[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
            ops[1].operand_index != 1U ||
            ops[1].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
            ops[0].has_fixed_register_binding || ops[1].has_fixed_register_binding ||
            ops[0].early_clobber || ops[1].early_clobber ||
            ops[0].value >= fn->value_count ||
            ops[1].value >= fn->value_count ||
            !aarch64_linux_structured_spill_slot_valid(ops[0].value) || !aarch64_linux_structured_spill_slot_valid(ops[1].value) ||
            !minic_type_is_pointer(fn->values[ops[1].value].type) ||
            !aarch64_stlr_type(fn->values[ops[0].value].type, store->width))
            return false;
        /* x9/x10 are AAPCS64 caller-saved. Preserve the actual
         * 8/16/32/64-bit STR hardware width, never a synthesized C store. */
        return fprintf(out,
            "  ldr x9, [sp, #%zu]\n"
            "  ldr x10, [sp, #%zu]\n"
            "  %s\n",
            (size_t)ops[0].value * 8U,
            (size_t)ops[1].value * 8U,
            store->instruction) >= 0;
    }

    /* Original GNU barrier.h acquire load: exact byte/half/word/64-bit
     * LDAR hardware and matching-width C output writes. */
    if(ins->value.structured_inline_asm.operand_count==2U &&
       aarch64_load_acquire_width(info->template_text,info->template_length)){
        const size_t w=aarch64_load_acquire_width(
            info->template_text,info->template_length);
        static const char *const rd[]={
            "ldarb w9, [x10]","ldarh w9, [x10]",
            "ldar w9, [x10]","ldar x9, [x10]"};
        static const char *const wr[]={"strb w9","strh w9","str w9","str x9"};
        const size_t n=w==1U?0U:w==2U?1U:w==4U?2U:3U;
        const MinicCoreStructuredInlineAsmOperand *ops=
            ins->value.structured_inline_asm.operands;
        MinicType dst_ty,mem_ty;
        if(!ops||!info->is_volatile||info->is_goto||
           !info->has_memory_clobber||info->register_clobber_count||
           ops[0].operand_index!=0U||ops[1].operand_index!=1U||
           ops[0].kind!=MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT||
           ops[1].kind!=MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT||
           ops[0].has_fixed_register_binding||ops[1].has_fixed_register_binding||
           ops[0].early_clobber||ops[1].early_clobber||
           ops[0].value>=fn->value_count||ops[1].value>=fn->value_count||
           !aarch64_linux_structured_spill_slot_valid(ops[0].value)||!aarch64_linux_structured_spill_slot_valid(ops[1].value)||
           !minic_type_pointee(fn->values[ops[0].value].type,&dst_ty)||
           !minic_type_pointee(fn->values[ops[1].value].type,&mem_ty)||
           !aarch64_stlr_type(dst_ty,w)||!aarch64_stlr_memory_type(mem_ty,w))
            return false;
        return fprintf(out,
            "  ldr x10, [sp, #%zu]\n"
            "  %s\n"
            "  ldr x11, [sp, #%zu]\n"
            "  %s, [x11]\n",
            (size_t)ops[1].value*8U,rd[n],
            (size_t)ops[0].value*8U,wr[n])>=0;
    }

    if (ins->value.structured_inline_asm.operand_count == 2U) {
        b = ins->value.structured_inline_asm.operands;
        static const char *const op[] = {
            NULL, "stlrb w9, [x10]", "stlrh w9, [x10]", NULL,
            "stlr w9, [x10]", NULL, NULL, NULL, "stlr x9, [x10]"
        };
        const size_t width = aarch64_stlr_width(
            info->template_text, info->template_length);
        MinicType pointee;
        if (!width || !info->is_volatile || info->is_goto ||
            !info->has_memory_clobber || info->register_clobber_count ||
            b[0].operand_index != 0U ||
            b[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT ||
            b[0].has_fixed_register_binding || b[0].early_clobber ||
            b[1].operand_index != 1U ||
            b[1].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
            b[1].has_fixed_register_binding || b[1].early_clobber ||
            b[0].value >= fn->value_count || b[1].value >= fn->value_count ||
            !aarch64_linux_structured_spill_slot_valid(b[0].value) || !aarch64_linux_structured_spill_slot_valid(b[1].value) ||
            !minic_type_pointee(fn->values[b[0].value].type, &pointee) ||
            !aarch64_stlr_memory_type(pointee, width) ||
            !aarch64_stlr_type(fn->values[b[1].value].type, width))
            return false;
        /* LP64 Core SSA slots SP-relative; x9/x10 are AAPCS64 caller-saved.
         * STLR/B/H provides release ordering, never a compiler barrier. */
        return fprintf(out, "  ldr x10, [sp, #%zu]\n"
                            "  ldr x9, [sp, #%zu]\n"
                            "  %s\n",
                       (size_t)b[0].value * 8U,
                       (size_t)b[1].value * 8U, op[width]) >= 0;
    }
    /* Same C status/tmp/+Q/input roles as our older 4-role atomic
     * family, but exact original per-CPU templates have no PRFM, no
     * ordering fence, and support narrow unsigned byte/halfword STXR.
     * Prefer the complete ISA decoder before any operand-count fallback. */
    if(ins->value.structured_inline_asm.operand_count==4U) {
        Aarch64PerCpuForm p;
        if(aarch64_linux_percpu_rmw_form(
                info->template_text,info->template_length,&p)) {
            const MinicCoreStructuredInlineAsmOperand *ops=
                ins->value.structured_inline_asm.operands;
            MinicType pointee;
            const char *suffix=p.width==1U?"b":p.width==2U?"h":"";
            const char *reg=p.width==8U?"x":"w";
            const char *store=p.width==1U?"strb w9":
                              p.width==2U?"strh w9":
                              p.width==4U?"str w9":"str x9";
            if(!info->is_volatile||info->is_goto||
               !info->has_memory_clobber||info->register_clobber_count)
                return false;
            for(i=0U;i<4U;++i) {
                const MinicCoreStructuredInlineAsmOperand *op=&ops[i];
                if(op->operand_index!=i||op->early_clobber||
                   op->has_fixed_register_binding||op->value>=fn->value_count||
                   !aarch64_linux_structured_spill_slot_valid(op->value)||
                   op->kind!=(i==3U?
                       MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT:
                       i==2U?
                       MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE:
                       MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT))
                    return false;
                if(i<3U) {
                    if(!minic_type_pointee(fn->values[op->value].type,&pointee)||
                       !aarch64_stlr_type(pointee,i==0U?4U:p.width))
                        return false;
                } else if(!aarch64_stlr_type(
                    fn->values[op->value].type,p.width))
                    return false;
            }
            {
                char old_body[256], new_body[256];
                if (aarch64_linux_alt_lse_partition(
                        info->template_text, info->template_length,
                        old_body, sizeof(old_body),
                        new_body, sizeof(new_body))) {
                    const char *lse_op = strcmp(p.op->name, "add") == 0
                        ? (p.returns_new ? "ldadd" : "stadd")
                        : strcmp(p.op->name, "andnot") == 0
                            ? "stclr" : "stset";
                    if (fprintf(out,
                            "  ldr x10, [sp, #%zu]\n"
                            "  ldr x11, [sp, #%zu]\n"
                            "661:\n"
                            "1: ldxr%s %s9, [x10]\n"
                            "  %s %s9, %s9, %s11\n"
                            "  stxr%s w12, %s9, [x10]\n"
                            "  cbnz w12, 1b\n",
                            (size_t)ops[2].value * 8U,
                            (size_t)ops[3].value * 8U,
                            suffix, reg, p.op->alu, reg, reg, reg,
                            suffix, reg) < 0 ||
                        !aarch64_linux_alt_emit_middle(out, 33U, true))
                        return false;
                    if (p.returns_new) {
                        if (fprintf(out,
                            "  %s%s %s11, %s9, [x10]\n"
                            "  add %s9, %s9, %s11\n"
                            "  nop\n  nop\n",
                            lse_op, suffix, reg, reg, reg, reg, reg) < 0)
                            return false;
                    } else if (fprintf(out,
                        "  %s%s %s11, [x10]\n"
                        "  nop\n  nop\n  nop\n",
                        lse_op, suffix, reg) < 0) return false;
                    if (!aarch64_linux_alt_emit_end(out)) return false;
                    return fprintf(out,
                        "  mov w12, #0\n"
                        "  ldr x13, [sp, #%zu]\n"
                        "  %s, [x13]\n"
                        "  ldr x13, [sp, #%zu]\n"
                        "  str w12, [x13]\n",
                        (size_t)ops[1].value * 8U, store,
                        (size_t)ops[0].value * 8U) >= 0;
                }
            }
            /* aarch64 LL/SC status failure retries; status written as u32
             * and tmp/ret narrowed EXACTLY, not widened to a 64-bit store. */
            return fprintf(out,
                "  ldr x10, [sp, #%zu]\n"
                "  ldr x11, [sp, #%zu]\n"
                "1:\n"
                "  ldxr%s %s9, [x10]\n"
                "  %s %s9, %s9, %s11\n"
                "  stxr%s w12, %s9, [x10]\n"
                "  cbnz w12, 1b\n"
                "  ldr x13, [sp, #%zu]\n"
                "  %s, [x13]\n"
                "  ldr x13, [sp, #%zu]\n"
                "  str w12, [x13]\n",
                (size_t)ops[2].value*8U,
                (size_t)ops[3].value*8U,
                suffix,reg,
                p.op->alu,reg,reg,reg,
                suffix,reg,
                (size_t)ops[1].value*8U,store,
                (size_t)ops[0].value*8U)>=0;
        }
    }
    if (ins->value.structured_inline_asm.operand_count == 4U) {
        Aarch64AtomicForm ret;
        if (aarch64_atomic_return_form(
                info->template_text, info->template_length, &ret)) {
            const MinicCoreStructuredInlineAsmOperand *ops =
                ins->value.structured_inline_asm.operands;
            MinicType pointee;
            const char *reg = ret.width == 8U ? "x" : "w";
            if (!info->is_volatile || info->is_goto ||
                info->register_clobber_count || !info->has_memory_clobber)
                return false;
            for (i = 0U; i < 4U; ++i) {
                const MinicCoreStructuredInlineAsmOperand *op = &ops[i];
                if (op->operand_index != i || op->early_clobber ||
                    op->has_fixed_register_binding ||
                    op->value >= fn->value_count ||
                    !aarch64_linux_structured_spill_slot_valid(op->value) ||
                    op->kind != (i == 3U ?
                        MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT :
                        i == 2U ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE :
                                  MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT))
                    return false;
                if (i < 3U) {
                    if (!minic_type_pointee(fn->values[op->value].type, &pointee) ||
                        !aarch64_stlr_type(
                            pointee, i == 1U ? 8U : ret.width))
                        return false;
                } else if (!aarch64_stlr_type(
                    fn->values[op->value].type, ret.width)) return false;
            }
            /* Register x9 becomes the new, post-RMW value. The memory
             * update is a real exclusive STXR retry, and source memory
             * ordering maps to hardware LDAXR/STLXR plus full DMB ISH. */
            return fprintf(out,
                "  ldr x10, [sp, #%zu]\n"
                "  ldr x11, [sp, #%zu]\n"
                "  prfm pstl1strm, [x10]\n"
                "1:\n"
                "  %s %s9, [x10]\n"
                "  %s %s9, %s9, %s11\n"
                "  %s w12, %s9, [x10]\n"
                "  cbnz w12, 1b\n"
                "%s"
                "  ldr x13, [sp, #%zu]\n"
                "  str %s9, [x13]\n"
                "  ldr x13, [sp, #%zu]\n"
                "  str x12, [x13]\n",
                (size_t)ops[2].value * 8U,
                (size_t)ops[3].value * 8U,
                aarch64_atomic_load[ret.mode], reg,
                ret.op->alu, reg, reg, reg,
                aarch64_atomic_store[ret.mode], reg,
                ret.mode == 0U ? "  dmb ish\n" : "",
                (size_t)ops[0].value * 8U, reg,
                (size_t)ops[1].value * 8U) >= 0;
        }
    }
    /* Genuine 1/2/4/8-byte AArch64 XCHG: preserve the exclusive retry
     * and its acquire/release ordering, returning the old value. */
    if (ins->value.structured_inline_asm.operand_count == 4U) {
        Aarch64XchgForm xchg;
        if (aarch64_linux_xchg_form(
                info->template_text, info->template_length, &xchg)) {
            const MinicCoreStructuredInlineAsmOperand *ops =
                ins->value.structured_inline_asm.operands;
            MinicType pointee;
            const char *suffix = xchg.width == 1U ? "b" :
                                 xchg.width == 2U ? "h" : "";
            const char *reg = xchg.width == 8U ? "x" : "w";
            const char *old_store = xchg.width == 1U ? "strb w9" :
                                    xchg.width == 2U ? "strh w9" :
                                    xchg.width == 4U ? "str w9" : "str x9";
            if (!info->is_volatile || info->is_goto ||
                info->register_clobber_count || !info->has_memory_clobber)
                return false;
            for (i = 0U; i < 4U; ++i) {
                const MinicCoreStructuredInlineAsmOperand *op = &ops[i];
                if (op->operand_index != i || op->early_clobber ||
                    op->has_fixed_register_binding || op->value >= fn->value_count ||
                    !aarch64_linux_structured_spill_slot_valid(op->value) ||
                    op->kind != (i == 3U ?
                        MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT :
                        i == 2U ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE :
                                  MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT))
                    return false;
                if (i < 3U) {
                    if (!minic_type_pointee(fn->values[op->value].type, &pointee) ||
                        !aarch64_stlr_type(pointee, i == 1U ? 8U : xchg.width))
                        return false;
                } else if (!aarch64_stlr_type(
                    fn->values[op->value].type, xchg.width))
                    return false;
            }
            {
                static const char *const order[] = {"", "a", "l", "al"};
                char old_body[256], new_body[256];
                if (aarch64_linux_alt_lse_partition(
                        info->template_text, info->template_length,
                        old_body, sizeof(old_body),
                        new_body, sizeof(new_body))) {
                    if (fprintf(out,
                        "  ldr x10, [sp, #%zu]\n"
                        "  ldr x11, [sp, #%zu]\n"
                        "661:\n"
                        "  prfm pstl1strm, [x10]\n"
                        "1: %sxr%s %s9, [x10]\n"
                        "  %sxr%s w12, %s11, [x10]\n"
                        "  cbnz w12, 1b\n"
                        "%s",
                        (size_t)ops[2].value * 8U,
                        (size_t)ops[3].value * 8U,
                        aarch64_xchg_ld[xchg.mode], suffix, reg,
                        aarch64_xchg_st[xchg.mode], suffix, reg,
                        xchg.mode == 3U ? "  dmb ish\n" : "") < 0 ||
                        !aarch64_linux_alt_emit_middle(out, 33U, true))
                        return false;
                    if (fprintf(out,
                        "  swp%s%s %s11, %s9, [x10]\n"
                        "  nop\n  nop\n  nop\n"
                        "%s",
                        order[xchg.mode], suffix, reg, reg,
                        xchg.mode == 3U ? "  nop\n" : "") < 0 ||
                        !aarch64_linux_alt_emit_end(out))
                        return false;
                    return fprintf(out,
                        "  mov w12, #0\n"
                        "  ldr x13, [sp, #%zu]\n"
                        "  %s, [x13]\n"
                        "  ldr x13, [sp, #%zu]\n"
                        "  str x12, [x13]\n",
                        (size_t)ops[0].value * 8U, old_store,
                        (size_t)ops[1].value * 8U) >= 0;
                }
            }
            return fprintf(out,
                "  ldr x10, [sp, #%zu]\n"
                "  ldr x11, [sp, #%zu]\n"
                "  prfm pstl1strm, [x10]\n"
                "1:\n"
                "  %sxr%s %s9, [x10]\n"
                "  %sxr%s w12, %s11, [x10]\n"
                "  cbnz w12, 1b\n"
                "%s"
                "  ldr x13, [sp, #%zu]\n"
                "  %s, [x13]\n"
                "  ldr x13, [sp, #%zu]\n"
                "  str x12, [x13]\n",
                (size_t)ops[2].value * 8U,
                (size_t)ops[3].value * 8U,
                aarch64_xchg_ld[xchg.mode], suffix, reg,
                aarch64_xchg_st[xchg.mode], suffix, reg,
                xchg.mode == 3U ? "  dmb ish\n" : "",
                (size_t)ops[0].value * 8U, old_store,
                (size_t)ops[1].value * 8U) >= 0;
        }
    }
    if (ins->value.structured_inline_asm.operand_count == 5U &&
        aarch64_linux_cmpxchg_form(
            info->template_text, info->template_length, true)) {
        static const char *const ld[] = {"ldxr", "ldaxr", "ldxr", "ldxr"};
        static const char *const st[] = {"stxr", "stxr", "stlxr", "stlxr"};
        const MinicCoreStructuredInlineAsmOperand *ops =
            ins->value.structured_inline_asm.operands;
        const unsigned int form = aarch64_linux_cmpxchg_form(
            info->template_text, info->template_length, true);
        const size_t width = aarch64_linux_cmpxchg_width(form);
        const unsigned int mode = (form - 1U) % 4U;
        const char *const suffix = width == 1U ? "b" : width == 2U ? "h" : "";
        const char *const old_store = width == 1U ? "strb w9" :
                                      width == 2U ? "strh w9" :
                                      width == 4U ? "str w9" : "str x9";
        MinicType pointee;
        if (!info->is_volatile || info->is_goto ||
            !info->has_memory_clobber || info->register_clobber_count)
            return false;
        for (i = 0U; i < 5U; ++i) {
            const size_t expected_width = i == 0U || i == 3U ? 8U : width;
            if (ops[i].operand_index != i || ops[i].early_clobber ||
                ops[i].has_fixed_register_binding ||
                ops[i].value >= fn->value_count ||
                !aarch64_linux_structured_spill_slot_valid(ops[i].value) ||
                ops[i].kind != (i < 2U ?
                    MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
                    i == 2U ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE :
                    MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT))
                return false;
            if (i < 3U) {
                if (!minic_type_pointee(fn->values[ops[i].value].type,
                                        &pointee) ||
                    !aarch64_stlr_type(pointee, expected_width))
                    return false;
            } else if (!aarch64_stlr_type(
                    fn->values[ops[i].value].type, expected_width)) {
                return false;
            }
        }
        /* Mismatch returns OLD without writing memory; a matching value
         * may be stored only via STXR, which is retried on hardware failure.
         * x9 old, x11 expected, x12 new, x13 compare / STXR status. */
        return fprintf(out,
            "  ldr x10, [sp, #%zu]\n"
            "  ldr x11, [sp, #%zu]\n"
            "  ldr x12, [sp, #%zu]\n"
            "  prfm pstl1strm, [x10]\n"
            "1:\n"
            "  %s%s %s9, [x10]\n"
            "  eor %s13, %s9, %s11\n"
            "  cbnz %s13, 2f\n"
            "  %s%s w13, %s12, [x10]\n"
            "  cbnz w13, 1b\n"
            "%s"
            "2:\n"
            "  ldr x14, [sp, #%zu]\n"
            "  %s, [x14]\n"
            "  ldr x14, [sp, #%zu]\n"
            "  str x13, [x14]\n",
            (size_t)ops[2].value * 8U,
            (size_t)ops[3].value * 8U,
            (size_t)ops[4].value * 8U,
            ld[mode], suffix, width == 8U ? "x" : "w",
            width == 8U ? "x" : "w",
            width == 8U ? "x" : "w",
            width == 8U ? "x" : "w",
            width == 8U ? "x" : "w",
            st[mode], suffix, width == 8U ? "x" : "w",
            mode == 3U ? "  dmb ish\n" : "",
            (size_t)ops[1].value * 8U,
            old_store,
            (size_t)ops[0].value * 8U) >= 0;
    }
    if (ins->value.structured_inline_asm.operand_count == 5U) {
        Aarch64AtomicForm atomic;
        const bool valid_form = aarch64_atomic_form(
            info->template_text, info->template_length, true, &atomic);
        const size_t width = valid_form ? atomic.width : 0U;
        const unsigned int mode = valid_form ? atomic.mode : 0U;
        const MinicCoreStructuredInlineAsmOperand *ops =
            ins->value.structured_inline_asm.operands;
        MinicType pointee;
        /* Core adds the memory effect to *every* checked +Q RMW role,
         * including GNU's syntactically unclobbered _relaxed variant.
         * Exact source clobber shape was validated by the target callback,
         * whereas the normalized Core effect must remain conservative. */
        if (!valid_form || !info->is_volatile || info->is_goto ||
            info->register_clobber_count || !info->has_memory_clobber)
            return false;
        for (i = 0U; i < 5U; ++i) {
            const MinicCoreStructuredInlineAsmOperand *op = &ops[i];
            if (op->operand_index != i || op->early_clobber ||
                op->has_fixed_register_binding ||
                op->value >= fn->value_count ||
                !aarch64_linux_structured_spill_slot_valid(op->value) ||
                op->kind != (i == 4U ?
                    MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT :
                    i == 3U ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE :
                    MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT))
                return false;
            if (i < 4U) {
                if (!minic_type_pointee(fn->values[op->value].type, &pointee) ||
                    !aarch64_stlr_type(pointee, i == 2U ? 8U : width))
                    return false;
            } else if (!aarch64_stlr_type(
                fn->values[op->value].type, width)) {
                return false;
            }
        }
        /* Five checked Core SSA operands, one atomic read-modify-write.
         * Preserve LDXR old value in x9, compute new in x13, publish through
         * STXR and write C result/temporary locals after successful retry.
         * Acquire/release use hardware LDAXR/STLXR, full uses DMB ISH. */
        return fprintf(out,
            "  ldr x10, [sp, #%zu]\n"
            "  ldr x11, [sp, #%zu]\n"
            "  prfm pstl1strm, [x10]\n"
            "1:\n"
            "  %s %s9, [x10]\n"
            "  %s %s13, %s9, %s11\n"
            "  %s w12, %s13, [x10]\n"
            "  cbnz w12, 1b\n"
            "%s"
            "  ldr x14, [sp, #%zu]\n"
            "  str %s9, [x14]\n"
            "  ldr x14, [sp, #%zu]\n"
            "  str %s13, [x14]\n"
            "  ldr x14, [sp, #%zu]\n"
            "  str x12, [x14]\n",
            (size_t)ops[3].value * 8U,
            (size_t)ops[4].value * 8U,
            aarch64_atomic_load[mode], width == 8U ? "x" : "w",
            atomic.op->alu, width == 8U ? "x" : "w",
            width == 8U ? "x" : "w",
            width == 8U ? "x" : "w",
            aarch64_atomic_store[mode], width == 8U ? "x" : "w",
            mode == 0U ? "  dmb ish\n" : "",
            (size_t)ops[0].value * 8U, width == 8U ? "x" : "w",
            (size_t)ops[1].value * 8U, width == 8U ? "x" : "w",
            (size_t)ops[2].value * 8U) >= 0;
    }
    if (ins->value.structured_inline_asm.operand_count == 4U) {
        Aarch64AtomicForm atomic;
        const bool valid_form = aarch64_atomic_form(
            info->template_text, info->template_length, false, &atomic);
        const size_t width = valid_form ? atomic.width : 0U;
        MinicType pointee;
        if (!valid_form || !info->is_volatile || info->is_goto ||
            !info->has_memory_clobber || info->register_clobber_count)
            return false;
        b = ins->value.structured_inline_asm.operands;
        for (i = 0U; i < 4U; ++i) {
            if (b[i].operand_index != i || b[i].early_clobber ||
                b[i].has_fixed_register_binding ||
                b[i].value >= fn->value_count ||
                !aarch64_linux_structured_spill_slot_valid(b[i].value) ||
                b[i].kind != (i == 3U ?
                    MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT :
                    i == 2U ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE :
                    MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT))
                return false;
            if (i == 3U) {
                if (!aarch64_stlr_type(fn->values[b[i].value].type, width))
                    return false;
            } else {
                if (!minic_type_pointee(fn->values[b[i].value].type, &pointee) ||
                    !aarch64_stlr_type(pointee, i == 1U ? 8U : width))
                    return false;
            }
        }
        /* Exact Linux atomic LL/SC loop with real hardware RMW effects.
         * Result and status outputs write their Core address-backed C locals;
         * memory operand is updated by STXR, not a fabricated ordinary store.
         * x9..x13 are AAPCS64 caller-saved scratch; numeric GAS label is local. */
        return fprintf(out,
            "  ldr x10, [sp, #%zu]\n"
            "  ldr x11, [sp, #%zu]\n"
            "  prfm pstl1strm, [x10]\n"
            "1:\n"
            "  ldxr %s9, [x10]\n"
            "  %s %s9, %s9, %s11\n"
            "  stxr w12, %s9, [x10]\n"
            "  cbnz w12, 1b\n"
            "  ldr x13, [sp, #%zu]\n"
            "  str %s9, [x13]\n"
            "  ldr x13, [sp, #%zu]\n"
            "  str x12, [x13]\n",
            (size_t)b[2].value * 8U,
            (size_t)b[3].value * 8U,
            width == 8U ? "x" : "w",
            atomic.op->alu,
            width == 8U ? "x" : "w",
            width == 8U ? "x" : "w",
            width == 8U ? "x" : "w",
            width == 8U ? "x" : "w",
            (size_t)b[0].value * 8U,
            width == 8U ? "x" : "w",
            (size_t)b[1].value * 8U) >= 0;
    }
    if (ins->value.structured_inline_asm.operand_count == 3U &&
        aarch64_linux_uaccess_bic_template(
            info->template_text, info->template_length)) {
        const MinicCoreStructuredInlineAsmOperand *ops =
            ins->value.structured_inline_asm.operands;
        const MinicCoreInstruction *imm;
        MinicCoreInstructionId def;
        MinicType pointee;
        if (!info->is_volatile || info->is_goto ||
            info->has_memory_clobber || info->register_clobber_count)
            return false;
        for (i = 0U; i < 3U; ++i) {
            if (ops[i].operand_index != i ||
                ops[i].kind != (i == 0U ?
                    MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
                    MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT) ||
                ops[i].has_fixed_register_binding || ops[i].early_clobber ||
                ops[i].value >= fn->value_count ||
                !aarch64_linux_structured_spill_slot_valid(ops[i].value))
                return false;
        }
        if (!minic_type_pointee(fn->values[ops[0].value].type, &pointee) ||
            !minic_type_is_pointer(pointee) ||
            !minic_type_is_pointer(fn->values[ops[1].value].type))
            return false;
        def = fn->values[ops[2].value].definition;
        if (def >= fn->instruction_count) return false;
        imm = &fn->instructions[def];
        if (imm->kind != MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT ||
            imm->result != ops[2].value ||
            (uint64_t)imm->value.integer_value != (UINT64_C(1) << 55))
            return false;
        /* Hardware BIC clears the single TBI/sign-extension tag bit 55
         * as GNU specifies. Stack SSA output is a C pointer object. */
        return fprintf(out,
            "  ldr x10, [sp, #%zu]\n"
            "  bic x9, x10, #0x80000000000000\n"
            "  ldr x11, [sp, #%zu]\n"
            "  str x9, [x11]\n",
            (size_t)ops[1].value * 8U,
            (size_t)ops[0].value * 8U) >= 0;
    }
    if (ins->value.structured_inline_asm.operand_count != 3U)
        return false;
    if (!info->is_volatile || info->is_goto || info->has_memory_clobber ||
        info->register_clobber_count != 1U || !info->register_clobbers ||
        !info->register_clobbers[0].name ||
        info->register_clobbers[0].name_length != 2U ||
        memcmp(info->register_clobbers[0].name, "cc", 2U) != 0 ||
        !mask_template(info->template_text, info->template_length))
        return false;
    b = ins->value.structured_inline_asm.operands;
    for (i = 0U; i < 3U; ++i) {
        MinicType ty;
        if (b[i].operand_index != i || b[i].value >= fn->value_count ||
            b[i].has_fixed_register_binding || b[i].early_clobber ||
            b[i].kind != (i == 0U ?
                MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
                MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT) ||
            !aarch64_linux_structured_spill_slot_valid(b[i].value))
            return false;
        ty = fn->values[b[i].value].type;
        if (i == 0U && !minic_type_pointee(ty, &ty)) return false;
        if (!minic_type_equal(ty, minic_type_unsigned_long()))
            return false;
    }
    /* GNU Ir admits a register alternative: x11 holds the size. x9/x10/x11
     * are caller-saved scratch and all live C values are in existing SSA
     * stack slots. AArch64 NZCV from CMP survives until the following SBC.
     * No direct raw-template passthrough or unsafe physical clobber alias. */
    return fprintf(out,
        "  ldr x10, [sp, #%zu]\n"
        "  ldr x11, [sp, #%zu]\n"
        "  cmp x10, x11\n"
        "  sbc x9, xzr, xzr\n"
        "  ldr x10, [sp, #%zu]\n"
        "  str x9, [x10]\n",
        (size_t)b[1].value * 8U,
        (size_t)b[2].value * 8U,
        (size_t)b[0].value * 8U) >= 0;
}
