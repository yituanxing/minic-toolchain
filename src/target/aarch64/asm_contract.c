#include "target/aarch64/asm_contract_internal.h"
#include "target/common/direct_linux_bug_asm.h"
#include "target/common/linux_jump_label_asm.h"
#include <string.h>

/* Source-language admission remains in asm_contract_source.c. This module
 * owns the finite target ISA-form recognizers consumed at C ingress, while
 * selected instruction/operand semantics and physical output live solely in
 * machine_core_select.c and MachineIR's target GNU ASM serializer. */

/* Original Linux 6.6 arch/arm64/include/asm/jump_label.h.  Both jump
 * sites own one 16-byte __jump_table record: site and C label PREL32,
 * static-key/branch-flags PREL64.  Only the original two templates qualify;
 * arbitrary source text never reaches the target assembler through this gate. */
unsigned int aarch64_linux_jump_label_form(
    const char *source, size_t length) {
    return minic_multi_linux_jump_label_form(
        source, length,
        ".pushsection__jump_table,\"aw\".align3"
        ".long1b-.,%l[l_yes]-..quad%c0-..popsection");
}

bool minic_aarch64_linux_jump_label_asm_goto_core(
    const MinicCoreInlineAsm *source) {
    return minic_multi_linux_jump_label_asm_goto_core(
        source, aarch64_linux_jump_label_form);
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

bool aarch64_linux_cap_goto_text(const char *s, size_t len,
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

bool minic_aarch64_linux_cap_asm_goto_core(const MinicCoreInlineAsm *a) {
    return a != NULL && a->is_goto && a->is_volatile &&
           a->goto_target != MINIC_CORE_BLOCK_INVALID &&
           (aarch64_linux_cap_goto_text(a->template_text,
                                        a->template_length, false, false) ||
            aarch64_linux_cap_goto_text(a->template_text,
                                        a->template_length, false, true));
}

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
bool mask_template(const char *text, size_t len) {
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

/* Linux BRK BUG trap: the AArch64 GNU I constraint is an add/sub
 * immediate class, but BRK itself takes an imm16. Admit the exact
 * brk %0 template with a literal in the intersection (0..4095).
 * Never treat a GNU constraint as permission to pass through arbitrary ASM. */
bool aarch64_linux_brk_template(const char *text, size_t length) {
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
bool aarch64_linux_brk_value(int64_t value) {
    return value >= 0 && value <= 4095;
}
/* The original Linux 6.6.143 arm64 io.h __raw_read{b,w,l,q}
 * carries a Linux feature-91 .altinstructions patch table. Neither a
 * plain LDR nor a compiler barrier can replace the complete contract.
 * Exact source admission and machine emission share this target catalog.
 * 91 is ARM64_WORKAROUND_DEVICE_LOAD_ACQUIRE from v6.6.143 cpucaps. */
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
const Aarch64RawIoRead *aarch64_raw_io_read(
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

/* ORIGINAL Linux 6.6.143 arch/arm64/include/asm/io.h, all four
 * __raw_write{b,w,l,q} operations:
 * asm volatile("str{b,h} %w0, [%1]" : : "rZ"(val), "r"(addr));
 * This is a hardware store through a pointer register, NOT a GNU "Qo"
 * memory operand. Core carries two true scalar inputs and preserves its
 * implicit write effect via a single target-neutral contract flag. */
static const Aarch64RawIoStore aarch64_raw_io_stores[] = {
    {"strb%w0,[%1]", "strb w9, [x10]", 1U},
    {"strh%w0,[%1]", "strh w9, [x10]", 2U},
    {"str%w0,[%1]",  "str w9, [x10]", 4U},
    {"str%x0,[%1]",  "str x9, [x10]", 8U}
};
const Aarch64RawIoStore *aarch64_raw_io_store(
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

/* One typed AArch64 ISA descriptor for source qualification AND machine
 * emission. No ordinal op IDs, width thresholds or duplicated ALU tables:
 * new Linux atomics extend this target-owned operation/mode inventory only.
 * Common Core keeps exactly the existing structured operand roles. */
const Aarch64AtomicOperation aarch64_atomic_operations[6] = {
    {"and", "and", "Lr", "Kr", "Lr"},
    {"or", "orr", "Lr", "Kr", "Lr"},
    {"xor", "eor", "Lr", "Kr", "Lr"},
    {"andnot", "bic", "r", "r", "r"},
    {"add", "add", "Ir", "Ir", "Ir"},
    {"sub", "sub", "Jr", "Jr", "Jr"}
};

/* Reserve source atomic prefixes from exactly the same typed operation
 * catalog used for source qualification and physical LL/SC emission. */

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

/* Linux uaccess.h PAN and mte-kasan.h TCO are the same complete GNU
 * ALTERNATIVE(NOP,SET_PSTATE(...)) primitive. The feature IDs are derived
 * from Linux arch/arm64/tools/cpucaps, not guessed instruction strings:
 * ARM64_HAS_PAN=38, ARM64_MTE=53.  KASAN's optional alternative can be
 * disabled by .if 0 == 1; that must produce NO PSTATE change at all.
 * Both exact GNU .inst expressions and complete PREL32 metadata are
 * mandatory. The source/machine use this one finite target contract. */
Aarch64LinuxPstateAlternative aarch64_linux_pstate_alternative(
    const char *text, size_t length) {
    Aarch64LinuxPstateAlternative result = {0U, 0U, 0U};
    char compact[700], old_body[32], new_body[256];
    unsigned int caps[] = {38U, 53U};
    size_t i, n = 0U, k;
    if (!text || !length || length >= sizeof(compact)) return result;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
        if (c == '\0' || c == ';' || n + 1U >= sizeof(compact))
            return result;
        compact[n++] = c;
    }
    compact[n] = '\0';
    if (n < sizeof(".if0==1661:") - 1U ||
        strncmp(compact, ".if", 3U) != 0 ||
        (compact[3] != '0' && compact[3] != '1') ||
        strncmp(compact + 4U, "==1661:", 7U) != 0)
        return result;
    result.enabled = (unsigned int)(compact[3] - '0');
    compact[3] = '1'; /* parser only: retain original enabled value */
    for (k = 0U; k < sizeof(caps) / sizeof(caps[0]); ++k) {
        const unsigned int cap = caps[k];
        unsigned int opcode;
        if (!aarch64_linux_alt_partition(compact, n, cap,
                old_body, sizeof(old_body), new_body, sizeof(new_body)) ||
            strcmp(old_body, "nop") != 0) continue;
        opcode = minic_aarch64_linux_pstate_imm_opcode(
            new_body, strlen(new_body));
        if (!opcode || (opcode & ~0x100U) !=
            (cap == 38U ? 0xd500409fU : 0xd503409fU)) continue;
        result.cap = cap;
        result.opcode = opcode;
        return result;
    }
    result.enabled = 0U;
    return result;
}

/* arch/arm64/include/asm/kvm_mmu.h __kern_hyp_va() applies a live
 * KVM-provided VA mask to a +r LP64 scalar. ALTERNATIVE_CB has a callback
 * PREL32 second word (NOT a patch-replacement instruction pointer) and no
 * .org equal-length pair; the original 5 instructions are overwritten by
 * kvm_update_va_mask at CPU feature init. The exact GNU callback and cap
 * bit (1<<15)|ARM64_ALWAYS_SYSTEM(1) must be preserved. */
bool aarch64_linux_kvm_va_mask_form(
    const char *text, size_t length) {
    static const char exact[] =
        ".if1==1661:"
        "and%0,%0,#1ror%0,%0,#1add%0,%0,#0"
        "add%0,%0,#0,lsl12ror%0,%0,#63"
        "662:.pushsection.altinstructions,\"a\""
        ".word661b-..wordkvm_update_va_mask-..hword(1<<15)|(1)"
        ".byte662b-661b.byte664f-663f.popsection663:664:.endif";
    char compact[650];
    size_t i, n = 0U;
    if (!text || !length || length >= sizeof(compact)) return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
        if (c == '\0' || c == ';' || n + 1U >= sizeof(compact))
            return false;
        compact[n++] = c;
    }
    compact[n] = '\0';
    return strcmp(compact, exact) == 0;
}

/* Match the entire original GNU Linux template, not a mnemonic prefix.
 * No partial acceptance, arbitrary replacement opcode or opaque ASM
 * passthrough. Both source and Core machine recheck use this single owner. */
bool aarch64_atomic_form(const char *text, size_t len, bool fetch,
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
bool aarch64_linux_percpu_rmw_form(
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

/* Genuine original arm64 Linux atomic_{add,sub}_return and atomic64_
 * variants: four memory orders × two sizes × two operations (16).
 * Reuse the AArch64 target ISA operation and memory-order catalogs:
 * Common Core only carries typed source ASM operand roles. */
bool aarch64_atomic_return_form(
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
static const char *const aarch64_xchg_ld[4] = {"ld", "lda", "ld", "ld"};
static const char *const aarch64_xchg_st[4] = {"st", "st", "stl", "stl"};
bool aarch64_linux_xchg_form(
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
size_t aarch64_linux_cmpxchg_width(unsigned int form) {
    return form >= 1U && form <= 16U ?
        (size_t)1U << ((form - 1U) / 4U) : 0U;
}
unsigned int aarch64_linux_cmpxchg_form(
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
bool aarch64_linux_percpu_alternative_template(
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
bool aarch64_linux_percpu_write_alternative_template(
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
/* Linux 6.6 arm64 finite barrier alternatives: feature-71 NOP->DMB SY,
 * feature-95 both NOP->ISB and ISB->NOP, feature-86 three NOPs->IC IALLU,
 * DSB NSH, ISB, and feature-42 DSB NSH/ISB->SB/NOP. Their source and
 * replacement instructions, PREL32 records and two .org guards must survive
 * intact; these are privileged runtime patches, not compiler barriers. */
static const Aarch64LinuxBarrierAlternative aarch64_linux_barrier_alternatives[] = {
    {71U, "nop", "dmbsy", "nop", "dmb sy"},
    {95U, "nop", "isb", "nop", "isb"},
    {95U, "isb", "nop", "isb", "nop"},
    {86U, "nop;nop;nop", "iciallu;dsbnsh;isb",
     "nop\n  nop\n  nop", "ic iallu\n  dsb nsh\n  isb"},
    {42U, "dsbnshisb",
     ".inst(0xd5000000|(((0)<<19)|((3)<<16)|((3)<<12)|(((0))<<8)|(((7))<<5))|((31)&0x1f))nop",
     "dsb nsh\n  isb", ".inst 0xd50330ff\n  nop"}
};
const Aarch64LinuxBarrierAlternative *aarch64_linux_barrier_alternative(
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
            ".if1==1661:%s662:"
            ".pushsection.altinstructions,\"a\""
            ".word661b-..word663f-..hword%u"
            ".byte662b-661b.byte664f-663f"
            ".popsection.subsection1663:%s664:"
            ".org.-(664b-663b)+(662b-661b)"
            ".org.-(662b-661b)+(664b-663b)"
            ".previous.endif",
            form->old_compact, form->cap, form->new_compact);
        if (count > 0 && (size_t)count < sizeof(expected) &&
            strcmp(normalized, expected) == 0) return form;
    }
    return NULL;
}

bool minic_aarch64_linux_barrier_alternative_opaque(
    const MinicCoreInlineAsm *a) {
    return a != NULL && a->is_volatile && !a->is_goto &&
           a->register_clobber_count == 0U && !a->has_memory_clobber &&
           aarch64_linux_barrier_alternative(
               a->template_text, a->template_length) != NULL;
}
/* Linux 6.6 KVM __translate_far_to_hpfar: AT S1E1R can trap while running
 * at EL2. The original GNU inline assembly snapshots SPSR_EL2/ELR_EL2,
 * issues AT, restores the exception state on its fixup path, and emits a
 * two-PREL32 hyp exception-table record. Exactly this complete sequence is
 * required: dropping the table or modelling AT as a normal read is unsafe. */
bool aarch64_linux_kvm_at_fixup_template(
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

bool aarch64_linux_uaccess_bic_template(
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
/* Original Linux arm64 io.h __io_par is a physical data dependency
 * (EOR + CBNZ), NOT an ordinary compiler barrier or a C assignment. */
bool aarch64_linux_io_dependency_form(const char *text,size_t length){
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

/* Linux KVM's VHE feature-50 alternative is an ALTERNATIVE around two full
 * GNU .irp/.equ/.macro/.inst constructions, not an ordinary read_sysreg_s
 * instruction. Both arms MUST be emitted: VHE reads/writes SPSR_EL2/ELR_EL2
 * while non-VHE uses SPSR_EL1/ELR_EL1. Accept only exact architectural
 * register pairs with identical read/write direction, and preserve the
 * original capability record. Reuse the single-register GNU decoders. */
/* Only register pairs corresponding to the exact KVM EL1/EL12 or EL2/EL1
 * alias move may be re-encoded.  Both must be known architectural SYS_*
 * tuple encodings. This is deliberately not an arbitrary GNU macro bridge. */
static bool aarch64_linux_vhe_sysreg_pair(
    const Aarch64LinuxWriteSysreg *old_reg,
    const Aarch64LinuxWriteSysreg *new_reg, unsigned int capability) {
    if (!old_reg || !new_reg || old_reg->op0 != 3U ||
        new_reg->op0 != 3U || old_reg->crn != new_reg->crn ||
        old_reg->crm != new_reg->crm || old_reg->op2 != new_reg->op2)
        return false;
    if (capability == 50U)
        return (old_reg->op1 == 0U && new_reg->op1 == 5U) ||
               (old_reg->op1 == 4U && new_reg->op1 == 0U);
    /* Feature 47 is ARM64_HAS_VIRT_HOST_EXTN in arch_timer: S3_3_EL0
     * uses the S3_5_EL02 alias for CNT{P,V}_{CTL,CVAL} exclusively. */
    return capability == 47U && old_reg->op1 == 3U &&
           new_reg->op1 == 5U && old_reg->crn == 14U &&
           (old_reg->crm == 2U || old_reg->crm == 3U) &&
           (old_reg->op2 == 1U || old_reg->op2 == 2U);
}
static const Aarch64LinuxWriteSysreg *aarch64_linux_known_sysreg_opcode(
    unsigned int opcode) {
    const unsigned int reg_bits = opcode & 0x001fffe0U;
    size_t i;
    for (i = 0U; i < aarch64_linux_write_sysreg_count; ++i) {
        const Aarch64LinuxWriteSysreg *reg = &aarch64_linux_write_sysregs[i];
        if (reg_bits == ((reg->op0 << 19) | (reg->op1 << 16) |
                         (reg->crn << 12) | (reg->crm << 8) |
                         (reg->op2 << 5))) return reg;
    }
    return NULL;
}
bool aarch64_linux_vhe_sysreg_alternative_form(
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
            aarch64_linux_vhe_sysreg_pair(old_reg, new_reg, cap)) {
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
            if (aarch64_linux_vhe_sysreg_pair(old_reg, new_reg, cap)) {
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

/* Original Linux arch/arm64/kvm/hyp/include/nvhe/spinlock.h uses a ticket
 * lock with a feature-33 LSE replacement.  The output value of all three
 * temporary registers is observable to C, and the owner load has acquire
 * ordering.  The .altinstructions PREL32 relocation/length descriptors
 * must survive together with the LL/SC and LSE instruction sequences. */
Aarch64LinuxHypSpinForm aarch64_linux_hyp_spin_form(
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

/* Linux __cmpwait_case_{8,16,32,64}: typed three-operand GNU ASM
 * uses exclusive loads of the actual memory width, not an ordinary load. */
unsigned int aarch64_linux_cmpwait_form(const char *s, size_t length) {
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
bool aarch64_linux_zeropad_form(const char *s, size_t length) {
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

/* Linux 6.6 arm64 __get_user/__put_user: the unprivileged LDTR/STTR
 * instruction and its __ex_table fixup are one indivisible machine effect.
 * This is not an ordinary load/store: on a fault the exception handler
 * writes the error register (and, for a load, the destination register).
 * Validate the complete GNU template including the register-number .irp
 * table before choosing our own physical scratch registers. */
bool aarch64_linux_uaccess_form(const char *text, size_t length,
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

/* Linux arm_smccc_{smc,hvc} wrappers pass an x0/x1 argument vector and
 * receive x0..x3 results. GCC's local asm("rN") is a *binding of each GNU
 * operand*, not a C-level hard-register reservation. Validate both the
 * frontend bindings and the imported Core bindings before touching the
 * exception-level interface; unknown SMC/HVC variants remain unsupported. */
unsigned int aarch64_linux_smccc_form(const char *s, size_t n) {
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
bool aarch64_linux_xpaclri_form(const char *text, size_t length) {
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

/* The vDSO's original fallback syscall has a narrower, independently
 * checked GNU contract than SMCCC: x0/x1 carry the arguments, x8 the Linux
 * syscall number, and x0 the result.  The fixed local register bindings are
 * essential; a free-form `svc` or a registerless opaque barrier is unsafe. */
bool aarch64_linux_svc_form(const char *s, size_t n) {
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

/* FEAT_WFxT: original Linux arm64 delay.c uses the generic SYS-register
 * spelling for the two timeout wait instructions, WFIT and WFET.  Preserve
 * the precise GNU ISA instruction instead of synthesizing an ordinary WFI
 * or WFE; the input deadline is a physical 64-bit GPR value. */
unsigned int aarch64_linux_wfxt_form(const char *text, size_t length) {
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

/* Linux atomic64_dec_if_positive has no input operand: its +Q memory is
 * the sole source value and destination. A negative result is returned
 * without an exclusive store; successful updates use release STXR and
 * a full DMB ISH, retaining the source's signed-flag branch. */
bool aarch64_linux_atomic_dec_positive_form(
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

/* Native AArch64 128-bit LL/SC compare-exchange. Its eight GNU named
 * operands are exactly four outputs (+Q, old low/high, retry status) and
 * four 64-bit scalar inputs (expected/replacement low/high). Preserve the
 * full compare + CCMP short-circuit and the release/full-barrier variant. */
unsigned int aarch64_linux_cmpxchg128_form(
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

/* Linux arm64 futex LL/SC exception recovery is part of the atomic
 * operation. The fault handler must observe both the failed load's and
 * failed store's PC, with the exact physical error register in the type-2
 * __ex_table entry. Qualify the complete GNU .irp + fixup templates; no
 * generic raw-asm pass-through is permitted. Modes 1..5 are SET, ADD,
 * OR, AND, XOR; mode 6 is compare-exchange with two different fixups. */
unsigned int aarch64_linux_futex_form(
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

/* With CONFIG_ARM64_PAN disabled the original kernel's
 * __uaccess_{enable,disable}_hw_pan() macros enclose their *entire*
 * alternative sequence in `.if 0 == 1`. GNU AS emits no instruction and
 * no alternative entry, including the inner `.inst` and sections. Accept
 * precisely those two complete templates as zero-sized target effects.
 * A future enabled `.if 1 == 1` must be implemented, never silently elided. */
unsigned int aarch64_linux_disabled_hw_pan_form(
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

/* Linux arch_timer_read_cnt{p,v}ct_el0: feature 19 replaces an ISB+MRS
 * pair with NOP+MRS_S. The replacement reads CNTPOCTSS/CNTVCTSS (op2
 * 5/6), so treating this as an ordinary MRS would silently break the
 * CNTVCTSS clocksource semantics and discard boot-time alternatives.
 * Match the complete GNU .irp/.macro/.inst source and both .org checks. */
unsigned int aarch64_linux_timer_alternative_form(
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

/* Linux arch_counter_enforce_ordering() is a real dependent LOAD from SP.
 * The EOR dependency on the timer result controls execution ordering even
 * though its mathematical result is zero; do not replace with a C cast,
 * empty compiler barrier, or a substitute DMB. GNU has no explicit memory
 * clobber, so record its implicit hardware load in Core's effect metadata. */
bool aarch64_linux_counter_ordering_form(const char *text, size_t len) {
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

/* ARMv8.5 RNDR and RNDRRS return their success state in NZCV. The CSET
 * must consume the flags from the SAME system-register read, with no
 * intervening flag-setting instructions or compiler-generated tests.
 * Linux emits the GNU .irp register table and MRS_S macro to make the
 * encoding portable across assemblers lacking named RNDR support. */
unsigned int aarch64_linux_rng_sysreg_form(
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

/* Qualcomm arm64 return-stack-buffer sanitization intentionally executes
 * sixteen BL instructions to poison the predictor while preserving x30.
 * The generated call targets are the *next* instruction, not C functions;
 * the usual direct-call ABI machinery must not turn these into thunks. */
bool aarch64_linux_qcom_return_stack_form(
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

/* User-cache maintenance is the exception-recovering companion of the
 * ordinary DC/IC API. In the kernel's undef trap emulator, the cache
 * operation can fault while accessing TTBR0. Its type-2 __ex_table entry
 * names the C return-status register; silently issuing a bare DC/IC would
 * turn -EFAULT into an unhandled kernel abort. The source uses precisely
 * four hardware operations (three CRM cases share DC CIVAC). */
const char *aarch64_linux_user_cache_fault_form(
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

/* Linux 6.6 arm64 crash_setup_regs() stores all 31 GPRs before
 * repurposing x9/x10, then SP, PSTATE and the local-label PC. Admit only
 * that exact 675-byte hardware program (modulo harmless GNU whitespace).
 * This fingerprint is target/ISA-owned; C types and output bindings are
 * independently checked by the C qualifier and the Machine verifier. */
bool aarch64_linux_kexec_crash_template(const char *text, size_t length) {
    static const char canonical[] =
        "stpx0,x1,[%2,#16*0]stpx2,x3,[%2,#16*1]stpx4,x5,[%2,#16*2]stpx6,x7,[%2,"
        "#16*3]stpx8,x9,[%2,#16*4]stpx10,x11,[%2,#16*5]stpx12,x13,[%2,#16*6]stp"
        "x14,x15,[%2,#16*7]stpx16,x17,[%2,#16*8]stpx18,x19,[%2,#16*9]stpx20,x21"
        ",[%2,#16*10]stpx22,x23,[%2,#16*11]stpx24,x25,[%2,#16*12]stpx26,x27,[%2"
        ",#16*13]stpx28,x29,[%2,#16*14]mov%0,spstpx30,%0,[%2,#16*15]/*fakedcurr"
        "entPSTATE*/mrs%0,CurrentELmrs%1,SPSELorr%0,%0,%1mrs%1,DAIForr%0,%0,%1m"
        "rs%1,NZCVorr%0,%0,%1adr%1,1f1:stp%1,%0,[%2,#16*16]";
    char compact[768];
    size_t i, n = 0U;
    if (text == NULL || length == 0U || length >= sizeof(compact))
        return false;
    for (i = 0U; i < length; ++i) {
        const char ch = text[i];
        if (ch == '\0') return false;
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r')
            continue;
        if (n + 1U >= sizeof(compact)) return false;
        compact[n++] = ch;
    }
    compact[n] = '\0';
    return n == sizeof(canonical) - 1U &&
        memcmp(compact, canonical, n) == 0;
}

/* This inventory is the one machine-owned name for every AArch64 GNU
 * structured form admitted by its source backend. Source checks C types,
 * constraints, and effects; machine checks this ISA identity again against
 * the normalized Core template. Neither half may infer an instruction from
 * the diagnostic source hash or a GNU constraint string. */
bool aarch64_machine_asm_identify(const char *text, size_t length,
                                  bool normalized, uint32_t *id,
                                  uint32_t *variant) {
    Aarch64LseForm lse;
    Aarch64AtomicForm atomic;
    Aarch64PerCpuForm percpu;
    Aarch64XchgForm xchg;
    Aarch64UaccessForm uaccess;
    Aarch64LinuxVheSysregAlternative vhe;
    const Aarch64RawIoRead *read;
    const Aarch64RawIoStore *store;
    unsigned int mode;
    size_t width;
    if (!text || !length || !id || !variant) return false;
    *id = 0U;
    *variant = 0U;
#define A64_MACHINE_FORM(predicate, form, detail) do { \
    if (predicate) { \
        *id = AARCH64_MACHINE_ASM_##form; \
        *variant = (uint32_t)(detail); \
        return true; \
    } \
} while (0)
    A64_MACHINE_FORM(length == sizeof(".arch_extension sha3\neor3 %0.16b, %1.16b, %2.16b, %3.16b") - 1U &&
                     memcmp(text, ".arch_extension sha3\neor3 %0.16b, %1.16b, %2.16b, %3.16b", length) == 0,
                     SHA3_EOR3, 0U);
    /* Preserve the prior machine dispatch priority where pattern families
     * might overlap; do not let a broader opcode family shadow an exact
     * Linux alternative or memory model. */
    A64_MACHINE_FORM(aarch64_linux_kexec_crash_template(text,length),
                      KEXEC_CRASH, 0U);
    A64_MACHINE_FORM(aarch64_linux_kvm_va_mask_form(text,length), KVM_VA_MASK, 0U);
    A64_MACHINE_FORM(aarch64_linux_pstate_alternative(text,length).cap,
                     PSTATE_ALTERNATIVE, 0U);
    A64_MACHINE_FORM((mode = aarch64_linux_hyp_spin_form(text,length)) != 0U,
                     HYP_SPIN, mode);
    A64_MACHINE_FORM(length == sizeof("rbit %w0, %w1") - 1U &&
                     memcmp(text,"rbit %w0, %w1",length) == 0,
                     BIT_REVERSE, 0U);
    A64_MACHINE_FORM(aarch64_lse_form(text,length,normalized,&lse), LSE,
                     ((unsigned int)lse.kind << 16) | ((unsigned int)lse.width << 8) | lse.mode);
    A64_MACHINE_FORM((mode = aarch64_linux_lse_casp128_form(text,length)) != 0U,
                     LSE_CASP128, mode);
    A64_MACHINE_FORM(aarch64_linux_lse_dec_positive_form(text,length),
                     LSE_DEC_POSITIVE, 0U);
    A64_MACHINE_FORM(aarch64_linux_kvm_at_fixup_template(text,length), KVM_AT_FIXUP, 0U);
    A64_MACHINE_FORM(aarch64_linux_vhe_sysreg_alternative_form(text,length,&vhe),
                     VHE_SYSREG_ALTERNATIVE, 0U);
    A64_MACHINE_FORM(aarch64_linux_at_s1e1r_template(text,length), AT_S1E1R, 0U);
    A64_MACHINE_FORM(aarch64_linux_barrier_alternative(text,length) != NULL,
                     BARRIER_ALTERNATIVE, 0U);
    A64_MACHINE_FORM(aarch64_linux_percpu_write_alternative_template(text,length),
                     PERCPU_WRITE_ALTERNATIVE, 0U);
    A64_MACHINE_FORM(aarch64_linux_qcom_return_stack_form(text,length),
                     QCOM_RETURN_STACK, 0U);
    A64_MACHINE_FORM((mode = aarch64_linux_rng_sysreg_form(text,length)) != 0U,
                     RNG_SYSREG, mode);
    A64_MACHINE_FORM(aarch64_linux_counter_ordering_form(text,length),
                     COUNTER_ORDERING, 0U);
    A64_MACHINE_FORM((mode = aarch64_linux_timer_alternative_form(text,length)) != 0U,
                     TIMER_ALTERNATIVE, mode);
    A64_MACHINE_FORM(aarch64_linux_xpaclri_form(text,length), XPACLRI, 0U);
    A64_MACHINE_FORM((mode = aarch64_linux_smccc_form(text,length)) != 0U,
                     SMCCC, mode);
    A64_MACHINE_FORM(aarch64_linux_svc_form(text,length), SVC, 0U);
    A64_MACHINE_FORM(aarch64_linux_atomic_dec_positive_form(text,length),
                     ATOMIC_DEC_POSITIVE, 0U);
    A64_MACHINE_FORM((mode = aarch64_linux_cmpxchg128_form(text,length)) != 0U,
                     CMPXCHG128, mode);
    A64_MACHINE_FORM((mode = aarch64_linux_futex_form(text,length)) != 0U,
                     FUTEX, mode);
    A64_MACHINE_FORM((mode = aarch64_linux_disabled_hw_pan_form(text,length)) != 0U,
                     DISABLED_HW_PAN, mode);
    A64_MACHINE_FORM((mode = aarch64_linux_wfxt_form(text,length)) != 0U,
                     WFXT, mode);
    A64_MACHINE_FORM(aarch64_linux_uaccess_form(text,length,&uaccess), UACCESS,
                     ((unsigned int)uaccess.is_load << 16) |
                     ((unsigned int)uaccess.width << 8) | uaccess.exception_type);
    A64_MACHINE_FORM(aarch64_linux_zeropad_form(text,length), ZEROPAD, 0U);
    A64_MACHINE_FORM(aarch64_linux_user_cache_fault_form(text,length) != NULL,
                     USER_CACHE_FAULT, 0U);
    A64_MACHINE_FORM(aarch64_linux_cache_op_form(text,length) != NULL,
                     CACHE_OP, 0U);
    A64_MACHINE_FORM((mode = aarch64_linux_encoded_sysreg_write_form(text,length)) != 0U,
                     ENCODED_SYSREG_WRITE, mode);
    A64_MACHINE_FORM((mode = aarch64_linux_tlbi_form(text,length)) != 0U,
                     TLBI, mode);
    A64_MACHINE_FORM((mode = aarch64_linux_cmpwait_form(text,length)) != 0U,
                     CMPWAIT, mode);
    A64_MACHINE_FORM(aarch64_barrier_template(text,length) != NULL,
                     BARRIER, 0U);
    A64_MACHINE_FORM((mode = aarch64_linux_encoded_sysreg_form(text,length)) != 0U,
                     ENCODED_SYSREG_READ, mode);
    A64_MACHINE_FORM(aarch64_linux_prefetch_template(text,length) != NULL,
                     PREFETCH, 0U);
    A64_MACHINE_FORM(aarch64_linux_msr_register(text,length) != NULL, MSR, 0U);
    A64_MACHINE_FORM(aarch64_linux_brk_template(text,length), BRK, 0U);
    A64_MACHINE_FORM(aarch64_linux_mrs_register(text,length) != NULL, MRS, 0U);
    A64_MACHINE_FORM(aarch64_linux_io_dependency_form(text,length),
                     IO_DEPENDENCY, 0U);
    A64_MACHINE_FORM(aarch64_linux_percpu_alternative_template(text,length),
                     PERCPU_ALTERNATIVE, 0U);
    read = aarch64_raw_io_read(text,length);
    A64_MACHINE_FORM(read != NULL, RAW_IO_READ, read ? read->width : 0U);
    store = aarch64_raw_io_store(text,length);
    A64_MACHINE_FORM(store != NULL, RAW_IO_STORE, store ? store->width : 0U);
    width = aarch64_load_acquire_width(text,length);
    A64_MACHINE_FORM(width != 0U, LDAR, width);
    width = aarch64_stlr_width(text,length);
    A64_MACHINE_FORM(width != 0U, STLR, width);
    /* A bare per-CPU LL/SC add and add_return have identical numeric
     * machine templates: GNU's named `[tmp]` versus `[ret]` disappears
     * when the operands become %1. Both execute the same exact LL/SC
     * sequence and write the resulting value to the same Core output.
     * Preserve the independently recognizable width and ALU identity, not
     * the C operand's name. An ALTERNATIVE+LSE form retains its replacement
     * opcode in the normalized text, verified by the shared recognizer. */
    A64_MACHINE_FORM(aarch64_linux_percpu_rmw_form(text,length,&percpu),
                     PERCPU_RMW,
                     ((unsigned int)percpu.width << 8) |
                     (unsigned int)(percpu.op - aarch64_atomic_operations));
    A64_MACHINE_FORM(aarch64_atomic_return_form(text,length,&atomic),
                     ATOMIC_RETURN,
                     ((unsigned int)atomic.width << 8) | atomic.mode);
    A64_MACHINE_FORM(aarch64_linux_xchg_form(text,length,&xchg), XCHG,
                     ((unsigned int)xchg.width << 8) | xchg.mode);
    A64_MACHINE_FORM((mode = aarch64_linux_cmpxchg_form(text,length,normalized)) != 0U,
                     CMPXCHG, mode);
    A64_MACHINE_FORM(aarch64_atomic_form(text,length,false,&atomic), ATOMIC_RMW,
                     ((unsigned int)atomic.width << 8) | atomic.mode);
    A64_MACHINE_FORM(aarch64_atomic_form(text,length,true,&atomic), ATOMIC_FETCH,
                     ((unsigned int)atomic.width << 8) | atomic.mode);
    A64_MACHINE_FORM(aarch64_linux_uaccess_bic_template(text,length),
                     UACCESS_BIC, 0U);
    A64_MACHINE_FORM(mask_template(text,length), MASK, 0U);
#undef A64_MACHINE_FORM
    return false;
}
