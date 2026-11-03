#!/usr/bin/env bash
# Check final DWARF *addresses*, not only ET_REL ADD16/SUB16 records: a
# linker-shortened CALL must move the source row immediately after the CALL.
set -Eeuo pipefail

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
: "${MINIAS:=$repo/build/release/bin/minic-as}"
test -x "$MINIAS"
rv_as=${RVAS:-riscv64-linux-gnu-as}
rv_ld=${RVLD:-riscv64-linux-gnu-ld}
rv_copy=${RVCOPY:-riscv64-linux-gnu-objcopy}
for tool in "$rv_as" "$rv_ld" "$rv_copy" readelf cmp python3; do
    if ! command -v "$tool" >/dev/null; then
        if [[ ${REQUIRE_ALL_GNU:-0} == 1 ]]; then
            echo "MINIAS_RV_FINAL_DWARF=BLOCKED missing=$tool" >&2
            exit 1
        fi
        echo "MINIAS_RV_FINAL_DWARF=SKIP missing=$tool"
        exit 0
    fi
done

work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
cat >"$work/line-relax.s" <<'ASM'
.file 1 "riscv-line.c"
.option push
.option relax
.text
.globl _start
.type _start,@function
_start:
.loc 1 1 0
    call target
.loc 1 2 0
after_call:
    addi a0,a0,0
    .p2align 4
.loc 1 3 0
target:
    ret
.size _start,.-_start
.option pop
ASM

"$rv_as" -march=rv64i2p1 -mabi=lp64 -mrelax \
    -o "$work/gnu.o" "$work/line-relax.s"
"$MINIAS" --target=riscv -march=rv64i2p1 -mabi=lp64 \
    -o "$work/mini.o" "$work/line-relax.s"

for mode in relax no-relax; do
    for producer in gnu mini; do
        "$rv_ld" -m elf64lriscv "--$mode" -e _start \
            -o "$work/$producer-$mode.elf" "$work/$producer.o"
        "$rv_copy" -O binary -j .text "$work/$producer-$mode.elf" \
            "$work/$producer-$mode.text"
        readelf -Ws "$work/$producer-$mode.elf" |
            awk '$8=="after_call" {print $2}' >"$work/$producer-$mode.after"
        readelf -Ws "$work/$producer-$mode.elf" |
            awk '$8=="target" {print $2}' >"$work/$producer-$mode.target"
        test -s "$work/$producer-$mode.after"
        test -s "$work/$producer-$mode.target"
        readelf --debug-dump=decodedline "$work/$producer-$mode.elf" \
            >"$work/$producer-$mode.lines"
    done
    cmp "$work/gnu-$mode.text" "$work/mini-$mode.text"
    cmp "$work/gnu-$mode.after" "$work/mini-$mode.after"
    cmp "$work/gnu-$mode.target" "$work/mini-$mode.target"
    python3 - "$work/gnu-$mode.lines" "$work/mini-$mode.lines" \
        "$work/gnu-$mode.after" "$work/gnu-$mode.target" <<'PY'
from pathlib import Path
import re
import sys

def rows(path):
    result=[]
    for line in Path(path).read_text().splitlines():
        match=re.match(r'^\s*\S*riscv-line\.c\s+(\d+)\s+(0x[0-9a-fA-F]+)\b', line)
        if match:
            result.append((int(match.group(1)),int(match.group(2),16)))
    return result

gnu, mini = rows(sys.argv[1]), rows(sys.argv[2])
after = int(Path(sys.argv[3]).read_text().strip(), 16)
target = int(Path(sys.argv[4]).read_text().strip(), 16)
assert gnu and mini and gnu == mini, ('GNU/MiniAS final source rows', gnu, mini)
assert target % 16 == 0, ('target lost .p2align 4', hex(target))
assert any(line == 2 and pc == after for line, pc in mini), \
    ('post-CALL line did not track linker relaxation', mini, hex(after))
assert any(line == 3 and pc == target for line, pc in mini), \
    ('aligned target line misplaced', mini, hex(target))
PY
done

# Prove the fixture really exercised relaxation; both links passing while
# retaining the same PC would be a false-green DWARF test.
if cmp -s "$work/gnu-relax.after" "$work/gnu-no-relax.after"; then
    echo 'MINIAS_RV_FINAL_DWARF=FAIL GNU CALL was not shortened' >&2
    exit 1
fi
echo 'MINIAS_RV_FINAL_DWARF=PASS final_text_symbols_and_line_PCs=exact linker_modes=2 actual_shrink=1'
