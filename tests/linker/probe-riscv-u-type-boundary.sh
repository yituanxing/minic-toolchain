#!/usr/bin/env bash
# RV32/64 Linux final-link U-type boundary oracle.  No cross GNU tools,
# gigabyte-sized spacer sections or QEMU are needed: linker script symbols
# provide exact RV64 +/-2GiB and RV32 high-half addresses.  The same RISC-V
# final encoder is used by ordinary ET_EXEC, Linux `-T vmlinux.lds` and PIE.
set -Eeuo pipefail
: "${MINIAS:?set MINIAS to minic-as}"
: "${MINILD:?set MINILD to minic-ld}"

work=$(mktemp -d "${TMPDIR:-/tmp}/minild-riscv-u-boundary.XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT

cat >"$work/call.s" <<'ASM'
.option norelax
.text
.globl _start
.type _start,@function
_start:
    call boundary_target
    ret
.size _start,.-_start
ASM
cat >"$work/absolute.s" <<'ASM'
.option norelax
.text
.globl _start
.type _start,@function
_start:
    lui a0,%hi(boundary_target)
    addi a0,a0,%lo(boundary_target)
    ret
.size _start,.-_start
ASM
cat >"$work/got.s" <<'ASM'
.option pic
.option norelax
.text
.globl _start
.type _start,@function
_start:
    la a0,boundary_target
    ret
.size _start,.-_start
.data
.globl boundary_target
boundary_target:
    .word 7
ASM

for bits in 32 64; do
    abi=lp64
    [[ $bits != 32 ]] || abi=ilp32
    for kind in call absolute got; do
        "$MINIAS" --target=riscv "-march=rv${bits}i" "-mabi=$abi" \
            -o "$work/$bits-$kind.o" "$work/$kind.s"
    done
done

link_case() {
    local bits=$1 kind=$2 name=$3 source_base=$4 target_addr=$5
    local verdict=$6
    local output="$work/$bits-$kind-$name.elf"
    local script="$work/$bits-$kind-$name.lds"
    local log="$work/$bits-$kind-$name.stderr"
    {
        echo 'ENTRY(_start)'
        echo 'SECTIONS {'
        printf '  . = %s;\n' "$source_base"
        echo '  .text : { *(.text) }'
        if [[ $kind == got ]]; then
            echo '  . = ALIGN(0x1000);'
            echo '  .data : { *(.data) }'
            printf '  . = %s;\n' "$target_addr"
            echo '  .got : { *(.got) }'
        else
            printf '  PROVIDE(boundary_target = %s);\n' "$target_addr"
        fi
        echo '}'
    } >"$script"
    local status=0
    "$MINILD" "-melf${bits}lriscv" -static -e _start \
        -T "$script" -o "$output" "$work/$bits-$kind.o" \
        >"$work/$bits-$kind-$name.stdout" 2>"$log" || status=$?
    if [[ $verdict == accept ]]; then
        if ((status != 0)) || [[ ! -s $output ]]; then
            cat "$log" >&2
            echo "MINILD_RISCV_U_BOUNDARY=FAIL stage=$name kind=$kind rv=$bits expected=accept" >&2
            exit 1
        fi
        readelf -h "$output" | grep -Eq 'Machine:.*RISC-V'
        echo "MINILD_RISCV_U_BOUNDARY=PASS stage=$name kind=$kind rv=$bits verdict=accept"
    else
        if ((status == 0)) || [[ -e $output ]]; then
            echo "MINILD_RISCV_U_BOUNDARY=FAIL stage=$name kind=$kind rv=$bits expected=reject" >&2
            exit 1
        fi
        grep -Fq 'R_RISCV_U-type-overflow' "$log" || {
            cat "$log" >&2
            echo "MINILD_RISCV_U_BOUNDARY=FAIL stage=$name kind=$kind rv=$bits wrong_failure" >&2
            exit 1
        }
        echo "MINILD_RISCV_U_BOUNDARY=PASS stage=$name kind=$kind rv=$bits verdict=reject"
    fi
}

# RV64 AUIPC/JALR can encode exactly [-0x80000800, 0x7ffff7ff]
# after HI20 rounding.  The instruction address is 0x10000 in this pool.
link_case 64 call plus_edge     0x10000 0x8000f7fe accept
link_case 64 call plus_overflow 0x10000 0x8000f800 reject
link_case 64 call minus_edge    0x10000 0xffffffff8000f800 accept
link_case 64 call minus_overflow 0x10000 0xffffffff8000f7fe reject
# At the RV64 Linux kernel's negative half, the same small PC displacement
# must work without casting an unsigned high-half address to signed early.
link_case 64 call high_half     0xffffffff80000000 0xffffffff80010000 accept

# RV32 U-type is modulo 2^32, so high-half ILP32 kernel addresses remain
# expressible even if the unwrapped 64-bit signed displacement exceeds 2 GiB.
link_case 32 call plus_wrap     0x10000 0x8000f800 accept
link_case 32 call kernel_half   0x10000 0xc0000000 accept

# LUI+LO12 has the same rounded-high ABI distinction: RV64 must not silently
# reinterpret +0x80000000 as -0x80000000, but RV32 represents it correctly.
link_case 64 absolute plus_edge     0x10000 0x7ffff7ff accept
link_case 64 absolute plus_overflow 0x10000 0x7ffff800 reject
link_case 64 absolute minus_edge    0x10000 0xffffffff7ffff800 accept
link_case 64 absolute minus_overflow 0x10000 0xffffffff7ffff7fe reject
link_case 32 absolute kernel_half    0x10000 0xc0000000 accept
link_case 32 absolute round_wrap     0x10000 0x7fffffff accept

# GOT_HI20 uses the final reserved GOT slot address, *not* the referenced
# symbol's address.  A script can move that slot beyond AUIPC's RV64 reach.
link_case 64 got near_slot 0x10000 0x20000 accept
link_case 64 got far_slot  0x10000 0x8000f800 reject
link_case 32 got wrap_slot 0x10000 0xc0000000 accept

# An unrelaxed CALL is still a genuine AUIPC/JALR relocation.  Alter one
# opcode in the existing object without changing its relocation or symbol.
python3 - "$work/64-call.o" "$work/malformed-call.o" <<'PY'
from pathlib import Path
import struct
import sys

data = bytearray(Path(sys.argv[1]).read_bytes())
assert data[:5] == b'\x7fELF\x02'
section_offset, = struct.unpack_from('<Q', data, 40)
count, string_index = struct.unpack_from('<HH', data, 60)
headers = [struct.unpack_from('<IIQQQQIIQQ', data, section_offset+i*64)
           for i in range(count)]
names = headers[string_index]
string_table = data[names[4]:names[4]+names[5]]
name = lambda h: string_table[h[0]:string_table.index(0, h[0])]
text = next(h for h in headers if name(h) == b'.text')
first, = struct.unpack_from('<I', data, text[4])
assert first & 0x7f == 0x17, hex(first)
struct.pack_into('<I', data, text[4], (first & ~0x7f) | 0x37)
Path(sys.argv[2]).write_bytes(data)
PY
cat >"$work/malformed.lds" <<'SCRIPT'
ENTRY(_start)
SECTIONS {
  . = 0x10000;
  .text : { *(.text) }
  PROVIDE(boundary_target = 0x11000);
}
SCRIPT
if "$MINILD" -melf64lriscv -static -e _start \
      -T "$work/malformed.lds" -o "$work/malformed.elf" \
      "$work/malformed-call.o" >"$work/malformed.stdout" \
      2>"$work/malformed.stderr"; then
    echo 'MINILD_RISCV_U_BOUNDARY=FAIL malformed_unrelaxed_call_accepted' >&2
    exit 1
fi
test ! -e "$work/malformed.elf"
grep -Fq 'R_RISCV_CALL-invalid-pair' "$work/malformed.stderr"
echo 'MINILD_RISCV_U_BOUNDARY=PASS malformed_unrelaxed_CALL=rejected'

echo 'MINILD_RISCV_U_BOUNDARY=PASS xlen=32,64 forms=CALL,HI20,GOT_HI20 high_half=true rounded_limits=true source_C_recompiled=0 runtime=UNVERIFIED'
