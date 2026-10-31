/* Original Linux 6.6 arch/arm64/include/asm/alternative-macros.h.
 * The default feature path is NOP; the alternative table records a branch
 * to a named C label for Linux boot-time capability patching. */
#define STRINGIFY_1(x) #x
#define STRINGIFY(x) STRINGIFY_1(x)
#define __emit_inst(x) ".inst " STRINGIFY((x)) "\n\t"
#define Op1_shift 16
#define Op2_shift 5
#define CRm_shift 8
#define pstate_field(op1, op2) ((op1) << Op1_shift | (op2) << Op2_shift)
#define PSTATE_PAN pstate_field(0, 4)
#define SET_PSTATE(x, r) __emit_inst(0xd500401f | PSTATE_ ## r | ((!!x) << CRm_shift))
#define SET_PSTATE_PAN(x) SET_PSTATE((x), PAN)

#define ALTINSTR_ENTRY(cap)                           \
    " .word 661b - .\n"                              \
    " .word 663f - .\n"                              \
    " .hword " STRINGIFY(cap) "\n"                    \
    " .byte 662b-661b\n"                            \
    " .byte 664f-663f\n"
#define ALTINSTR_ENTRY_CB(cap, cb)                    \
    " .word 661b - .\n"                              \
    " .word " STRINGIFY(cb) "- .\n"                   \
    " .hword " STRINGIFY(cap) "\n"                    \
    " .byte 662b-661b\n"                            \
    " .byte 664f-663f\n"
#define ALTERNATIVE_CFG(oldinstr, newinstr, cap)      \
    ".if 1 == 1\n"                                  \
    "661:\n\t"                                      \
    oldinstr "\n"                                   \
    "662:\n"                                        \
    ".pushsection .altinstructions,\"a\"\n"         \
    ALTINSTR_ENTRY(cap)                              \
    ".popsection\n"                                 \
    ".subsection 1\n"                               \
    "663:\n\t"                                      \
    newinstr "\n"                                   \
    "664:\n\t"                                      \
    ".org\t. - (664b-663b) + (662b-661b)\n\t"      \
    ".org\t. - (662b-661b) + (664b-663b)\n\t"      \
    ".previous\n"                                   \
    ".endif\n"
#define ALTERNATIVE_CB(oldinstr, cap, cb)             \
    ".if 1 == 1\n"                                  \
    "661:\n\t"                                      \
    oldinstr "\n"                                   \
    "662:\n"                                        \
    ".pushsection .altinstructions,\"a\"\n"         \
    ALTINSTR_ENTRY_CB((1 << 15) | (cap), cb)          \
    ".popsection\n"                                 \
    "663:\n\t"                                      \
    "664:\n\t"                                      \
    ".endif\n"

/* ARM64_HAS_PAN is a generated cpucap. The ABI of this exact expansion
 * is checked against the frozen corpus signature; do not assume a v6.6
 * numeric cap still equals Linux 6.6.143's generated number. */
#ifndef PAN_CAP
#define PAN_CAP 38
#endif
#ifdef CHECK_PAN_ALT
void linux_pan_disable(void) {
    __asm__(ALTERNATIVE_CFG("nop", SET_PSTATE_PAN(0), PAN_CAP));
}
void linux_pan_enable(void) {
    __asm__(ALTERNATIVE_CFG("nop", SET_PSTATE_PAN(1), PAN_CAP));
}
#endif

/* Linux alternative_has_cap_unlikely(): this is the *other* NOP family,
 * using asm goto, a named immediate feature and a named C branch label.
 * It occurs in generic Linux objects, not just arch/arm64/uaccess. */
int linux_has_cap_unlikely(void) {
    __asm__ goto (
        ALTERNATIVE_CFG("nop", "b\t%l[l_yes]", %[cpucap])
        :
        : [cpucap] "i" (38UL)
        :
        : l_yes);
    return 0;
l_yes:
    return 1;
}

/* Linux alternative_has_cap_likely(): the original is an unconditional
 * branch; the alternative callback turns that branch into NOPs. The callback
 * address in .altinstructions is a PREL32 relocation, not a dead comment. */
void alt_cb_patch_nops(void) {}
int linux_has_cap_likely(void) {
    __asm__ goto (
        ALTERNATIVE_CB("b\t%l[l_no]", %[cpucap], alt_cb_patch_nops)
        : : [cpucap] "i" (38UL) : : l_no);
    return 1;
l_no:
    return 0;
}

int main(void) {
    /* Before Linux boot-time patching the original NOP falls through. */
    return linux_has_cap_unlikely() == 0 &&
           linux_has_cap_likely() == 0 ? 42 : 1;
}

/* Deferred cpucap is a real Linux pattern but is only -S discovery until
 * MiniC can specialize the inline caller's compile-time immediate. */
#ifdef CHECK_DEFERRED
int linux_has_cap_deferred(const unsigned long cpucap) {
    __asm__ goto (
        ALTERNATIVE_CFG("nop", "b\t%l[l_yes]", %[cpucap])
        : : [cpucap] "i" (cpucap) : : l_yes);
    return 0;
l_yes:
    return 1;
}
int linux_has_cap_likely_deferred(const unsigned long cpucap) {
    __asm__ goto (
        ALTERNATIVE_CB("b\t%l[l_no]", %[cpucap], alt_cb_patch_nops)
        : : [cpucap] "i" (cpucap) : : l_no);
    return 1;
l_no:
    return 0;
}
#endif
