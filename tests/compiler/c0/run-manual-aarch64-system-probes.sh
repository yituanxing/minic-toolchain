#!/usr/bin/env bash
# Named manual probes; shared initialization is exact, case assertions are unchanged.
if [ "${1:-}" = "--list" ]; then
    printf '%s\n' 'probe-aarch64-kvm-encoded-sysreg' 'probe-aarch64-linux-kvm-barrier' 'probe-aarch64-linux-live-sp' 'probe-aarch64-linux-topology-amu' 'probe-aarch64-linux-vhe-sysreg-alt'
    exit 0
fi
__minic_probe_case=${1:?probe case required; use --list}
shift
case "$__minic_probe_case" in
  probe-aarch64-kvm-encoded-sysreg|probe-aarch64-linux-kvm-barrier|probe-aarch64-linux-live-sp|probe-aarch64-linux-topology-amu|probe-aarch64-linux-vhe-sysreg-alt) ;;
  *) printf "%s\n" "unknown probe: $__minic_probe_case" >&2; exit 64 ;;
esac
set -Eeuo pipefail
mini=${1:?MiniC compiler}
test -x "$mini"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
case "$__minic_probe_case" in
probe-aarch64-kvm-encoded-sysreg)
# Exact Linux 6.6 KVM hypervisor SYS_* write_sysreg_s() path. Original
# .irp/.equ/.macro expansion must emit physical MSR opcodes, not stubs.
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
;;
probe-aarch64-linux-kvm-barrier)
# Exact Linux 6.6 KVM/arm64 zero-input GNU ALTERNATIVE contracts:
# 71 NOP->DMB SY for PAR_EL1 erratum and 95 NOP->ISB for stage-2 updates.
cat > "$work/kvm-barrier.c" <<'C'
#define ALT(cap, new_instruction) \
  ".if 1 == 1\n" \
  "661:\n\t" "nop" "\n" \
  "662:\n" \
  ".pushsection .altinstructions,\"a\"\n" \
  " .word 661b - .\n .word 663f - .\n" \
  " .hword " #cap "\n" \
  " .byte 662b-661b\n .byte 664f-663f\n" \
  ".popsection\n.subsection 1\n" \
  "663:\n\t" new_instruction "\n" \
  "664:\n" \
  ".org\t. - (664b-663b) + (662b-661b)\n\t" \
  ".org\t. - (662b-661b) + (664b-663b)\n\t" \
  ".previous\n.endif\n"
void kvm_par_barrier(void) { asm(ALT(71, "dmb sy")); }
void kvm_stage2_barrier(void) { asm(ALT(95, "isb")); }
C
cc -E -P -x c "$work/kvm-barrier.c" -o "$work/kvm-barrier.i"
"$mini" --target=aarch64-linux -S "$work/kvm-barrier.i" \
  -o "$work/kvm-barrier.s"
test "$(grep -Fc '.pushsection .altinstructions' "$work/kvm-barrier.s")" -eq 2
test "$(grep -Fc '.word 661b - .' "$work/kvm-barrier.s")" -eq 2
test "$(grep -Fc '.word 663f - .' "$work/kvm-barrier.s")" -eq 2
test "$(grep -Fc '.hword 71' "$work/kvm-barrier.s")" -eq 1
test "$(grep -Fc '.hword 95' "$work/kvm-barrier.s")" -eq 1
test "$(grep -Ec '^  dmb sy$' "$work/kvm-barrier.s")" -eq 1
test "$(grep -Ec '^  isb$' "$work/kvm-barrier.s")" -eq 1
test "$(grep -Fc '.org . -' "$work/kvm-barrier.s")" -eq 4
echo 'AARCH64_KVM_BARRIER=PASS cap71_NOP_DMB_SY=1 cap95_NOP_ISB=1 records=2'
if command -v aarch64-linux-gnu-as >/dev/null 2>&1; then
  aarch64-linux-gnu-as "$work/kvm-barrier.s" -o "$work/kvm-barrier.o"
  echo 'AARCH64_KVM_BARRIER_GAS=PASS'
else
  echo 'AARCH64_KVM_BARRIER_GAS=SKIP'
fi
for changed in wrong-cap wrong-op missing-org extra-instr; do
  case "$changed" in
    wrong-cap) sed 's/"71"/"72"/g' "$work/kvm-barrier.i" > "$work/$changed.i" ;;
    wrong-op) sed 's/dmb sy/dsb ish/g' "$work/kvm-barrier.i" > "$work/$changed.i" ;;
    missing-org) sed 's/\.org/\.Xorg/g' "$work/kvm-barrier.i" > "$work/$changed.i" ;;
    extra-instr) sed 's/isb/isb; brk #0/g' "$work/kvm-barrier.i" > "$work/$changed.i" ;;
  esac
  if "$mini" --target=aarch64-linux -S "$work/$changed.i" \
      -o "$work/$changed.s" >/dev/null 2>"$work/$changed.err"; then
    echo "AARCH64_KVM_BARRIER=FAIL unexpected $changed accepted" >&2
    exit 1
  fi
  test ! -e "$work/$changed.s"
done
echo 'AARCH64_KVM_BARRIER_NEGATIVE=PASS 4/4'
;;
probe-aarch64-linux-live-sp)
# Linux 6.6 arm64 stacktrace/current_stack_pointer fixed GNU register alias.
# The target must read the live hardware SP; it must never permit writing it.
cat > "$work/live-sp.i" <<'C'
register unsigned long current_stack_pointer asm("sp");
unsigned long aarch_stack_snapshot(void) {
    return current_stack_pointer;
}
C
"$mini" --target=aarch64-linux -S "$work/live-sp.i" -o "$work/live-sp.s"
test "$(grep -Ec '^  mov x9, sp$' "$work/live-sp.s")" -eq 1
echo 'AARCH64_LIVE_SP=PASS physical=mov_x9_sp global_register_alias=sp'
cat > "$work/illegal-sp-write.i" <<'C'
register unsigned long current_stack_pointer asm("sp");
void bad_sp_write(unsigned long input) { current_stack_pointer = input; }
C
cat > "$work/illegal-x29.i" <<'C'
register unsigned long frame_pointer asm("x29");
unsigned long bad_saved_register(void) { return frame_pointer; }
C
cat > "$work/illegal-typemismatch.i" <<'C'
register unsigned int short_sp asm("sp");
unsigned int bad_sp_width(void) { return short_sp; }
C
for invalid in illegal-sp-write illegal-x29 illegal-typemismatch; do
  if "$mini" --target=aarch64-linux -S "$work/$invalid.i" -o "$work/$invalid.s" \
      >/dev/null 2>"$work/$invalid.err"; then
    echo "AARCH64_LIVE_SP=FAIL invalid case $invalid was admitted" >&2
    exit 1
  fi
  test ! -e "$work/$invalid.s"
done
echo 'AARCH64_LIVE_SP_NEGATIVE=PASS write=suppressed x29=blocked width=checked'
;;
probe-aarch64-linux-topology-amu)
# Exact Linux 6.6 topology.c read_sysreg_s SYS_AMEVCNTR0{0,1}_EL0.
# Keep the complete .irp/.equ/.macro GNU preamble and computed register
# operand; do not admit general assembler expressions or directive injection.
python3 - "$work" <<'PY'
import json
from pathlib import Path
import sys

work = Path(sys.argv[1])
prefix = (
    "\t.irp\tnum," + ",".join(map(str, range(31))) + "\n"
    "\t.equ\t.L__gpr_num_x\\num, \\num\n"
    "\t.equ\t.L__gpr_num_w\\num, \\num\n"
    "\t.endr\n"
    "\t.equ\t.L__gpr_num_xzr, 31\n"
    "\t.equ\t.L__gpr_num_wzr, 31\n"
    "\t.macro\tmrs_s, rt, sreg\n"
    ".inst (0xd5200000|(\\sreg)|(.L__gpr_num_\\rt))\n"
    "\t.endm\n"
)
suffix = "\n\t.purgem\tmrs_s\n"

def expression(index):
    return ("(((3) << 19) | ((3) << 16) | ((13) << 12) | "
            "(((4 + ((%d) >> 3))) << 8) | ((((%d) & 7)) << 5))"
            % (index, index))

def source(name, template, constraint='=r'):
    return ("unsigned long long %s(void) {\n"
            "  unsigned long long result;\n"
            "  __asm__ __volatile__(%s : %s(result));\n"
            "  return result;\n}\n" %
            (name, json.dumps(template), json.dumps(constraint)))

valid = ''.join(source("topology_amu_%d" % n,
                       prefix + "\tmrs_s %0, " + expression(n) + suffix)
                for n in (0, 1))
(work / 'valid.i').write_text(valid)
template = prefix + "\tmrs_s %0, " + expression(0) + suffix
cases = {
    'bad-index': prefix + "\tmrs_s %0, " + expression(2) + suffix,
    'bad-base': template.replace('4 + ((0)', '5 + ((0)'),
    'bad-mask': template.replace(' & 7)', ' & 15)'),
    'bad-mixed-index': template.replace('((0) & 7)', '((1) & 7)'),
    'injected-opcode': template.replace('\n\t.purgem', '\n\tnop\n\t.purgem'),
    'bad-preamble': template.replace('.equ\t.L__gpr_num_xzr, 31',
                                      '.equ\t.L__gpr_num_xzr, 30'),
}
for name, bad in cases.items():
    (work / (name + '.i')).write_text(source(name.replace('-', '_'), bad))
(work / 'bad-constraint.i').write_text(source('bad_constraint', template, '=m'))
PY
"$mini" --target=aarch64-linux -S "$work/valid.i" -o "$work/valid.s"
test "$(grep -Fc '  .inst 0xd53bd409' "$work/valid.s")" -eq 1
test "$(grep -Fc '  .inst 0xd53bd429' "$work/valid.s")" -eq 1
test "$(grep -Fc '  str x9, [x10]' "$work/valid.s")" -eq 2
echo 'AARCH64_TOPOLOGY_AMU=PASS exact 0/1 computed-index read MRS_S'
if command -v aarch64-linux-gnu-as >/dev/null 2>&1; then
  aarch64-linux-gnu-as "$work/valid.s" -o "$work/valid.o"
  echo 'AARCH64_TOPOLOGY_AMU_GAS=PASS'
else
  echo 'AARCH64_TOPOLOGY_AMU_GAS=SKIP'
fi
for bad in bad-index bad-base bad-mask bad-mixed-index injected-opcode \
           bad-preamble bad-constraint; do
  if "$mini" --target=aarch64-linux -S "$work/$bad.i" -o "$work/$bad.s" \
      >/dev/null 2>"$work/$bad.err"; then
    echo "AARCH64_TOPOLOGY_AMU=FAIL unsafe GNU template admitted $bad" >&2
    exit 1
  fi
  test ! -e "$work/$bad.s"
done
echo 'AARCH64_TOPOLOGY_AMU_NEGATIVE=PASS 7/7'
;;
probe-aarch64-linux-vhe-sysreg-alt)
# Linux 6.6 KVM VHE feature-50 GNU ALTERNATIVE: full nested MRS_S/MSR_S
# .irp/.equ/.macro/.inst expansion in both boot-time selected arms.
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
test "$(grep -Ec '^  \.inst 0xd5[13][0-9a-f]{4}9$' "$work/vhe.s")" -eq 32
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
;;
esac
