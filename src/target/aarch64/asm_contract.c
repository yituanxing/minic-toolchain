#include "target/aarch64/asm_contract.h"
#include "target/common/lp64_frame.h"
#include "target/common/direct_linux_bug_asm.h"
#include "frontend/const_eval.h"
#include <string.h>
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
bool minic_aarch64_qualified_opaque_asm(const MinicCoreInlineAsm *a) {
    static const char *const allowed[] = {
        "msrdaifset,#3", "msrdaifclr,#3",
        "yield", "hint#20", "sev", "sevl", "wfe", "wfi", "nop",
        "dsbsy", "dsbish", "dsbishst", "dsbld",
        "dmbish", "dmbishld", "dmbishst", "dmbsy",
        "dmbosh", "dmboshld", "dmboshst", "isb"
    };
    char compact[64];
    size_t i, n = 0U;
    if (minic_multi_direct_linux_bug_asm(
            a, "14470", "14471", "14472", "brk0x800", true))
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
        {"mrs%0,tcr_el1", "tcr_el1"},
        {"mrs%0,esr_el1", "esr_el1"},
        {"mrs%0,sctlr_el1", "sctlr_el1"},
        {"mrs%0,far_el1", "far_el1"},
        {"mrs%0,par_el1", "par_el1"},
        {"mrs%0,id_aa64mmfr1_el1", "id_aa64mmfr1_el1"},
        {"mrs%0,hcr_el2", "hcr_el2"},
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
        "dsb sy", "dsb ish", "dsb ishst", "dsb ld",
        "dsb nsh", "dsb nshst", "isb",
        "msr daifset, #3", "msr daifclr, #3",
        "yield", "hint #20", "sev", "sevl", "wfe", "wfi", "nop"
    };
    char normalized[64];
    size_t i, n = 0U;
    if (text == NULL || len == 0U || len >= sizeof(normalized))
        return NULL;
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
/* Reserve source atomic prefixes from exactly the same typed operation
 * catalog used for source qualification and physical LL/SC emission. */
bool minic_aarch64_requires_qualified_input_asm(const MinicInlineAsm *src) {
    static const char *const fixed[] = {
        "brk", "stlrb", "stlrh", "stlr", "ldarb", "ldarh", "ldar",
        "msr", "bic", ".if",
        "strb", "strh", "str",
        /* Original Linux CMPXCHG begins with PRFM and named operands;
         * rejected constraints/modes must not escape into generic Core. */
        "prfm", "prfm", "1:"
    };
    char prefix[64];
    const char *const single[] = {prefix};
    size_t width, fetch, op;
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
 * LDXR/LDAXR + EOR + conditional STXR/STLXR. GNU source contains named
 * %[oldval] etc; Core normalizes them to positional %1 etc. Match BOTH
 * forms to exactly one AArch64 ordering/width contract; no raw passthrough. */
static unsigned int aarch64_linux_cmpxchg_form(
    const char *text, size_t length, bool numeric) {
    static const char *const loads[] = {"ldxr", "ldaxr", "ldxr", "ldxr"};
    static const char *const stores[] = {"stxr", "stxr", "stlxr", "stlxr"};
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
    for (word = 0U; word < 2U; ++word) {
        const char *mem = numeric ? "%2" : "%[v]";
        const char *oldval = numeric ?
            (word ? "%1" : "%w1") :
            (word ? "%[oldval]" : "%w[oldval]");
        const char *tmp = numeric ?
            (word ? "%0" : "%w0") :
            (word ? "%[tmp]" : "%w[tmp]");
        const char *status = numeric ? "%w0" : "%w[tmp]";
        const char *old = numeric ?
            (word ? "%3" : "%w3") :
            (word ? "%[old]" : "%w[old]");
        const char *newval = numeric ?
            (word ? "%4" : "%w4") :
            (word ? "%[new]" : "%w[new]");
        for (mode = 0U; mode < 4U; ++mode) {
            const int count = snprintf(expected, sizeof(expected),
                "prfmpstl1strm,%s1:%s%s,%s"
                "eor%s,%s,%scbnz%s,2f"
                "%s%s,%s,%scbnz%s,1b%s2:",
                mem, loads[mode], oldval, mem,
                tmp, oldval, old, tmp,
                stores[mode], status, newval, mem,
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
    static const char *const registers[] = {
        "daif", "ttbr0_el1", "ttbr1_el1", "tcr_el1", "sctlr_el1",
        "contextidr_el1", "cpacr_el1", "vbar_el1", "mdscr_el1",
        "osdlr_el1", "oslar_el1", "cntkctl_el1",
        "cntp_ctl_el0", "cntp_cval_el0", "cntv_ctl_el0",
        "cntv_cval_el0", "tpidr_el0", "tpidrro_el0",
        "tpidr_el2", "hcr_el2", "cptr_el2", "vtcr_el2",
        "vttbr_el2"
    };
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
    static const struct { unsigned int a,b,c,d,e; } regs[]={
        {3U,0U,0U,0U,0U},
        {3U,0U,0U,0U,5U},
        {3U,0U,0U,4U,0U},
        {3U,0U,0U,6U,2U},
        {3U,0U,0U,7U,0U},
        {3U,0U,0U,7U,1U},
        {3U,0U,4U,6U,0U},
        {3U,3U,0U,0U,1U},
        {3U,3U,14U,0U,5U},
        {3U,3U,14U,0U,6U}
    };
    char normalized[768],expected[768];
    size_t i,n=0U;
    if(!text||!length||length>=sizeof(normalized))return 0U;
    for(i=0U;i<length;++i){
        const char ch=text[i];
        if(ch==' '||ch=='\t'||ch=='\r'||ch=='\n')continue;
        if(ch=='\0'||n+1U>=sizeof(normalized))return 0U;
        normalized[n++]=ch;
    }
    normalized[n]='\0';
    for(i=0U;i<sizeof(regs)/sizeof(regs[0]);++i){
        int count=snprintf(expected,sizeof(expected),
          "%smrs_s%%0,(((%u)<<19)|((%u)<<16)|((%u)<<12)|"
          "((%u)<<8)|((%u)<<5)).purgemmrs_s",
          prefix,regs[i].a,regs[i].b,regs[i].c,regs[i].d,regs[i].e);
        if(count>0&&(size_t)count<sizeof(expected)&&
            strcmp(normalized,expected)==0)return (unsigned int)i+1U;
    }
    return 0U;
}
static unsigned int aarch64_linux_encoded_sysreg_instruction(unsigned int form){
    static const struct { unsigned int a,b,c,d,e; } regs[]={
        {3U,0U,0U,0U,0U},
        {3U,0U,0U,0U,5U},
        {3U,0U,0U,4U,0U},
        {3U,0U,0U,6U,2U},
        {3U,0U,0U,7U,0U},
        {3U,0U,0U,7U,1U},
        {3U,0U,4U,6U,0U},
        {3U,3U,0U,0U,1U},
        {3U,3U,14U,0U,5U},
        {3U,3U,14U,0U,6U}
    };
    const size_t index=(size_t)form-1U;
    if(!form||index>=sizeof(regs)/sizeof(regs[0]))return 0U;
    return 0xd5200000U | (regs[index].a<<19) | (regs[index].b<<16) |
           (regs[index].c<<12) | (regs[index].d<<8) | (regs[index].e<<5) |
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
    const unsigned int form=aarch64_linux_encoded_sysreg_form(
        info->template_text,info->template_length);
    const MinicCoreFunction *fn=ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *op=
        ins->value.structured_inline_asm.operands;
    MinicType type;
    if(!out||!ctx||!fn||!info||!op||!form||ctx->dynamic_frame||
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
           aarch64_linux_encoded_sysreg_instruction(form),
           (size_t)op[0].value*8U)>=0;
}

/* Entire original Linux 6.6.143 arm64 TLBI v8.5 control family.
 * The original source uses .arch armv8.5-a followed by TLBI and an rZ
 * GNU register-or-zero operand. This is a KERNEL HARDWARE invalidation,
 * not a C function call or an ordinary compiler memory fence. Keep all
 * ten original TLBI opcodes in this ISA target, never generic Core. */
static unsigned int aarch64_linux_tlbi_form(const char *text,size_t length) {
    static const char *const names[]={
        "vale1is",
        "vale2is",
        "aside1is",
        "rvale1is",
        "vae1is",
        "rvae1is",
        "vaale1is",
        "vaae1is",
        "vmalle1",
        "vmalle1is"
    };
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
    for(i=0U;i<sizeof(names)/sizeof(names[0]);++i){
        const int written=snprintf(expected,sizeof(expected),
            ".archarmv8.5-atlbi%s%s",names[i],
            i<8U?",%x0":"");
        if(written>0&&(size_t)written<sizeof(expected)&&
           strcmp(normalized,expected)==0)return (unsigned int)i+1U;
    }
    return 0U;
}
static const char *aarch64_linux_tlbi_mnemonic(unsigned int form) {
    static const char *const names[]={
        "vale1is",
        "vale2is",
        "aside1is",
        "rvale1is",
        "vae1is",
        "rvae1is",
        "vaale1is",
        "vaae1is",
        "vmalle1",
        "vmalle1is"
    };
    return !form||(size_t)form>sizeof(names)/sizeof(names[0])
        ? NULL:names[form-1U];
}
static bool aarch64_linux_tlbi_source(
    const MinicC0Program *program,const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const unsigned int form=src?aarch64_linux_tlbi_form(
        src->template_text,src->template_length):0U;
    const bool has_address=form>=1U&&form<=8U;
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
       ins->value.structured_inline_asm.operand_count!=(form<=8U?1U:0U))
        return false;
    if(form>8U)
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
    if(aarch64_linux_io_dependency_source(program,src,contract))
        return true;
    if(aarch64_linux_encoded_sysreg_source(program,src,contract))
        return true;
    if(aarch64_linux_tlbi_source(program,src,contract))return true;
    /* The exact native AArch64 zero-input ISA list is shared by source
     * qualification and emission. The original Linux DAIF helpers use
     * implicit volatile ASM with no explicit memory clobber. Both that
     * source and explicit clobber variants emit the SAME physical hardware
     * instruction; rejecting the former loses original kernel semantics. */
    if (program && src && contract && src->is_volatile &&
        !src->is_goto && src->output_count == 0U && src->input_count == 0U &&
        src->register_clobber_count == 0U && src->label_count == 0U) {
        const char *form=aarch64_barrier_template(
            src->template_text,src->template_length);
        if(form && src->clobber_count <= 1U &&
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
        const size_t width = form > 4U ? 8U : 4U;
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
         constraint(&src->inputs[0], "r") ||
         constraint(&src->inputs[0], "Ir") ||
         constraint(&src->inputs[0], "Jr"))) {
        Aarch64AtomicForm atomic;
        bool accepted = aarch64_atomic_form(
            src->template_text, src->template_length, false, &atomic);
        const size_t width = accepted ? atomic.width : 0U;
        accepted = accepted &&
            constraint(&src->inputs[0], atomic.op->rmw_constraint);
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
        strcmp(aarch64_linux_mrs_register(
                   src->template_text, src->template_length), "daif") == 0 &&
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
        const size_t width = form > 4U ? 8U : 4U;
        const unsigned int mode = (form - 1U) % 4U;
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
            "  %s %s9, [x10]\n"
            "  eor %s13, %s9, %s11\n"
            "  cbnz %s13, 2f\n"
            "  %s w13, %s12, [x10]\n"
            "  cbnz w13, 1b\n"
            "%s"
            "2:\n"
            "  ldr x14, [sp, #%zu]\n"
            "  str %s9, [x14]\n"
            "  ldr x14, [sp, #%zu]\n"
            "  str x13, [x14]\n",
            (size_t)ops[2].value * 8U,
            (size_t)ops[3].value * 8U,
            (size_t)ops[4].value * 8U,
            ld[mode], width == 8U ? "x" : "w",
            width == 8U ? "x" : "w",
            width == 8U ? "x" : "w",
            width == 8U ? "x" : "w",
            width == 8U ? "x" : "w",
            st[mode], width == 8U ? "x" : "w",
            mode == 3U ? "  dmb ish\n" : "",
            (size_t)ops[1].value * 8U,
            width == 8U ? "x" : "w",
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
