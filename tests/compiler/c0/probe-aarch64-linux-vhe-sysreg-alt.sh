#!/usr/bin/env bash
# Linux 6.6 KVM VHE feature-50 GNU ALTERNATIVE: full nested MRS_S/MSR_S
# .irp/.equ/.macro/.inst expansion in both boot-time selected arms.
set -Eeuo pipefail
mini=${1:?MiniC compiler}
test -x "$mini"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
python3 - "$work/vhe.i" <<'PY'
import json, sys
# VHE feature 50 aliases the three architectural EL2 banks to EL1.
# ESR_EL2 (S3_4_C5_C2_0) was missing from the exact pair catalog even
# though Linux's original 1028-byte .irp alternative uses that encoding.
registers = {"SPSR":(4,0,0),"ELR":(4,0,1),
             "ESR":(5,2,0),"FAR":(6,0,0)}
timer_registers = {"CNTP_CTL":(2,1),"CNTP_CVAL":(2,2),
                   "CNTV_CTL":(3,1),"CNTV_CVAL":(3,2)}
gpr = ("\t.irp\tnum,"+",".join(map(str,range(31)))+"\n"
       "\t.equ\t.L__gpr_num_x\\num, \\num\n"
       "\t.equ\t.L__gpr_num_w\\num, \\num\n"
       "\t.endr\n\t.equ\t.L__gpr_num_xzr, 31\n"
       "\t.equ\t.L__gpr_num_wzr, 31\n")
alt_prefix = ".if 1 == 1\n661:\n\t"
def alt_middle(capability):
    return ("\n662:\n.pushsection .altinstructions,\"a\"\n"
            " .word 661b - .\n .word 663f - .\n .hword %d\n" % capability+
            " .byte 662b-661b\n .byte 664f-663f\n"
            ".popsection\n.subsection 1\n663:\n\t")
alt_tail = ("\n664:\n.org\t. - (664b-663b) + (662b-661b)\n\t"
            ".org\t. - (662b-661b) + (664b-663b)\n\t"
            ".previous\n.endif\n")
def opcode_reg(op1, crn, crm, op2):
    return "(" + " | ".join("((%d) << %d)" % (x,shift)
        for x,shift in zip((3,op1,crn,crm,op2),(19,16,12,8,5))) + ")"
def instruction(mode, op1, crn, crm, op2):
    if mode == "read":
        return (gpr+"\t.macro\tmrs_s, rt, sreg\n"
                ".inst (0xd5200000|(\\sreg)|(.L__gpr_num_\\rt))\n"
                "\t.endm\n\tmrs_s %0, "+opcode_reg(op1,crn,crm,op2)+
                "\n\t.purgem\tmrs_s\n")
    return (gpr+"\t.macro\tmsr_s, sreg, rt\n"
            ".inst (0xd5000000|(\\sreg)|(.L__gpr_num_\\rt))\n"
            "\t.endm\n\tmsr_s "+opcode_reg(op1,crn,crm,op2)+
            ", %x0\n\t.purgem\tmsr_s\n")
with open(sys.argv[1],"w") as out:
    for name, (crn,crm,op2) in registers.items():
        for mode in ("read","write"):
            template = (alt_prefix+instruction(mode,4,crn,crm,op2)+alt_middle(50)+
                        instruction(mode,0,crn,crm,op2)+alt_tail)
            if mode == "read":
                out.write("unsigned long long vhe_read_%s(void) {\n" % name)
                out.write("  unsigned long long value;\n")
                out.write("  asm volatile(%s : \"=r\"(value));\n" %
                          json.dumps(template))
                out.write("  return value;\n}\n")
            else:
                out.write("void vhe_write_%s(unsigned long long value) {\n" % name)
                out.write("  asm volatile(%s : : \"rZ\"(value));\n}\n" %
                          json.dumps(template))
    # arch/arm64/kvm/arch_timer.c feature47: EL0 timer aliases accessed via
    # S3_3_C14 -> S3_5_C14, both MRS_S and MSR_S, four register pairs.
    for name, (crm,op2) in timer_registers.items():
        for mode in ("read","write"):
            template = (alt_prefix+instruction(mode,3,14,crm,op2)+alt_middle(47)+
                        instruction(mode,5,14,crm,op2)+alt_tail)
            if mode == "read":
                out.write("unsigned long long timer_read_%s(void) {\n" % name)
                out.write("  unsigned long long value;\n")
                out.write("  asm volatile(%s : \"=r\"(value));\n" % json.dumps(template))
                out.write("  return value;\n}\n")
            else:
                out.write("void timer_write_%s(unsigned long long value) {\n" % name)
                out.write("  asm volatile(%s : : \"rZ\"(value));\n}\n" % json.dumps(template))
PY
"$mini" --target=aarch64-linux -S "$work/vhe.i" -o "$work/vhe.s"
test "$(grep -Fc '.pushsection .altinstructions' "$work/vhe.s")" -eq 16
test "$(grep -Fc '.hword 50' "$work/vhe.s")" -eq 8
test "$(grep -Fc '.hword 47' "$work/vhe.s")" -eq 8
test "$(grep -Ec '^  \.inst 0xd53[0-9a-f]{5}$' "$work/vhe.s")" -eq 16
test "$(grep -Ec '^  \.inst 0xd51[0-9a-f]{5}$' "$work/vhe.s")" -eq 16
test "$(grep -Fc '.org . -' "$work/vhe.s")" -eq 32
echo 'AARCH64_VHE_SYSREG_ALT=PASS feature50 read4 write4 feature47 CNT{P,V}_{CTL,CVAL} read4 write4 patches16'
if command -v aarch64-linux-gnu-as >/dev/null 2>&1; then
  aarch64-linux-gnu-as "$work/vhe.s" -o "$work/vhe.o"
  echo 'AARCH64_VHE_SYSREG_ALT_GAS=PASS'
else
  echo 'AARCH64_VHE_SYSREG_ALT_GAS=SKIP'
fi
for invalid in bad-cap bad-sysreg bad-extra bad-constraint bad-timer-cap bad-timer-bank; do
  case "$invalid" in
    bad-cap) sed 's/\.hword 50/\.hword 51/g' "$work/vhe.i" > "$work/$invalid.i" ;;
    bad-sysreg) sed 's/((4) << 16)/((5) << 16)/g' "$work/vhe.i" > "$work/$invalid.i" ;;
    bad-extra) sed 's/\.purgem\\tmsr_s/\.purgem\\tmsr_s; nop/g' "$work/vhe.i" > "$work/$invalid.i" ;;
    bad-constraint) sed 's/"rZ"(value)/"m"(value)/g' "$work/vhe.i" > "$work/$invalid.i" ;;
    bad-timer-cap) sed 's/\.hword 47/\.hword 48/g' "$work/vhe.i" > "$work/$invalid.i" ;;
    bad-timer-bank) sed 's/((3) << 16)/((4) << 16)/g' "$work/vhe.i" > "$work/$invalid.i" ;;
  esac
  if "$mini" --target=aarch64-linux -S "$work/$invalid.i" -o "$work/$invalid.s" \
      >/dev/null 2>"$work/$invalid.err"; then
    echo "AARCH64_VHE_SYSREG_ALT=FAIL unsafe template $invalid accepted" >&2
    exit 1
  fi
  test ! -e "$work/$invalid.s"
done
echo 'AARCH64_VHE_SYSREG_ALT_NEGATIVE=PASS 6/6'
