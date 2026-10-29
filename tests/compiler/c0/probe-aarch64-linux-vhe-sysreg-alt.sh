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
registers = {"SPSR":0,"ELR":1}
gpr = ("\t.irp\tnum,"+",".join(map(str,range(31)))+"\n"
       "\t.equ\t.L__gpr_num_x\\num, \\num\n"
       "\t.equ\t.L__gpr_num_w\\num, \\num\n"
       "\t.endr\n\t.equ\t.L__gpr_num_xzr, 31\n"
       "\t.equ\t.L__gpr_num_wzr, 31\n")
alt_prefix = ".if 1 == 1\n661:\n\t"
alt_middle = ("\n662:\n.pushsection .altinstructions,\"a\"\n"
              " .word 661b - .\n .word 663f - .\n .hword 50\n"
              " .byte 662b-661b\n .byte 664f-663f\n"
              ".popsection\n.subsection 1\n663:\n\t")
alt_tail = ("\n664:\n.org\t. - (664b-663b) + (662b-661b)\n\t"
            ".org\t. - (662b-661b) + (664b-663b)\n\t"
            ".previous\n.endif\n")
def opcode_reg(op1, op2):
    return "(" + " | ".join("((%d) << %d)" % (x,shift)
        for x,shift in zip((3,op1,4,0,op2),(19,16,12,8,5))) + ")"
def instruction(mode, op1, op2):
    if mode == "read":
        return (gpr+"\t.macro\tmrs_s, rt, sreg\n"
                ".inst (0xd5200000|(\\sreg)|(.L__gpr_num_\\rt))\n"
                "\t.endm\n\tmrs_s %0, "+opcode_reg(op1,op2)+
                "\n\t.purgem\tmrs_s\n")
    return (gpr+"\t.macro\tmsr_s, sreg, rt\n"
            ".inst (0xd5000000|(\\sreg)|(.L__gpr_num_\\rt))\n"
            "\t.endm\n\tmsr_s "+opcode_reg(op1,op2)+
            ", %x0\n\t.purgem\tmsr_s\n")
with open(sys.argv[1],"w") as out:
    for name, op2 in registers.items():
        for mode in ("read","write"):
            template = (alt_prefix+instruction(mode,4,op2)+alt_middle+
                        instruction(mode,0,op2)+alt_tail)
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
PY
"$mini" --target=aarch64-linux -S "$work/vhe.i" -o "$work/vhe.s"
test "$(grep -Fc '.pushsection .altinstructions' "$work/vhe.s")" -eq 4
test "$(grep -Fc '.hword 50' "$work/vhe.s")" -eq 4
test "$(grep -Ec '^  \.inst 0xd53[0-9a-f]{5}$' "$work/vhe.s")" -eq 4
test "$(grep -Ec '^  \.inst 0xd51[0-9a-f]{5}$' "$work/vhe.s")" -eq 4
test "$(grep -Fc '.org . -' "$work/vhe.s")" -eq 8
echo 'AARCH64_VHE_SYSREG_ALT=PASS SPSR+ELR read2 write2 feature50 patches4'
if command -v aarch64-linux-gnu-as >/dev/null 2>&1; then
  aarch64-linux-gnu-as "$work/vhe.s" -o "$work/vhe.o"
  echo 'AARCH64_VHE_SYSREG_ALT_GAS=PASS'
else
  echo 'AARCH64_VHE_SYSREG_ALT_GAS=SKIP'
fi
for invalid in bad-cap bad-sysreg bad-extra bad-constraint; do
  case "$invalid" in
    bad-cap) sed 's/\.hword 50/\.hword 51/g' "$work/vhe.i" > "$work/$invalid.i" ;;
    bad-sysreg) sed 's/((4) << 16)/((5) << 16)/g' "$work/vhe.i" > "$work/$invalid.i" ;;
    bad-extra) sed 's/\.purgem\\tmsr_s/\.purgem\\tmsr_s; nop/g' "$work/vhe.i" > "$work/$invalid.i" ;;
    bad-constraint) sed 's/"rZ"(value)/"m"(value)/g' "$work/vhe.i" > "$work/$invalid.i" ;;
  esac
  if "$mini" --target=aarch64-linux -S "$work/$invalid.i" -o "$work/$invalid.s" \
      >/dev/null 2>"$work/$invalid.err"; then
    echo "AARCH64_VHE_SYSREG_ALT=FAIL unsafe template $invalid accepted" >&2
    exit 1
  fi
  test ! -e "$work/$invalid.s"
done
echo 'AARCH64_VHE_SYSREG_ALT_NEGATIVE=PASS 4/4'
