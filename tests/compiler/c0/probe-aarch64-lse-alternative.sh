#!/usr/bin/env bash
set -Eeuo pipefail
minic="$1"
test -x "$minic"
d=$(mktemp -d)
if [[ "${MINIC_KEEP_TEST:-0}" == 1 ]]; then
    echo "AARCH64_ALT_TEST_DIR=$d" >&2
else
    trap 'rm -rf "$d"' EXIT
fi
cat > "$d/alternative.c" <<'C'
typedef unsigned int u32;
typedef unsigned long u64;
static u32 v32;
static u64 v64;
void target_barriers(void) {
    asm volatile("dsb st" ::: "memory");
    asm volatile("dsb ld" ::: "memory");
    asm volatile("dmb oshst" ::: "memory");
    asm volatile("hint #6");
}
/* Linux entry-common.c erratum 1463225 manipulates debug sysregs and
 * clears only the AArch64 DAIF D bit. Preserve exactly that #8 effect. */
void target_debug_erratum(void) {
    u64 reg;
    asm volatile("mrs %0, mdscr_el1" : "=r"(reg));
    asm volatile("msr mdscr_el1, %x0" :: "rZ"(reg));
    asm volatile("msr daifclr, #8");
    asm volatile("isb" ::: "memory");
}
/* Genuine 6.6 KVM hyp/switch.h privileged EL2 trap/debug controls. */
u64 target_hyp_switch(u64 v) {
    u64 old;
    asm volatile("mrs %0, fpexc32_el2" : "=r"(old));
    asm volatile("mrs %0, mdcr_el2" : "=r"(old));
    asm volatile("msr fpexc32_el2, %x0" :: "rZ"(v));
    asm volatile("msr hstr_el2, %x0" :: "rZ"(v));
    asm volatile("msr mdcr_el2, %x0" :: "rZ"(v));
    return old;
}
u32 target_bitrev(u32 x) {
    asm("rbit %w0, %w1" : "=r"(x) : "r"(x));
    return x;
}
/* Linux 6.6 alternative-macros.h's exact in-C metadata and length guards. */
#define ALT(old, new) \
    ".if 1 == 1\n661:\n\t" old "\n" \
    "662:\n.pushsection .altinstructions,\"a\"\n" \
    " .word 661b - .\n .word 663f - .\n .hword 33\n" \
    " .byte 662b-661b\n .byte 664f-663f\n" \
    ".popsection\n.subsection 1\n663:\n\t" new "\n" \
    "664:\n\t.org\t. - (664b-663b) + (662b-661b)\n\t" \
    ".org\t. - (662b-661b) + (664b-663b)\n\t" \
    ".previous\n.endif\n"
u64 alt_xchg_mb_64(u64 x) {
    u64 ret, tmp;
    asm volatile(ALT(
        "\tprfm\tpstl1strm, %2\n"
        "1:\tldxr\t%0, %2\n"
        "\tstlxr\t%w1, %3, %2\n"
        "\tcbnz\t%w1, 1b\n"
        "\tdmb ish",
        ".arch_extension lse\n"
        "\tswpal\t%3, %0, %2\n"
        "\t.rept 3\n\tnop\n\t.endr\n\tnop")
        : "=&r"(ret), "=&r"(tmp), "+Q"(v64) : "r"(x) : "memory");
    return ret;
}
u32 alt_xchg_relaxed_32(u32 x) {
    u32 ret; u64 tmp;
    asm volatile(ALT(
        "\tprfm\tpstl1strm, %2\n"
        "1:\tldxr\t%w0, %2\n"
        "\tstxr\t%w1, %w3, %2\n"
        "\tcbnz\t%w1, 1b\n",
        ".arch_extension lse\n"
        "\tswp\t%w3, %w0, %2\n"
        "\t.rept 3\n\tnop\n\t.endr\n")
        : "=&r"(ret), "=&r"(tmp), "+Q"(v32) : "r"(x));
    return ret;
}
void alt_percpu_add_64(u64 val) {
    u32 loop;
    u64 tmp;
    asm volatile(ALT(
        "1:\tldxr\t%[tmp], %[ptr]\n"
        "add\t%[tmp], %[tmp], %[val]\n"
        "\tstxr\t%w[loop], %[tmp], %[ptr]\n"
        "\tcbnz\t%w[loop], 1b",
        ".arch_extension lse\n"
        "stadd\t%[val], %[ptr]\n"
        ".rept 3\nnop\n.endr\n")
        : [loop] "=&r"(loop), [tmp] "=&r"(tmp), [ptr] "+Q"(v64)
        : [val] "r"(val));
}
u32 alt_percpu_add_return_32(u32 val) {
    u32 loop, ret;
    asm volatile(ALT(
        "1:\tldxr\t%w[ret], %[ptr]\n"
        "add\t%w[ret], %w[ret], %w[val]\n"
        "\tstxr\t%w[loop], %w[ret], %[ptr]\n"
        "\tcbnz\t%w[loop], 1b",
        ".arch_extension lse\n"
        "ldadd\t%w[val], %w[ret], %[ptr]\n"
        "add\t%w[ret], %w[ret], %w[val]\n"
        ".rept 2\nnop\n.endr\n")
        : [loop] "=&r"(loop), [ret] "=&r"(ret), [ptr] "+Q"(v32)
        : [val] "r"(val));
    return ret;
}
C
cc -E -P -x c "$d/alternative.c" -o "$d/alternative.i"
"$minic" --target=aarch64-linux -S "$d/alternative.i" -o "$d/alternative.s"
for needle in '.hword 33' '.pushsection .altinstructions' \
    'swpal x11, x9, [x10]' 'swp w11, w9, [x10]' \
    'stadd x11, [x10]' 'ldadd w11, w9, [x10]' \
    '.org . - (664b-663b) + (662b-661b)' \
    'dsb st' 'dsb ld' 'dmb oshst' 'hint #6' 'rbit w9, w9' \
    'mrs x9, mdscr_el1' 'msr mdscr_el1, x9' 'msr daifclr, #8' \
    'mrs x9, fpexc32_el2' 'mrs x9, mdcr_el2' \
    'msr fpexc32_el2, x9' 'msr hstr_el2, x9' 'msr mdcr_el2, x9'; do
    grep -Fq "$needle" "$d/alternative.s" || {
        echo "AARCH64_ALT_LSE_MISSING=$needle" >&2; exit 1;
    }
done
sed 's/\.hword 33/\.hword 34/g' "$d/alternative.i" > "$d/invalid-cap.i"
if "$minic" --target=aarch64-linux -S "$d/invalid-cap.i" \
    -o "$d/invalid-cap.s" >/dev/null 2>&1; then
    echo 'AArch64 alternative admitted unsupported CPU capability' >&2
    exit 1
fi
sed 's/swpal/swpa/g' "$d/alternative.i" > "$d/invalid-order.i"
if "$minic" --target=aarch64-linux -S "$d/invalid-order.i" \
    -o "$d/invalid-order.s" >/dev/null 2>&1; then
    echo 'AArch64 alternative admitted wrong hardware memory order' >&2
    exit 1
fi
sed 's/hint #6/hint #7/g' "$d/alternative.i" > "$d/invalid-hint.i"
if "$minic" --target=aarch64-linux -S "$d/invalid-hint.i" \
    -o "$d/invalid-hint.s" >/dev/null 2>&1; then
    echo 'AArch64 opaque ISA gate admitted unknown Linux HINT opcode' >&2
    exit 1
fi
sed 's/dsb st/dsb xyz/g' "$d/alternative.i" > "$d/invalid-dsb.i"
if "$minic" --target=aarch64-linux -S "$d/invalid-dsb.i" \
    -o "$d/invalid-dsb.s" >/dev/null 2>&1; then
    echo 'AArch64 opaque ISA gate admitted invalid DSB option' >&2
    exit 1
fi
sed 's/msr daifclr, #8/msr daifclr, #7/g' "$d/alternative.i" > "$d/invalid-daif.i"
if "$minic" --target=aarch64-linux -S "$d/invalid-daif.i" \
    -o "$d/invalid-daif.s" >/dev/null 2>&1; then
    echo 'AArch64 privileged DAIF gate admitted unsupported #7 mask' >&2
    exit 1
fi
sed 's/mdcr_el2/alien_el2/g' "$d/alternative.i" > "$d/invalid-sysreg.i"
if "$minic" --target=aarch64-linux -S "$d/invalid-sysreg.i" \
    -o "$d/invalid-sysreg.s" >/dev/null 2>&1; then
    echo 'AArch64 sysreg gate admitted unknown privileged EL2 register' >&2
    exit 1
fi
echo 'AARCH64_LSE_ALTERNATIVE=PASS xchg=2 percpu=2 metadata=preserved negatives=6'
