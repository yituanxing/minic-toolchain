/* Linux 6.6.143 sysreg.h + alternative-macros.h macro bodies, retaining
 * source-faithful GNU .inst, .if, .hword and both .org length guards.
 * ARM64_HAS_PAN=38, ARM64_MTE=53 from tools/gen-cpucaps.awk. */
#define __stringify_1(x) #x
#define __stringify(x) __stringify_1(x)
#define __emit_inst(x) ".inst " __stringify((x)) "\n\t"
#define Op1_shift 16
#define Op2_shift 5
#define PSTATE_Imm_shift 8
#define pstate_field(op1,op2) ((op1) << Op1_shift | (op2) << Op2_shift)
#define PSTATE_PAN pstate_field(0,4)
#define PSTATE_TCO pstate_field(3,4)
#define SET_PSTATE(x,r) __emit_inst(0xd500401f | PSTATE_##r | ((!!x) << PSTATE_Imm_shift))
#define SET_PSTATE_PAN(x) SET_PSTATE((x),PAN)
#define SET_PSTATE_TCO(x) SET_PSTATE((x),TCO)
#define ALT(oldinstr,newinstr,cap,cfg) \
    ".if " #cfg " == 1\n" \
    "661:\n\t" oldinstr "\n" \
    "662:\n" \
    ".pushsection .altinstructions,\"a\"\n" \
    " .word 661b - .\n" \
    " .word 663f - .\n" \
    " .hword " #cap "\n" \
    " .byte 662b-661b\n" \
    " .byte 664f-663f\n" \
    ".popsection\n" \
    ".subsection 1\n" \
    "663:\n\t" newinstr "\n" \
    "664:\n" \
    ".org . - (664b-663b) + (662b-661b)\n" \
    ".org . - (662b-661b) + (664b-663b)\n" \
    ".previous\n.endif\n"
void pan_disable(void) { asm(ALT("nop", SET_PSTATE_PAN(0), 38, 1)); }
void pan_enable(void) { asm(ALT("nop", SET_PSTATE_PAN(1), 38, 1)); }
void tco_disable(void) { asm volatile(ALT("nop", SET_PSTATE_TCO(0), 53, 1)); }
void tco_enable(void) { asm volatile(ALT("nop", SET_PSTATE_TCO(1), 53, 1)); }
void tco_disabled_config(void) { asm volatile(ALT("nop", SET_PSTATE_TCO(1), 53, 0)); }
