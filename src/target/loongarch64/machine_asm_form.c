#include "target/loongarch64/machine_asm_form.h"
#include "target/loongarch64/asm_contract_internal.h"
#include "target/common/linux_jump_label_asm.h"

#include <limits.h>
#include <string.h>
#include <stdio.h>

/* Source admission, MIR emission and linker-relocation recording must share
 * the same exact Linux static-key grammar, independent of legacy FILE* code. */
unsigned int loong_linux_jump_label_form(
    const char *text, size_t length) {
    return minic_multi_linux_jump_label_form(
        text, length,
        ".pushsection__jump_table,\"aw\".align3"
        ".long1b-.,%l[l_yes]-..quad%0-..popsection");
}

bool minic_loongarch64_jump_key_relocation_valid(
    const char *symbol, int64_t addend) {
    MinicCoreInlineAsmImmediate source_witness = {0};
    source_witness.kind = MINIC_CORE_INLINE_ASM_IMMEDIATE_SYMBOL;
    source_witness.symbol = (char *)symbol;
    source_witness.byte_addend = addend;
    /* Machine is not a second C expression evaluator: this is the same
     * bounded symbol-plus-byte-addend witness the source/Core gate sealed. */
    return minic_core_inline_asm_relocation_valid(&source_witness);
}

bool minic_loongarch64_linux_jump_label_asm_goto_core(
    const MinicCoreInlineAsm *source) {
    return minic_multi_linux_jump_label_asm_goto_core(
        source, loong_linux_jump_label_form);
}

LoongLinuxClockAsm loong_linux_clock_form(
    const char *text, size_t length) {
    static const char *const forms[] = {
        "rdtime.d%0,%1", "rdtime.d%[val],%[tid]",
        "la.pcrel%0,_start", "rdtime.d$zero,%0",
        "rdtime.d%0,$zero"
    };
    char compact[96];
    size_t i, n = 0U;
    if (!text || !length || length >= sizeof(compact)) return LOONG_CLOCK_NONE;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == ';' || c == '\0' || n + 1U >= sizeof(compact))
            return LOONG_CLOCK_NONE;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(forms) / sizeof(forms[0]); ++i)
        if (strcmp(compact, forms[i]) == 0)
            return (LoongLinuxClockAsm)(i + 1U);
    return LOONG_CLOCK_NONE;
}

bool loong_linux_vdso_syscall_form(const char *text, size_t length) {
    char compact[32];
    size_t i, n = 0U;
    if (!text || !length || length >= sizeof(compact)) return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
        if (c == ';' || c == '\0' || n + 1U >= sizeof(compact))
            return false;
        compact[n++] = c;
    }
    compact[n] = '\0';
    return strcmp(compact, "syscall0") == 0;
}

bool loongarch64_dbar_hint(int64_t hint) {
    return hint >= 0 && hint <= 2047;
}

bool loongarch64_dbar_template(const char *text, size_t length) {
    static const char prefix[] = "dbar %0";
    size_t i, n;
    if (text == NULL || length < sizeof(prefix) - 1U)
        return false;
    n = sizeof(prefix) - 1U;
    if (memcmp(text, prefix, n) != 0) return false;
    for (i = n; i < length; ++i) {
        if (text[i] != ' ' && text[i] != '\t')
            return false;
    }
    return true;
}

bool loong_linux_cacop_form(const char *text, size_t length) {
    static const char exact[] = "cacop%0,%1";
    char compact[sizeof(exact)];
    size_t i, n = 0U;
    if (!text || !length) return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
        if (c == ';' || c == '\0' || n + 1U >= sizeof(compact))
            return false;
        compact[n++] = c;
    }
    compact[n] = '\0';
    return strcmp(compact, exact) == 0;
}

const char *loongarch64_literal_dbar(const char *text,size_t length){
    static const char *const allowed[]={"dbar 0","dbar 0x700"};
    char norm[48];size_t i,n=0U;
    if(!text||!length||length>=sizeof(norm))return NULL;
    for(i=0U;i<length;++i){
        const char c=text[i];
        if(c==9||c==10||c==13||c==';')return NULL;
        if(n+1U>=sizeof(norm))return NULL;
        norm[n++]=c;
    }
    while(n!=0U&&norm[n-1U]==' ')--n;
    norm[n]=0;
    for(i=0U;i<sizeof(allowed)/sizeof(allowed[0]);++i)
        if(strcmp(norm,allowed[i])==0)return allowed[i];
    return NULL;
}

const struct MinicLoongAtomicTemplate loong_atomic[] = {
    {"amand_db.w$zero,%1,%0", "amand_db.w", 4U},
    {"amor_db.w$zero,%1,%0", "amor_db.w", 4U},
    {"amxor_db.w$zero,%1,%0", "amxor_db.w", 4U},
    {"amadd_db.w$zero,%1,%0", "amadd_db.w", 4U},
    {"amand_db.d$zero,%1,%0", "amand_db.d", 8U},
    {"amor_db.d$zero,%1,%0", "amor_db.d", 8U},
    {"amxor_db.d$zero,%1,%0", "amxor_db.d", 8U},
    {"amadd_db.d$zero,%1,%0", "amadd_db.d", 8U}
};

const struct MinicLoongAtomicTemplate *loong_atomic_template(
    const char *text, size_t length, bool fetch_old_value) {
    char compact[96];
    size_t i, used = 0U;
    if (text == NULL || length == 0U || length >= sizeof(compact))
        return NULL;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
            continue;
        if (used + 1U >= sizeof(compact)) return NULL;
        compact[used++] = c;
    }
    compact[used] = '\0';
    for (i = 0U; i < sizeof(loong_atomic) / sizeof(loong_atomic[0]); ++i) {
        char expected[64];
        if (fetch_old_value) {
            const int count = snprintf(expected, sizeof(expected),
                                       "%s%%1,%%2,%%0", loong_atomic[i].instruction);
            if (count <= 0 || (size_t)count >= sizeof(expected)) return NULL;
            if (strcmp(compact, expected) == 0) return &loong_atomic[i];
        } else if (strcmp(compact, loong_atomic[i].text) == 0) {
            return &loong_atomic[i];
        }
    }
    return NULL;
}

unsigned int loong_csr_template(const char *text, size_t length) {
    static const char *const forms[] = {
        "csrrd%[val],%[reg]", "csrrd%0,%1",
        "csrxchg%[val],%[mask],%[reg]", "csrxchg%0,%1,%2"
    };
    char compact[96];
    size_t i, n = 0U;
    if (!text || length == 0U || length >= sizeof(compact)) return 0U;
    for (i = 0U; i < length; ++i) {
        char c = text[i];
        if (c == ';') return 0U;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
        if (n + 1U >= sizeof(compact)) return 0U;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(forms) / sizeof(forms[0]); ++i)
        if (strcmp(compact, forms[i]) == 0) return i < 2U ? 1U : 2U;
    return 0U;
}

bool loong_linux_cpucfg_form(const char *text, size_t length) {
    static const char form[] = "cpucfg%0,%1";
    char compact[64];
    size_t i, size = 0U;
    if (!text || !length || length >= sizeof(compact)) return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
        if (c == '\0' || c == ';' || size + 1U >= sizeof(compact))
            return false;
        compact[size++] = c;
    }
    compact[size] = '\0';
    return strcmp(compact, form) == 0;
}

bool loong_pte_form(const char *text,size_t len) {
    static const char named[]=
        "1:ll.d%[tmp],%[buddy]bnez%[tmp],2for%[tmp],%[tmp],%[global]"
        "sc.d%[tmp],%[buddy]beqz%[tmp],1bnop2:dbar0x700";
    static const char numeric[]=
        "1:ll.d%1,%0bnez%1,2for%1,%1,%2"
        "sc.d%1,%0beqz%1,1bnop2:dbar0x700";
    char compact[240];
    size_t i,n=0U;
    if(!text||!len||len>=sizeof(compact))return false;
    for(i=0U;i<len;++i){
        char ch=text[i];
        if(ch==' '||ch=='\t'||ch=='\n'||ch=='\r')continue;
        if(n+1U>=sizeof(compact))return false;
        compact[n++]=ch;
    }
    compact[n]='\0';
    return strcmp(compact,named)==0||strcmp(compact,numeric)==0;
}

unsigned loong_linux_cmpxchg_form(const char *text,size_t length) {
    static const char *const exact[]={
        "1:ll.w%0,%2#__cmpxchg_asmbne%0,%z3,2fmove$t0,%z4sc.w$t0,%1beqz$t0,1b2:dbar0x700",
        "1:ll.d%0,%2#__cmpxchg_asmbne%0,%z3,2fmove$t0,%z4sc.d$t0,%1beqz$t0,1b2:dbar0x700",
        "1:ll.w%0,%3and%1,%0,%z4bne%1,%z5,2fandn%1,%0,%z4or%1,%1,%z6sc.w%1,%2beqz%1,1bb3f2:dbar0x7003:"
    };
    char compact[256];size_t i,n=0U;
    if(!text||!length||length>=sizeof(compact))return 0U;
    for(i=0U;i<length;++i){
        const char c=text[i];
        if(c==' '||c=='\t'||c=='\r'||c=='\n')continue;
        if(c=='\0'||c==';'||n+1U>=sizeof(compact))return 0U;
        compact[n++]=c;
    }
    compact[n]='\0';
    for(i=0U;i<3U;++i)if(strcmp(compact,exact[i])==0)return (unsigned)i+1U;
    return 0U;
}

unsigned loong_linux_xchg_form(const char *text,size_t length){
    static const char *const forms[]={
        /* arch/loongarch/include/asm/cmpxchg.h __xchg_asm */
        "amswap_db.w%1,%z2,%0","amswap_db.d%1,%z2,%0",
        /* arch/loongarch/include/asm/barrier.h __smp_store_mb.
         * The GNU named form has an unsigned-long scratch and NO explicit
         * clobber; Core supplies the implicit +ZB memory effect. */
        "amswap_db.w%[tmp],%[val],%[mem]",
        "amswap_db.d%[tmp],%[val],%[mem]",
        /* Checked numeric representation after Core name resolution. */
        "amswap_db.w%1,%2,%0","amswap_db.d%1,%2,%0",
        /* Linux relaxed xchg is an atomic swap without the DB fence. */
        "amswap.w%1,%z2,%0","amswap.d%1,%z2,%0",
        /* The local atomic fetch operations have the SAME Core operand
         * roles as xchg: +ZB memory, old GPR output, scalar input. Only
         * the hardware AMO operation differs; never emulate a RMW in C. */
        "amadd.w%1,%2,%0","amadd.d%1,%2,%0",
        "amand.w%1,%2,%0","amand.d%1,%2,%0",
        "amor.w%1,%2,%0","amor.d%1,%2,%0",
        "amxor.w%1,%2,%0","amxor.d%1,%2,%0"
    };
    char compact[96];size_t n=0U,i;
    if(!text||!length||length>=sizeof(compact))return 0U;
    for(i=0U;i<length;++i){
        const char c=text[i];
        if(c==' '||c=='\t'||c=='\r'||c=='\n')continue;
        if(c=='\0'||c==';'||n+1U>=sizeof(compact))return 0U;
        compact[n++]=c;
    }
    compact[n]='\0';
    for(i=0U;i<sizeof(forms)/sizeof(forms[0]);++i)
        if(strcmp(compact,forms[i])==0)return (unsigned)i+1U;
    return 0U;
}

bool loong_linux_xchg_small_form(const char *text,size_t length){
    static const char exact[]=
        "1:ll.w%0,%3andn%1,%0,%z4or%1,%1,%z5sc.w%1,%2beqz%1,1b";
    char compact[160];size_t i,n=0U;
    if(!text||!length||length>=sizeof(compact))return false;
    for(i=0U;i<length;++i){
        const char ch=text[i];
        if(ch==' '||ch=='\t'||ch=='\r'||ch=='\n')continue;
        if(ch=='\0'||ch==';'||n+1U>=sizeof(compact))return false;
        compact[n++]=ch;
    }
    compact[n]='\0';
    return strcmp(compact,exact)==0;
}

bool loong_mask_nospec_form(const char *text, size_t length) {
    static const char exact[] = "sltu%0,%1,%2sub.d%0,$zero,%0";
    char compact[128];
    size_t i, used = 0U;
    if (text == NULL || length == 0U || length >= sizeof(compact))
        return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            continue;
        if (c == ';' || c == '\0' || used + 1U >= sizeof(compact))
            return false;
        compact[used++] = c;
    }
    compact[used] = '\0';
    return strcmp(compact, exact) == 0;
}

MinicLoongPerCpuIndexedForm loong_percpu_indexed_form(
    const char *text, size_t length) {
    static const char *const words[] = {
        "ldx.b%[ret],$r21,%[ptr]", "ldx.h%[ret],$r21,%[ptr]",
        "ldx.w%[ret],$r21,%[ptr]", "ldx.d%[ret],$r21,%[ptr]",
        "stx.b%[val],$r21,%[ptr]", "stx.h%[val],$r21,%[ptr]",
        "stx.w%[val],$r21,%[ptr]", "stx.d%[val],$r21,%[ptr]"
    };
    char compact[96];
    size_t i, n = 0U;
    if (!text || length == 0U || length >= sizeof(compact)) return LOONG_PERCPU_NONE;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
        if (c == ';' || c == '\0' || n + 1U >= sizeof(compact))
            return LOONG_PERCPU_NONE;
        compact[n++] = c;
    }
    compact[n] = '\0';
    /* Shared Core canonicalizes GNU named slots to %0/%1, but the
     * target must also qualify ORIGINAL named source templates. Both
     * forms name exactly the same two typed operand roles. */
    for (i = 0U; i < sizeof(words)/sizeof(words[0]); ++i) {
        char numeric[48];
        const char *const name = i < 4U ?
            (i == 0U ? "ldx.b" : i == 1U ? "ldx.h" :
             i == 2U ? "ldx.w" : "ldx.d") :
            (i == 4U ? "stx.b" : i == 5U ? "stx.h" :
             i == 6U ? "stx.w" : "stx.d");
        if (strcmp(compact, words[i]) == 0) return (MinicLoongPerCpuIndexedForm)(i + 1U);
        if (snprintf(numeric,sizeof(numeric), "%s%%0,$r21,%%1",name) > 0 &&
            strcmp(compact,numeric) == 0)
            return (MinicLoongPerCpuIndexedForm)(i + 1U);
    }
    return LOONG_PERCPU_NONE;
}

const LoongPerCpuAmoTemplate loong_percpu_amo[] = {
    {"amadd.w%[ret],%[val],%[ptr]", "amadd.w", 4U},
    {"amadd.d%[ret],%[val],%[ptr]", "amadd.d", 8U},
    {"amand.w%[ret],%[val],%[ptr]", "amand.w", 4U},
    {"amand.d%[ret],%[val],%[ptr]", "amand.d", 8U},
    {"amor.w%[ret],%[val],%[ptr]", "amor.w", 4U},
    {"amor.d%[ret],%[val],%[ptr]", "amor.d", 8U}
};

size_t loong_percpu_amo_width(size_t form) {
    if (form >= sizeof(loong_percpu_amo) / sizeof(loong_percpu_amo[0]))
        return 0U;
    return loong_percpu_amo[form].width;
}

size_t loong_percpu_amo_form(const char *text, size_t length) {
    char compact[96];
    size_t i,n=0U;
    if (!text || !length || length >= sizeof(compact)) return SIZE_MAX;
    for(i=0U;i<length;++i) {
        const char c=text[i];
        if(c==' '||c=='\n'||c=='\r'||c=='\t')continue;
        if(c==';'||c=='\0'||n+1U>=sizeof(compact))return SIZE_MAX;
        compact[n++]=c;
    }
    compact[n]='\0';
    for (i=0U;i<sizeof(loong_percpu_amo)/sizeof(loong_percpu_amo[0]);++i) {
        char numeric[64];
        if(strcmp(compact,loong_percpu_amo[i].compact)==0)return i;
        if(snprintf(numeric,sizeof(numeric),"%s%%0,%%2,%%1",
                    loong_percpu_amo[i].mnemonic)>0 &&
           strcmp(compact,numeric)==0)return i;
    }
    return SIZE_MAX;
}

bool loong_fcsr0_write_form(const char *text,size_t len){
    static const char exact[]="movgr2fcsr$fcsr0,%0";
    char norm[64];size_t i,n=0U;
    if(!text||len==0U||len>=sizeof(norm))return false;
    for(i=0U;i<len;++i) {
        const char c=text[i];
        if(c==' '||c=='\n'||c=='\r'||c=='\t')continue;
        if(c==';'||c=='\0'||n+1U>=sizeof(norm))return false;
        norm[n++]=c;
    }
    norm[n]='\0';
    return strcmp(norm,exact)==0;
}

unsigned int loong_linux_fpr_transfer_form(
    const char *text, size_t length) {
    char compact[96], expected[96];
    size_t i, n = 0U, reg;
    if (!text || !length || length >= sizeof(compact)) return 0U;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || c == ';' || n + 1U >= sizeof(compact))
            return 0U;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (reg = 0U; reg < 32U; ++reg) {
        int written = snprintf(expected, sizeof(expected),
            "movfr2gr.d%%0,$f%zu", reg);
        if (written > 0 && (size_t)written < sizeof(expected) &&
            strcmp(compact, expected) == 0) return (unsigned int)reg + 1U;
        written = snprintf(expected, sizeof(expected),
            "movgr2fr.d$f%zu,%%0", reg);
        if (written > 0 && (size_t)written < sizeof(expected) &&
            strcmp(compact, expected) == 0) return (unsigned int)reg + 33U;
    }
    return 0U;
}

unsigned int loong_linux_invtlb_form(const char *text, size_t length) {
    static const char *const forms[] = {
        "invtlb%0,%1,%2", "invtlb%0,$zero,%1",
        "invtlb%0,%1,$zero", "invtlb%0,$zero,$zero"
    };
    char compact[90];
    size_t i, n = 0U;
    if (!text || !length || length >= sizeof(compact)) return 0U;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return 0U;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(forms) / sizeof(forms[0]); ++i)
        if (strcmp(compact, forms[i]) == 0) return (unsigned int)i + 1U;
    return 0U;
}

unsigned int loong_linux_add_unless_form(
    const char *text, size_t length) {
    static const char *const forms[] = {
        "0:ll.w%[p],%[c]beq%[p],%[u],1fadd.w%[rc],%[p],%[a]sc.w%[rc],%[c]beqz%[rc],0bb2f1:dbar0x7002:",
        "0:ll.d%[p],%[c]beq%[p],%[u],1fadd.d%[rc],%[p],%[a]sc.d%[rc],%[c]beqz%[rc],0bb2f1:dbar0x7002:",
        "0:ll.w%0,%2beq%0,%4,1fadd.w%1,%0,%3sc.w%1,%2beqz%1,0bb2f1:dbar0x7002:",
        "0:ll.d%0,%2beq%0,%4,1fadd.d%1,%0,%3sc.d%1,%2beqz%1,0bb2f1:dbar0x7002:"
    };
    char compact[260];
    size_t i, n = 0U;
    if (!text || !length || length >= sizeof(compact)) return 0U;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return 0U;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(forms) / sizeof(forms[0]); ++i)
        if (strcmp(compact, forms[i]) == 0) return (unsigned int)(i % 2U) + 1U;
    return 0U;
}

bool loong_linux_sub_positive_form(
    const char *text, size_t length, LoongLinuxSubPositive *result) {
    static const struct {
        const char *compact;
        size_t width;
        bool add_negated;
    } forms[] = {
        {"1:ll.w%1,%2#atomic_sub_if_positiveaddi.w%0,%1,%3move%1,%0bltz%0,2fsc.w%1,%2beqz%1,1b2:dbar0x700",4U,true},
        {"1:ll.w%1,%2#atomic_sub_if_positivesub.w%0,%1,%3move%1,%0bltz%0,2fsc.w%1,%2beqz%1,1b2:dbar0x700",4U,false},
        {"1:ll.d%1,%2#atomic64_sub_if_positiveaddi.d%0,%1,%3move%1,%0bltz%0,2fsc.d%1,%2beqz%1,1b2:dbar0x700",8U,true},
        {"1:ll.d%1,%2#atomic64_sub_if_positivesub.d%0,%1,%3move%1,%0bltz%0,2fsc.d%1,%2beqz%1,1b2:dbar0x700",8U,false}
    };
    char compact[260];
    size_t i, n = 0U;
    if (!text || !length || !result || length >= sizeof(compact)) return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return false;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(forms) / sizeof(forms[0]); ++i)
        if (strcmp(compact, forms[i].compact) == 0) {
            result->width = forms[i].width;
            result->add_negated = forms[i].add_negated;
            return true;
        }
    return false;
}

const char loong_linux_exception_gpr_map[] =
    ".equ.L__gpr_num_zero,0"
    ".irpnum,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31"
    ".equ.L__gpr_num_$r\\num,\\num"
    ".endr"
    ".equ.L__gpr_num_$ra,1"
    ".equ.L__gpr_num_$tp,2"
    ".equ.L__gpr_num_$sp,3"
    ".irpnum,0,1,2,3,4,5,6,7"
    ".equ.L__gpr_num_$a\\num,4+\\num"
    ".endr"
    ".irpnum,0,1,2,3,4,5,6,7,8"
    ".equ.L__gpr_num_$t\\num,12+\\num"
    ".endr"
    ".equ.L__gpr_num_$s9,22"
    ".equ.L__gpr_num_$fp,22"
    ".irpnum,0,1,2,3,4,5,6,7,8"
    ".equ.L__gpr_num_$s\\num,23+\\num"
    ".endr";

LoongLinuxFutexRmw loong_linux_futex_rmw_form(
    const char *text, size_t length) {
    static const char *const operations[] = {
        "move$t0,%z5", "add.w$t0,%1,%z5", "or$t0,%1,%z5",
        "and$t0,%1,%z5", "xor$t0,%1,%z5"
    };
    static const char first[] = "1:ll.w%1,%4#__futex_atomic_op";
    static const char loop[] = "2:sc.w$t0,%2beqz$t0,1b3:";
    static const char exception_prefix[] =
        ".pushsection__ex_table,\"a\".balign4";
    static const char exception_suffix[] =
        ".long((3b)-.).short(2)"
        ".short((((.L__gpr_num_%0)<<0)|((.L__gpr_num_zero)<<5)))"
        ".popsection";
    char compact[3600];
    size_t n = 0U, i, form;
    const char *cursor;
    if (!text || length == 0U || length >= sizeof(compact))
        return LOONG_FUTEX_NONE;
    for (i = 0U; i < length; ++i) {
        const char ch = text[i];
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') continue;
        if (ch == '\0' || n + 1U >= sizeof(compact)) return LOONG_FUTEX_NONE;
        compact[n++] = ch;
    }
    compact[n] = '\0';
    if (strncmp(compact, first, sizeof(first) - 1U) != 0)
        return LOONG_FUTEX_NONE;
    for (form = 0U; form < sizeof(operations) / sizeof(operations[0]); ++form) {
        const size_t op_len = strlen(operations[form]);
        cursor = compact + sizeof(first) - 1U;
        if (strncmp(cursor, operations[form], op_len) != 0) continue;
        cursor += op_len;
        if (strncmp(cursor, loop, sizeof(loop) - 1U) != 0) continue;
        cursor += sizeof(loop) - 1U;
        for (i = 0U; i < 2U; ++i) {
            const size_t map_len = sizeof(loong_linux_exception_gpr_map) - 1U;
            if (strncmp(cursor, loong_linux_exception_gpr_map, map_len) != 0)
                break;
            cursor += map_len;
            if (strncmp(cursor, exception_prefix,
                        sizeof(exception_prefix) - 1U) != 0) break;
            cursor += sizeof(exception_prefix) - 1U;
            if (strncmp(cursor, i == 0U ? ".long((1b)-.)" : ".long((2b)-.)",
                        sizeof(".long((1b)-.)") - 1U) != 0) break;
            cursor += sizeof(".long((1b)-.)") - 1U;
            if (strncmp(cursor, exception_suffix,
                        sizeof(exception_suffix) - 1U) != 0) break;
            cursor += sizeof(exception_suffix) - 1U;
        }
        if (i == 2U && *cursor == '\0')
            return (LoongLinuxFutexRmw)(form + 1U);
    }
    return LOONG_FUTEX_NONE;
}

bool loong_linux_futex_cmpxchg_form(const char *text, size_t length) {
    static const char prefix[] =
        "#futex_atomic_cmpxchg_inatomic"
        "1:ll.w%1,%3bne%1,%z4,3fmove$t0,%z5"
        "2:sc.w$t0,%2beqz$t0,1b3:dbar0x700";
    static const char section[] = ".pushsection__ex_table,\"a\".balign4";
    static const char suffix[] =
        ".long((3b)-.).short(2)"
        ".short((((.L__gpr_num_%0)<<0)|((.L__gpr_num_zero)<<5)))"
        ".popsection";
    char compact[3600];
    const char *cursor;
    size_t i, n = 0U;
    if (!text || !length || length >= sizeof(compact)) return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return false;
        compact[n++] = c;
    }
    compact[n] = '\0';
    if (strncmp(compact, prefix, sizeof(prefix) - 1U) != 0) return false;
    cursor = compact + sizeof(prefix) - 1U;
    for (i = 0U; i < 2U; ++i) {
        const size_t map_len = sizeof(loong_linux_exception_gpr_map) - 1U;
        const char *site = i == 0U ? ".long((1b)-.)" : ".long((2b)-.)";
        if (strncmp(cursor, loong_linux_exception_gpr_map, map_len) != 0)
            return false;
        cursor += map_len;
        if (strncmp(cursor, section, sizeof(section) - 1U) != 0)
            return false;
        cursor += sizeof(section) - 1U;
        if (strncmp(cursor, site, strlen(site)) != 0) return false;
        cursor += strlen(site);
        if (strncmp(cursor, suffix, sizeof(suffix) - 1U) != 0) return false;
        cursor += sizeof(suffix) - 1U;
    }
    return *cursor == '\0';
}

bool loong_linux_uaccess_form(
    const char *text, size_t length, LoongLinuxUaccess *result) {
    static const struct {
        const char *prefix;
        bool load;
        unsigned int width;
        const char *opcode;
    } forms[] = {
        {"1:ld.b%1,%22:", true, 1U, "ld.b"},
        {"1:ld.h%1,%22:", true, 2U, "ld.h"},
        {"1:ld.w%1,%22:", true, 4U, "ld.w"},
        {"1:ld.d%1,%22:", true, 8U, "ld.d"},
        {"1:st.b%z2,%1#__put_user_asm2:", false, 1U, "st.b"},
        {"1:st.h%z2,%1#__put_user_asm2:", false, 2U, "st.h"},
        {"1:st.w%z2,%1#__put_user_asm2:", false, 4U, "st.w"},
        {"1:st.d%z2,%1#__put_user_asm2:", false, 8U, "st.d"}
    };
    static const char common_tail[] =
        ".pushsection__ex_table,\"a\""
        ".balign4"
        ".long((1b)-.)"
        ".long((2b)-.)"
        ".short(2)";
    static const char load_tail[] =
        ".short((((.L__gpr_num_%0)<<0)|((.L__gpr_num_%1)<<5)))"
        ".popsection";
    static const char store_tail[] =
        ".short((((.L__gpr_num_%0)<<0)|((.L__gpr_num_zero)<<5)))"
        ".popsection";
    char compact[1500];
    size_t i, n = 0U;

    if (!text || !length || length >= sizeof(compact) || !result) return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return false;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(forms) / sizeof(forms[0]); ++i) {
        const size_t prefix = strlen(forms[i].prefix);
        const size_t map_len = sizeof(loong_linux_exception_gpr_map) - 1U;
        const size_t middle = sizeof(common_tail) - 1U;
        const char *tail = forms[i].load ? load_tail : store_tail;
        const size_t suffix = strlen(tail);
        if (n != prefix + map_len + middle + suffix ||
            memcmp(compact, forms[i].prefix, prefix) != 0 ||
            memcmp(compact + prefix, loong_linux_exception_gpr_map, map_len) != 0 ||
            memcmp(compact + prefix + map_len, common_tail, middle) != 0 ||
            memcmp(compact + prefix + map_len + middle, tail, suffix) != 0)
            continue;
        result->load = forms[i].load;
        result->width = forms[i].width;
        result->opcode = forms[i].opcode;
        return true;
    }
    return false;
}

bool loong_linux_irq_switch_template(
    const char *text, size_t length, bool numeric) {
    static const char source_form[] =
        "move$s0,$spmove$sp,%[stk]move$a0,%[regs]"
        "blhandle_loongarch_irqmove$sp,$s0";
    static const char core_form[] =
        "move$s0,$spmove$sp,%0move$a0,%1"
        "blhandle_loongarch_irqmove$sp,$s0";
    const char *const expected = numeric ? core_form : source_form;
    char compact[256];
    size_t i, n = 0U;
    if (!text || !length || length >= sizeof(compact)) return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == '\n' || c == '\r' || c == '\t' || c == ' ')
            continue;
        if (c == '\0' || c == ';' || n + 1U >= sizeof(compact))
            return false;
        compact[n++] = c;
    }
    compact[n] = '\0';
    return strcmp(compact, expected) == 0;
}

bool loong_linux_irq_switch_clobber(
    const char *name, size_t length, size_t index) {
    char kind;
    unsigned int number;
    if (!name || length != 3U || name[0] != '$' || index >= 18U)
        return false;
    if (index < 8U) {
        kind = 'a';
        number = (unsigned int)index;
    } else if (index == 8U) {
        kind = 's';
        number = 0U;
    } else {
        kind = 't';
        number = (unsigned int)(index - 9U);
    }
    return name[1] == kind && name[2] == (char)('0' + number);
}

bool loong_linux_frametrace_template(
    const char *text, size_t length) {
    char expected[2048];
    char normalized[2048];
    size_t n = 0U, i, j;
    int count;
    if (!text || !length || length >= sizeof(normalized)) return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
            continue;
        if (c == '\0' || c == ';' || n + 1U >= sizeof(normalized))
            return false;
        normalized[n++] = c;
    }
    normalized[n] = '\0';
    count = snprintf(expected, sizeof(expected),
        "st.d$r1,%%1,8*1pcaddi$ra,0st.d$ra,%%0ld.d$ra,%%1,8");
    if (count < 0 || (size_t)count >= sizeof(expected)) return false;
    j = (size_t)count;
    for (i = 2U; i <= 31U; ++i) {
        count = snprintf(expected + j, sizeof(expected) - j,
                         "st.d$r%zu,%%1,8*%zu", i, i);
        if (count < 0 || (size_t)count >= sizeof(expected) - j)
            return false;
        j += (size_t)count;
    }
    return strcmp(normalized, expected) == 0;
}

unsigned int loong_linux_frametrace_csr_form(
    const char *text, size_t length) {
    static const struct { const char *text; unsigned int csr; } forms[] = {
        {"csrrd%0,0x7", 7U},   /* CSR_BADV */
        {"csrrd%0,0x0", 0U},   /* CSR_CRMD */
        {"csrrd%0,0x1", 1U},   /* CSR_PRMD */
        {"csrrd%0,0x2", 2U},   /* CSR_EUEN */
        {"csrrd%0,0x4", 4U},   /* CSR_ECFG */
        {"csrrd%0,0x5", 5U}    /* CSR_ESTAT */
    };
    char compact[64];
    size_t i, n = 0U;
    if (!text || !length || length >= sizeof(compact)) return UINT32_MAX;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
        if (c == '\0' || c == ';' || n + 1U >= sizeof(compact))
            return UINT32_MAX;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(forms)/sizeof(forms[0]); ++i)
        if (strcmp(compact, forms[i].text) == 0) return forms[i].csr;
    return UINT32_MAX;
}

/* All descriptor IDs are source-sealed ordinals. This table is the sole
 * mapping between those stable IDs and the target's exact template parsers;
 * old direct emission owns only code generation, not template admission. */
typedef bool (*LoongExactFormMatch)(const char *, size_t);

static bool loong_match_frametrace_csr(const char *text, size_t length) {
    return loong_linux_frametrace_csr_form(text, length) != UINT32_MAX;
}
static bool loong_match_fpr_transfer(const char *text, size_t length) {
    return loong_linux_fpr_transfer_form(text, length) != 0U;
}
static bool loong_match_percpu_indexed(const char *text, size_t length) {
    return loong_percpu_indexed_form(text, length) != LOONG_PERCPU_NONE;
}
static bool loong_match_cmpxchg(const char *text, size_t length) {
    return loong_linux_cmpxchg_form(text, length) != 0U;
}
static bool loong_match_xchg(const char *text, size_t length) {
    return loong_linux_xchg_form(text, length) != 0U;
}

static const LoongExactFormMatch loong_descriptor_match[] = {
    loong_linux_cacop_form,
    loong_linux_vdso_syscall_form,
    loong_linux_frametrace_template,
    loong_match_frametrace_csr,
    loong_linux_cpucfg_form,
    loong_match_fpr_transfer,
    loong_linux_futex_cmpxchg_form,
    loong_fcsr0_write_form,
    loong_match_percpu_indexed,
    loong_match_cmpxchg,
    loong_match_xchg,
    loong_linux_xchg_small_form,
    loong_mask_nospec_form,
    loong_pte_form
};

_Static_assert(sizeof(loong_descriptor_match) /
                   sizeof(loong_descriptor_match[0]) ==
                   MINIC_LOONG_MACHINE_ASM_CLOCK - 1U,
               "Loong typed descriptor IDs and scalar IDs must be disjoint");

bool minic_loongarch64_descriptor_template_requires_typed_id(
    const char *text, size_t length) {
    size_t i;
    if (text == NULL) return false;
    for (i = 0U; i < sizeof(loong_descriptor_match) /
                          sizeof(loong_descriptor_match[0]); ++i)
        if (loong_descriptor_match[i](text, length)) return true;
    return false;
}

bool minic_loongarch64_typed_descriptor_matches_template(
    uint32_t id, uint32_t variant, const char *text, size_t length) {
    const size_t index = (size_t)(id - 1U);
    if (id == 0U || variant != 0U || text == NULL ||
        index >= sizeof(loong_descriptor_match) /
                     sizeof(loong_descriptor_match[0])) return false;
    return loong_descriptor_match[index](text, length);
}

bool minic_loongarch64_typed_scalar_form_matches_template(
    uint32_t id, uint32_t variant, const char *text, size_t length) {
    LoongLinuxSubPositive sub;
    LoongLinuxUaccess uaccess;
    const struct MinicLoongAtomicTemplate *atom;
    const char *literal;
    size_t percpu;
    if (text == NULL || variant == 0U) return false;
    switch ((MinicLoongLinuxMachineAsmForm)id) {
    case MINIC_LOONG_MACHINE_ASM_CLOCK:
        return variant == (uint32_t)loong_linux_clock_form(text, length);
    case MINIC_LOONG_MACHINE_ASM_IRQ_SWITCH:
        return variant == 1U && loong_linux_irq_switch_template(
            text, length, true);
    case MINIC_LOONG_MACHINE_ASM_INVTLB:
        return variant == loong_linux_invtlb_form(text, length);
    case MINIC_LOONG_MACHINE_ASM_ADD_UNLESS:
        return variant == loong_linux_add_unless_form(text, length);
    case MINIC_LOONG_MACHINE_ASM_FUTEX_RMW:
        return variant == (uint32_t)loong_linux_futex_rmw_form(text, length);
    case MINIC_LOONG_MACHINE_ASM_LITERAL_DBAR:
        literal = loongarch64_literal_dbar(text, length);
        return literal != NULL && variant ==
            (strcmp(literal, "dbar 0") == 0 ? 1U : 2U);
    case MINIC_LOONG_MACHINE_ASM_HINT_DBAR:
        return variant == 1U && loongarch64_dbar_template(text, length);
    case MINIC_LOONG_MACHINE_ASM_SUB_POSITIVE:
        return loong_linux_sub_positive_form(text, length, &sub) &&
            variant == (sub.width == 8U ? 2U : 0U) +
                       (sub.add_negated ? 1U : 2U);
    case MINIC_LOONG_MACHINE_ASM_UACCESS:
        return loong_linux_uaccess_form(text, length, &uaccess) &&
            variant == (uaccess.load ? 0U : 4U) +
                (uint32_t)(uaccess.width == 1U ? 1U :
                    uaccess.width == 2U ? 2U : uaccess.width == 4U ? 3U : 4U);
    case MINIC_LOONG_MACHINE_ASM_PERCPU_AMO:
        percpu = loong_percpu_amo_form(text, length);
        return percpu != SIZE_MAX && variant == (uint32_t)percpu + 1U;
    case MINIC_LOONG_MACHINE_ASM_CSR:
        return variant == loong_csr_template(text, length);
    case MINIC_LOONG_MACHINE_ASM_AMO_STORE:
    case MINIC_LOONG_MACHINE_ASM_AMO_FETCH:
        atom = loong_atomic_template(text, length,
                    id == MINIC_LOONG_MACHINE_ASM_AMO_FETCH);
        return atom != NULL &&
            variant == (uint32_t)(atom - loong_atomic) + 1U;
    default:
        return false;
    }
}

bool minic_loongarch64_scalar_template_requires_typed_id(
    const char *text, size_t length) {
    return loong_linux_clock_form(text, length) != LOONG_CLOCK_NONE ||
        loong_linux_irq_switch_template(text, length, true) ||
        loong_linux_invtlb_form(text, length) != 0U ||
        loong_linux_add_unless_form(text, length) != 0U ||
        loong_linux_futex_rmw_form(text, length) != LOONG_FUTEX_NONE ||
        loongarch64_literal_dbar(text, length) != NULL ||
        loongarch64_dbar_template(text, length) ||
        loong_linux_sub_positive_form(text, length,
                                     &(LoongLinuxSubPositive){0}) ||
        loong_linux_uaccess_form(text, length, &(LoongLinuxUaccess){0}) ||
        loong_percpu_amo_form(text, length) != SIZE_MAX ||
        loong_csr_template(text, length) != 0U ||
        loong_atomic_template(text, length, false) != NULL ||
        loong_atomic_template(text, length, true) != NULL;
}
