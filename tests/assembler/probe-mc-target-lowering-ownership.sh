#!/usr/bin/env bash
# One small public-CLI contract per Target after removing the duplicate
# parser/encoder paths: x86 site modes, A64 MOV/MOVW, Loong LI and RV LI.
# The ISA-specific constants are checked at the emitted ELF boundary rather
# than by source grep alone; GNU x86 is an exact byte oracle on every host.
set -Eeuo pipefail
: "${MINIAS:?MiniAS executable required}"
for cmd in as objcopy readelf python3; do command -v "$cmd" >/dev/null; done

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cat >"$work/x86.s" <<'ASM'
.text
.code32
  pushl %ebp
  lret
.code64
  movq %rax,%rbx
  pushq %r12
ASM
"$MINIAS" --target=x86_64 -o "$work/x86.o" "$work/x86.s"
as --64 -o "$work/x86.gnu.o" "$work/x86.s"
objcopy -O binary -j .text "$work/x86.o" "$work/x86.bin"
objcopy -O binary -j .text "$work/x86.gnu.o" "$work/x86.gnu.bin"
cmp "$work/x86.bin" "$work/x86.gnu.bin"
printf '.text\n.code32\nmovq %%rax, %%rbx\n' >"$work/x86.bad.s"
if "$MINIAS" --target=x86_64 -o "$work/x86.bad.o" \
    "$work/x86.bad.s" 2>"$work/x86.bad.err"; then
    echo 'x86 .code32 permitted 64-bit-only opcode' >&2; exit 1
fi
test ! -e "$work/x86.bad.o"

cat >"$work/a64.s" <<'ASM'
.text
  movz x0, #0x1234
  mov x1, #0x1234
  movn w2, #0
  mov x3, #-1
  movk x4, #0x5678, lsl #16
ASM
"$MINIAS" --target=aarch64 -o "$work/a64.o" "$work/a64.s"
printf '.text\nmovz w0,#65536\n' >"$work/a64.bad.s"
if "$MINIAS" --target=aarch64 -o "$work/a64.bad.o" \
    "$work/a64.bad.s" 2>"$work/a64.bad.err"; then
    echo 'AArch64 MOVZ accepted out-of-range immediate' >&2; exit 1
fi
test ! -e "$work/a64.bad.o"

cat >"$work/loong.s" <<'ASM'
.text
  li.w $r4, 0x12345
  li.d $r5, 0x123456789abcdef0
ASM
"$MINIAS" --target=loongarch64 -o "$work/loong.o" "$work/loong.s"
printf '.text\nli.w $r4, 0x100000000\n' >"$work/loong.bad.s"
if "$MINIAS" --target=loongarch64 -o "$work/loong.bad.o" \
    "$work/loong.bad.s" 2>"$work/loong.bad.err"; then
    echo 'LoongArch LI.W accepted wider-than-32-bit immediate' >&2; exit 1
fi
test ! -e "$work/loong.bad.o"

cat >"$work/rv.s" <<'ASM'
.text
  li a0, 0x12345
  li a1, -1
  li a2, 2048
ASM
"$MINIAS" --target=riscv -march=rv64i -mabi=lp64 \
    -o "$work/rv.o" "$work/rv.s"
printf '.text\nli a0,0x10000000000000000\n' >"$work/rv.bad.s"
if "$MINIAS" --target=riscv -march=rv64i -mabi=lp64 \
    -o "$work/rv.bad.o" "$work/rv.bad.s" 2>"$work/rv.bad.err"; then
    echo 'RV64 LI accepted wider-than-64-bit immediate' >&2; exit 1
fi
test ! -e "$work/rv.bad.o"

python3 - "$work/a64.o" "$work/loong.o" "$work/rv.o" <<'PY'
import re
import struct
import subprocess
import sys

def text_bytes(path):
    dump = subprocess.check_output(('readelf', '-x', '.text', path), text=True)
    data = bytearray()
    for line in dump.splitlines():
        columns = line.split()
        if not columns or not re.fullmatch(r'0x[0-9a-fA-F]+', columns[0]):
            continue
        for word in columns[1:]:
            if not re.fullmatch(r'[0-9a-fA-F]{2,8}', word):
                break
            data.extend(bytes.fromhex(word))
    return bytes(data)

a64 = text_bytes(sys.argv[1])
expected_a64 = (0xd2824680, 0xd2824681, 0x12800002,
                0x92800003, 0xf2aacf04)
if a64 != struct.pack('<5I', *expected_a64):
    raise SystemExit(f'AArch64 MOVZ/MOV alias paths diverged: {a64.hex()}')

loong = text_bytes(sys.argv[2])
if not loong.startswith(struct.pack('<2I', 0x14000244, 0x038d1484)):
    raise SystemExit('LoongArch LI.W typed pair changed unexpectedly: '+loong.hex())
if len(loong) != 24:
    raise SystemExit(f'LoongArch LI.D lowering word count changed: {len(loong)}')

rv = text_bytes(sys.argv[3])
if len(rv) != 20 or rv[:4] != struct.pack('<I', 0x00012537):
    raise SystemExit(f'RV64 LI lowering changed: bytes={rv.hex()}')
print('MINIAS_MC_TARGET_LOWERING=PASS x86_GNU=exact A64_MOVW=5 Loong_LI=6 RV_LI=5 negatives=4')
PY
