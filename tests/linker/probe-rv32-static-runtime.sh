#!/usr/bin/env bash
# RV32I/ILP32 ordinary ET_EXEC, before libc. Exact MiniAS ELF32 inputs are
# independently linked by MiniLD and GNU BFD, then run under qemu-riscv32.
# A missing cross GNU LD/QEMU is explicitly UNVERIFIED, never a runtime PASS.
set -Eeuo pipefail
: "${MINIAS:?MINIAS required}"
: "${MINILD:?MINILD required}"

repo=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
gnu_ld=${RISCV_GNU_LD:-riscv64-linux-gnu-ld}
qemu=${QEMU_RV32:-qemu-riscv32}
work=$(mktemp -d "${TMPDIR:-/tmp}/minild-rv32-static.XXXXXX")
cleanup() {
    if [[ ${MINILD_RV32_KEEP:-0} == 1 ]]; then
        echo "MINILD_RV32_ARTIFACTS=$work"
    else
        rm -rf -- "$work"
    fi
}
trap cleanup EXIT

cat >"$work/exit.s" <<'ASM'
.text
.globl _start
.type _start,@function
_start:
    addi a0,zero,0
    addi a7,zero,93
    ecall
.size _start,.-_start
ASM
cat >"$work/entry.s" <<'ASM'
.text
.globl _start
.type _start,@function
_start:
    call fixture_helper
    addi a7,zero,93
    ecall
.size _start,.-_start
ASM
cat >"$work/helper.s" <<'ASM'
.text
.globl fixture_helper
.type fixture_helper,@function
fixture_helper:
    .option push
    .option pic
    la t0,fixture_data
    .option pop
    lw a0,0(t0)
    addi a0,a0,-74
    ret
.size fixture_helper,.-fixture_helper
.data
.p2align 2
.globl fixture_data
fixture_data:
    .word 74
.globl fixture_ptr
fixture_ptr:
    .word fixture_data
ASM

for unit in exit entry helper; do
    "$MINIAS" --target=riscv -march=rv32i -mabi=ilp32 \
        -o "$work/$unit.o" "$work/$unit.s"
    readelf -h "$work/$unit.o" | grep -Eq 'Class:[[:space:]]+ELF32'
done
readelf -Wr "$work/entry.o" | grep -Eq 'R_RISCV_CALL(_PLT)?'
readelf -Wr "$work/helper.o" | grep -Eq 'R_RISCV_GOT_HI20'
readelf -Wr "$work/helper.o" | grep -Eq 'R_RISCV_32[[:space:]]'

for case in exit multi; do
    if [[ $case == exit ]]; then inputs=("$work/exit.o");
    else inputs=("$work/entry.o" "$work/helper.o"); fi
    "$MINILD" -m elf32lriscv -static -e _start \
        -o "$work/$case.minild" "${inputs[@]}"
    "$MINILD" -m elf32lriscv -r -o "$work/$case.partial.o" \
        "${inputs[@]}"
    "$MINILD" -m elf32lriscv -static -e _start \
        -o "$work/$case.roundtrip" "$work/$case.partial.o"
    for flavor in minild roundtrip; do
        readelf -h "$work/$case.$flavor" >"$work/$case.$flavor.header"
        grep -Eq 'Class:[[:space:]]+ELF32' "$work/$case.$flavor.header"
        grep -Eq 'Type:[[:space:]]+EXEC' "$work/$case.$flavor.header"
        grep -Eq 'Machine:[[:space:]]+RISC-V' "$work/$case.$flavor.header"
        readelf -l "$work/$case.$flavor" | grep -q LOAD
    done
    if command -v "$gnu_ld" >/dev/null 2>&1; then
        "$gnu_ld" -m elf32lriscv -static -e _start \
            -o "$work/$case.gnu" "${inputs[@]}"
    elif [[ ${MINILD_REQUIRE_GNU:-0} == 1 ]]; then
        echo "MINILD_RV32_STATIC=FAIL missing_gnu_ld=$gnu_ld" >&2
        exit 127
    fi
done

# RISC-V GOT slot width follows XLEN; RV32 must never materialize 8B slots.
python3 - "$work/multi.minild" <<'PY'
from pathlib import Path
import struct
import sys

data = Path(sys.argv[1]).read_bytes()
assert data[:6] == b'\x7fELF\x01\x01'
shoff = struct.unpack_from('<I', data, 32)[0]
count, shstr = struct.unpack_from('<HH', data, 48)
headers = [struct.unpack_from('<IIIIIIIIII', data, shoff + i*40)
           for i in range(count)]
names = data[headers[shstr][4]:headers[shstr][4]+headers[shstr][5]]
for sh in headers:
    end = names.find(b'\0', sh[0])
    if names[sh[0]:end] == b'.got':
        assert sh[5] == 4 and sh[8] == 4, (sh[5], sh[8])
        break
else:
    raise SystemExit('MINILD_RV32_STATIC=FAIL missing_32bit_got')
PY

# Mutate only one real MiniAS-produced ELF32 relocation type. A 64-bit
# absolute write cannot be admitted in an ELF32 static relocation pass.
python3 - "$work/helper.o" "$work/invalid64.o" <<'PY'
from pathlib import Path
import struct
import sys

blob = bytearray(Path(sys.argv[1]).read_bytes())
shoff = struct.unpack_from('<I', blob, 32)[0]
count = struct.unpack_from('<H', blob, 48)[0]
found = 0
for i in range(count):
    sh = struct.unpack_from('<IIIIIIIIII', blob, shoff + i * 40)
    if sh[1] != 4:
        continue
    for off in range(sh[4], sh[4] + sh[5], 12):
        info = struct.unpack_from('<I', blob, off + 4)[0]
        if info & 0xff == 1:
            struct.pack_into('<I', blob, off + 4, (info & ~0xff) | 2)
            found += 1
assert found == 1, f'expected one R_RISCV_32, found={found}'
Path(sys.argv[2]).write_bytes(blob)
PY
if "$MINILD" -m elf32lriscv -static -e _start \
        -o "$work/rejected.elf" "$work/entry.o" "$work/invalid64.o" \
        >"$work/rejected.stdout" 2>"$work/rejected.stderr" || \
   [[ -e $work/rejected.elf ]] || \
   ! grep -q 'rv32-unsupported-R_RISCV_64' "$work/rejected.stderr"; then
    echo 'MINILD_RV32_STATIC=FAIL illegal_64bit_relocation_not_rejected' >&2
    exit 1
fi

if ! command -v "$qemu" >/dev/null 2>&1 || \
   ! command -v "$gnu_ld" >/dev/null 2>&1; then
    if [[ ${MINILD_REQUIRE_QEMU:-0} == 1 ]]; then
        echo "MINILD_RV32_STATIC=FAIL missing_runner_or_oracle=$qemu,$gnu_ld" >&2
        exit 127
    fi
    echo 'MINILD_RV32_STATIC=UNVERIFIED elf32_et_exec=2 got_word=4 runtime=missing_qemu_or_gnu_bfd'
    exit 0
fi
for case in exit multi; do
    for flavor in gnu minild roundtrip; do
        result=0
        timeout 20s "$qemu" "$work/$case.$flavor" \
            >"$work/$case.$flavor.stdout" \
            2>"$work/$case.$flavor.stderr" || result=$?
        if ((result != 0)) || [[ -s $work/$case.$flavor.stderr ]]; then
            echo "MINILD_RV32_STATIC=FAIL case=$case linker=$flavor exit=$result" >&2
            sed -n '1,12p' "$work/$case.$flavor.stderr" >&2
            exit 1
        fi
    done
    cmp "$work/$case.gnu.stdout" "$work/$case.minild.stdout"
    cmp "$work/$case.gnu.stdout" "$work/$case.roundtrip.stdout"
done
echo 'MINILD_RV32_STATIC=PASS elf32_ilp32=2 gnu_bfd=2 mini_link=4 qemu_run=6 got_word=4 illegal_reloc_rejected=1'
