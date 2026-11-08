#!/bin/sh
set -eu
: "${MINIC_CC:?set MINIC_CC to the current minic-cc}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=${BUILD_DIR:-"$root/build/debug"}/tests/riscv-adjacent-private-stack-forward
mkdir -p "$work"
cc -E -P -std=gnu11 \
    "$root/tests/compiler/c0/riscv_adjacent_private_stack_forward.c" \
    -o "$work/input.i"

for target in riscv64-linux riscv32-linux; do
    output="$work/$target.s"
    "$MINIC_CC" --target="$target" -S "$work/input.i" -o "$output"
    python3 - "$output" "$target" <<'PY_VERIFY'
import re
import sys
from pathlib import Path

lines = Path(sys.argv[1]).read_text().splitlines()
target = sys.argv[2]
word_store, word_load = ('sd', 'ld') if target == 'riscv64-linux' else ('sw', 'lw')

def body(name):
    start = lines.index(name + ':')
    end = next((i for i in range(start + 1, len(lines))
                if lines[i].startswith('.size ' + name + ',')), None)
    if end is None:
        raise SystemExit('RV_ADJACENT_PRIVATE_STACK=FAIL missing_function=' + name)
    return [line.strip() for line in lines[start:end]]

functions = {name: body('mini_stack_forward_' + name)
             for name in ('chain', 'call', 'asm', 'branch', 'large')}
word = re.compile(r'^(s[dw]|l[dw]) x([0-9]+), ([0-9]+)\(x([0-9]+)\)$')
stores = loads = 0
for name, assembly in functions.items():
    for before, after in zip(assembly, assembly[1:]):
        lhs, rhs = word.fullmatch(before), word.fullmatch(after)
        if lhs is not None and lhs.group(1) == word_store:
            stores += 1
            if (rhs is not None and rhs.group(1) == word_load and
                    lhs.group(2, 3, 4) == rhs.group(2, 3, 4)):
                raise SystemExit('RV_ADJACENT_PRIVATE_STACK=FAIL residual_adjacent_spill=' + name)
        if rhs is not None and rhs.group(1) == word_load:
            loads += 1
if stores < 20 or loads < 20:
    raise SystemExit('RV_ADJACENT_PRIVATE_STACK=FAIL lost_real_stack_accesses=' + target)

# A call's physical clobbers, a source-ASM memory barrier and different
# branch successors must still materialize actual memory operations.
for name, pattern in [('call', r'^jalr ra,'), ('asm', r'^nop$'),
                      ('branch', r'^beqz ')]:
    code = functions[name]
    matching = [i for i, line in enumerate(code) if re.search(pattern, line)]
    if not matching or not any(re.match('^' + word_load + ' ', line)
                               for line in code[matching[0] + 1:]):
        raise SystemExit('RV_ADJACENT_PRIVATE_STACK=FAIL barrier=' + name)
# Both widths must use the out-of-range private frame-transfer helper. The
# scratch register address calculation is a real intervening instruction.
large = functions['large']
if not any((match := re.fullmatch(r'li x31, ([0-9]+)', line)) and
           int(match.group(1)) > 2047 for line in large) or not any(
               line == 'add x31, x2, x31' for line in large):
    raise SystemExit('RV_ADJACENT_PRIVATE_STACK=FAIL large_frame=' + target)
print('RV_ADJACENT_PRIVATE_STACK=PASS target=' + target +
      ' exact_adjacent=elided call_asm_branch=retained large_frame=retained' +
      ' actual_stores=' + str(stores) + ' actual_loads=' + str(loads))
PY_VERIFY
done
