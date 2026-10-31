#!/usr/bin/env bash
set -Eeuo pipefail
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
mini=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
# The original GNU macro statement is too long for a literal one-line
# negative fixture. Generate the two exact sysreg.h expansions and keep
# ALL section PREL32/length relocation metadata in the source text.
python3 - "$work/vhe.i" <<'PY'
import json, sys
gpr = ('\t.irp\tnum,' + ','.join(map(str,range(31))) + '\n'
       '\t.equ\t.L__gpr_num_x\\num, \\num\n'
       '\t.equ\t.L__gpr_num_w\\num, \\num\n'
       '\t.endr\n\t.equ\t.L__gpr_num_xzr, 31\n'
       '\t.equ\t.L__gpr_num_wzr, 31\n')
prefix = '.if 1 == 1\n661:\n'
tail = ('\n664:\n.org . - (664b-663b) + (662b-661b)\n'
        '.org . - (662b-661b) + (664b-663b)\n.previous\n.endif\n')
def reg(op1,crn,crm,op2):
    return '('+' | '.join(f'(({v}) << {sh})' for v,sh in
        zip((3,op1,crn,crm,op2),(19,16,12,8,5)))+')'
def body(kind,op1,crn,crm,op2):
    if kind == 'read':
        return (gpr+'\t.macro\tmrs_s, rt, sreg\n'
                '.inst (0xd5200000|(\\sreg)|(.L__gpr_num_\\rt))\n'
                '\t.endm\n\tmrs_s %0, '+reg(op1,crn,crm,op2)+'\n'
                '\t.purgem\tmrs_s\n')
    return (gpr+'\t.macro\tmsr_s, sreg, rt\n'
            '.inst (0xd5000000|(\\sreg)|(.L__gpr_num_\\rt))\n'
            '\t.endm\n\tmsr_s '+reg(op1,crn,crm,op2)+', %x0\n'
            '\t.purgem\tmsr_s\n')
def alt(kind, cap, older, newer):
    middle = ('\n662:\n.pushsection .altinstructions,"a"\n'
              f'.word 661b - .\n.word 663f - .\n.hword {cap}\n'
              '.byte 662b-661b\n.byte 664f-663f\n'
              '.popsection\n.subsection 1\n663:\n')
    return prefix+body(kind,*older)+middle+body(kind,*newer)+tail
pairs = {
    'TTBR0_EL1_to_EL12': ((0,2,0,0),(5,2,0,0)),
    'TCR_EL1_to_EL12': ((0,2,0,2),(5,2,0,2)),
    'SCTLR_EL1_to_EL12': ((0,1,0,0),(5,1,0,0)),
    'ZCR_EL2_to_EL1': ((4,1,2,0),(0,1,2,0)),
}
with open(sys.argv[1], 'w') as fd:
    for cap in (47,50):
        for name,(older,newer) in pairs.items():
            for mode in ('read','write'):
                func=f'kvm_{cap}_{mode}_{name}'
                source=alt(mode,cap,older,newer)
                if mode=='read':
                    fd.write(f'unsigned long long {func}(void) {{\n'
                             'unsigned long long v;\n'
                             f'asm volatile({json.dumps(source)} : "=r"(v));\n'
                             'return v;\n}\n')
                else:
                    fd.write(f'void {func}(unsigned long long v) {{\n'
                             f'asm volatile({json.dumps(source)} : : "rZ"(v));\n'
                             '}\n')
PY
"$mini" --target=aarch64-linux -S "$work/vhe.i" -o "$work/vhe.s"
test "$(grep -c '^  \.hword 47$' "$work/vhe.s")" -eq 8
test "$(grep -c '^  \.hword 50$' "$work/vhe.s")" -eq 8
test "$(grep -Ec '^  \.inst 0xd5[13][0-9a-f]{5}$' "$work/vhe.s")" -eq 32
test "$(grep -Fc '.org . -' "$work/vhe.s")" -eq 32
sed 's/\.hword 47/\.hword 48/g' "$work/vhe.i" > "$work/bad-cap.i"
sed 's/((5) << 16)/((6) << 16)/g' "$work/vhe.i" > "$work/bad-target.i"
for invalid in bad-cap bad-target; do
    if "$mini" --target=aarch64-linux -S "$work/$invalid.i" \
        -o "$work/$invalid.s" >/dev/null 2>&1; then
        echo "FAIL accepted unsafe VHE alternative $invalid" >&2
        exit 1
    fi
done
printf '%s\n' 'PASS KVM VHE ISA pair family 4 regs x read/write x cap47+50 =16, negative=2'
