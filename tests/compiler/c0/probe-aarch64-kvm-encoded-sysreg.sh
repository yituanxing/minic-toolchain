#!/usr/bin/env bash
# Exact Linux 6.6 KVM hypervisor SYS_* write_sysreg_s() path. Original
# .irp/.equ/.macro expansion must emit physical MSR opcodes, not stubs.
set -Eeuo pipefail
mini=${1:?MiniC compiler}
test -x "$mini"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
python3 - "$work/kvm.i" <<'PY'
import json, sys
prefix = (
    "\t.irp\tnum," + ",".join(map(str,range(31))) + "\n"
    "\t.equ\t.L__gpr_num_x\\num, \\num\n"
    "\t.equ\t.L__gpr_num_w\\num, \\num\n"
    "\t.endr\n"
    "\t.equ\t.L__gpr_num_xzr, 31\n"
    "\t.equ\t.L__gpr_num_wzr, 31\n"
    "\t.macro\tmsr_s, sreg, rt\n"
    ".inst (0xd5000000|(\\sreg)|(.L__gpr_num_\\rt))\n"
    "\t.endm\n"
)
read_prefix = prefix.replace("msr_s, sreg, rt", "mrs_s, rt, sreg").replace(
    "0xd5000000", "0xd5200000")
# From kernel arch/arm64/tools/sysreg, arch/arm64/include/asm/sysreg.h.
regs = [
    ("HFGRTR_EL2",3,4,1,1,4),("HFGWTR_EL2",3,4,1,1,5),
    ("HFGITR_EL2",3,4,1,1,6),("HDFGRTR_EL2",3,4,3,1,4),
    ("HDFGWTR_EL2",3,4,3,1,5),("HCRX_EL2",3,4,1,2,2),
    ("VSESR_EL2",3,4,5,2,3),
    ("APIAKEYLO_EL1",3,0,2,1,0),("APIAKEYHI_EL1",3,0,2,1,1),
    ("APIBKEYLO_EL1",3,0,2,1,2),("APIBKEYHI_EL1",3,0,2,1,3),
    ("APDAKEYLO_EL1",3,0,2,2,0),("APDAKEYHI_EL1",3,0,2,2,1),
    ("APDBKEYLO_EL1",3,0,2,2,2),("APDBKEYHI_EL1",3,0,2,2,3),
    ("APGAKEYLO_EL1",3,0,2,3,0),("APGAKEYHI_EL1",3,0,2,3,1),
    # Linux KVM __kvm_skip_instr and kvm_skip_host_instr restore the
    # architectural SPSR/ELR state through genuine GNU MSR_S macro forms.
    ("SPSR_EL1",3,0,4,0,0),("ELR_EL1",3,0,4,0,1),
    # fpsimd.h SVE ZCR_EL2 passes a macro argument surrounded by one
    # extra pair of parentheses on BOTH MRS_S and MSR_S directions.
    ("ZCR_EL2",3,4,1,2,0),
]
with open(sys.argv[1], "w") as out:
    for name, *fields in regs:
        encoded = "(" + " | ".join(
            "((%d) << %d)" % (v,shift)
            for v,shift in zip(fields,(19,16,12,8,5))) + ")"
        if name == "ZCR_EL2":
            encoded = "(" + encoded + ")"
        template = prefix + "\tmsr_s " + encoded + ", %x0\n\t.purgem\tmsr_s\n"
        out.write("void kvm_%s(unsigned long long value) {\n" % name)
        out.write("  __asm__ __volatile__(%s : : \"rZ\"(value));\n" % json.dumps(template))
        out.write("}\n")
        if name in ("SPSR_EL1", "ELR_EL1", "ZCR_EL2"):
            read_template = (read_prefix + "\tmrs_s %0, " + encoded +
                             "\n\t.purgem\tmrs_s\n")
            out.write("unsigned long long read_%s(void) {\n" % name)
            out.write("  unsigned long long value;\n")
            out.write("  __asm__ __volatile__(%s : \"=r\"(value));\n" %
                      json.dumps(read_template))
            out.write("  return value;\n}\n")
PY
"$mini" --target=aarch64-linux -S "$work/kvm.i" -o "$work/kvm.s"
test "$(grep -Ec '^  \.inst 0xd51[0-9a-f]{5}$' "$work/kvm.s")" -eq 20
test "$(grep -Ec '^  \.inst 0xd53[0-9a-f]{5}$' "$work/kvm.s")" -eq 3
test "$(grep -Fc 'ldr x9, [sp,' "$work/kvm.s")" -ge 20
echo 'AARCH64_KVM_ENCODED_SYSREG=PASS hypervisor=7 pauth=10 SPSR_EL1+ELR_EL1+ZCR_EL2=3 MSR=20 MRS=3 nested_parentheses=1'

if command -v aarch64-linux-gnu-gcc >/dev/null 2>&1; then
  aarch64-linux-gnu-gcc -c -x assembler "$work/kvm.s" -o "$work/kvm.o"
  echo 'AARCH64_KVM_ENCODED_SYSREG_GAS=PASS'
else
  echo 'AARCH64_KVM_ENCODED_SYSREG_GAS=SKIP'
fi
sed 's/((3) << 19)/((2) << 19)/g' "$work/kvm.i" > "$work/invalid-op0.i"
sed 's/msr_s /msr_s ;nop /g' "$work/kvm.i" > "$work/invalid-inject.i"
sed 's/"rZ"(value)/"m"(value)/g' "$work/kvm.i" > "$work/invalid-constraint.i"
sed '/void kvm_ZCR_EL2/,/^}/s/msr_s ((((3)/msr_s (((((3)/' \
    "$work/kvm.i" > "$work/invalid-nesting.i"
for bad in invalid-op0 invalid-inject invalid-constraint invalid-nesting; do
  if "$mini" --target=aarch64-linux -S "$work/$bad.i" -o "$work/$bad.s" \
      >/dev/null 2>"$work/$bad.err"; then
    echo "AARCH64_KVM_SYSREG=FAIL unsafe GNU macro admitted $bad" >&2
    exit 1
  fi
  test ! -e "$work/$bad.s"
done
echo 'AARCH64_KVM_ENCODED_SYSREG_NEGATIVE=PASS encoding3 extra_opcode3 constraints3 nesting1'
