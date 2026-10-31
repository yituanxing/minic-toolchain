#!/usr/bin/env bash
# Exact Linux 6.6 topology.c read_sysreg_s SYS_AMEVCNTR0{0,1}_EL0.
# Keep the complete .irp/.equ/.macro GNU preamble and computed register
# operand; do not admit general assembler expressions or directive injection.
set -Eeuo pipefail
mini=${1:?MiniC compiler}
test -x "$mini"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
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
