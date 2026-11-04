#!/usr/bin/env bash
# AArch64 variant-I IE GOT: ELF PT_TLS, TP offsets, weak TLS and GNU/QEMU parity.
set -Eeuo pipefail
: "${MINIAS:?MINIAS must point to minic-as}"
: "${MINILD:?MINILD must point to minic-ld}"
for tool in python3 readelf timeout; do command -v "$tool" >/dev/null; done
work=$(mktemp -d "${TMPDIR:-/tmp}/minild-aarch64-tls.XXXXXX")
cleanup() {
    if [[ ${MINILD_AARCH64_STATIC_TLS_KEEP:-0} == 1 ]]; then
        echo "MINILD_AARCH64_STATIC_TLS_ARTIFACTS=$work"
    else
        rm -rf -- "$work"
    fi
}
trap cleanup EXIT
cat >"$work/entry.s" <<'ASM'
.text
.globl _start
.type _start,@function
_start:
    adrp x0,:got:tls_alpha
    ldr x1,[x0,:got_lo12:tls_alpha]
    adrp x0,:got:tls_beta
    ldr x2,[x0,:got_lo12:tls_beta]
    adrp x0,:got:tls_missing
    ldr x3,[x0,:got_lo12:tls_missing]
    adrp x9,tcb_stub
    add x9,x9,:lo12:tcb_stub
    msr tpidr_el0,x9
    mrs x10,tpidr_el0
    add x11,x10,x1
    ldr x11,[x11]
    add x12,x10,x2
    ldr x12,[x12]
    add x0,x11,x12
    mov x8,#93
    svc #0
    // Unreachable instruction pool: relocation types are rewritten below
    // to prove LE MOVW, ADD-HI/LO and LD64, including weak undefined TLS.
    add x20,x20,:lo12:tls_alpha
    movz x14,#0
    add x21,x21,:lo12:tls_beta
    add x21,x21,#0,lsl #12
    add x22,x22,:lo12:tls_beta
    add x23,x23,:lo12:tls_alpha
    ldr x15,[x13]
    add x24,x24,:lo12:tls_missing
.size _start,.-_start
.type tls_alpha,@tls_object
.type tls_beta,@tls_object
.weak tls_missing
.type tls_missing,@tls_object
ASM
cat >"$work/data.s" <<'ASM'
.data
.p2align 4
.globl tcb_stub
.type tcb_stub,@object
tcb_stub:
    .zero 16
    .quad 20
    .zero 4088
    .quad 22
.size tcb_stub,4120
.section .tdata,"awT",@progbits
.p2align 4
.globl tls_alpha
.type tls_alpha,@tls_object
tls_alpha:
    .quad 20
.size tls_alpha,8
    .zero 4088
.globl tls_beta
.type tls_beta,@tls_object
tls_beta:
    .quad 22
.size tls_beta,8
ASM
"$MINIAS" --target=aarch64 -march=armv8-a -mabi=lp64 \
    -o "$work/entry-original.o" "$work/entry.s"
"$MINIAS" --target=aarch64 -march=armv8-a -mabi=lp64 \
    -o "$work/data.o" "$work/data.s"
# MiniAS recognizes :got: but not yet :gottprel:.  Mutate only ELF r_type
# from ordinary GOT relocations to their psABI TLS IE equivalents.
python3 - "$work/entry-original.o" "$work/entry.o" <<'PY'
from pathlib import Path
import struct
import sys
raw = bytearray(Path(sys.argv[1]).read_bytes())
shoff = struct.unpack_from('<Q', raw, 40)[0]
shnum = struct.unpack_from('<H', raw, 60)[0]
counts = {311: 0, 312: 0}
le_relocations = [(547, 4), (549, 4), (551, 0), (559, 4), (551, 0)]
le_count = 0
ordinary_add_count = 0
for i in range(shnum):
    sh = struct.unpack_from('<IIQQQQIIQQ', raw, shoff + i * 64)
    if sh[1] != 4:
        continue
    for off in range(sh[4], sh[4] + sh[5], 24):
        info = struct.unpack_from('<Q', raw, off + 8)[0]
        typ = info & 0xffffffff
        if typ in counts:
            counts[typ] += 1
            struct.pack_into('<Q', raw, off + 8,
                             (info & ~0xffffffff) | {311: 541, 312: 542}[typ])
        elif typ == 277:
            ordinary_add_count += 1
            if ordinary_add_count == 1:
                continue # The real tcb_stub address calculation.
            newtype, delta = le_relocations[le_count]
            le_count += 1
            struct.pack_into('<Q', raw, off + 8,
                             (info & ~0xffffffff) | newtype)
            value = struct.unpack_from('<Q', raw, off)[0]
            struct.pack_into('<Q', raw, off, value + delta)
if counts != {311: 3, 312: 3}:
    raise SystemExit(f'MINILD_AARCH64_STATIC_TLS=FAIL pairs={counts}')
if ordinary_add_count != 6 or le_count != 5:
    raise SystemExit(f'MINILD_AARCH64_STATIC_TLS=FAIL LE_count={le_count}')
Path(sys.argv[2]).write_bytes(raw)
PY
"$MINILD" -m aarch64linux -static -e _start \
    -o "$work/direct.elf" "$work/entry.o" "$work/data.o"
"$MINILD" -m aarch64linux -r -o "$work/partial.o" \
    "$work/entry.o" "$work/data.o"
"$MINILD" -m aarch64linux -static -e _start \
    -o "$work/roundtrip.elf" "$work/partial.o"
python3 - "$work/direct.elf" "$work/roundtrip.elf" <<'PY'
from pathlib import Path
import struct
import sys
def check(path):
    raw = Path(path).read_bytes()
    assert raw[:6] == b'\x7fELF\x02\x01'
    assert struct.unpack_from('<HH', raw, 16) == (2, 183)
    phoff, shoff = struct.unpack_from('<QQ', raw, 32)
    phentsize, phnum = struct.unpack_from('<HH', raw, 54)
    shentsize, shnum, shstrndx = struct.unpack_from('<HHH', raw, 58)
    assert phentsize == 56 and shentsize == 64
    phs = [struct.unpack_from('<IIQQQQQQ', raw, phoff + i * 56)
           for i in range(phnum)]
    tls = [p for p in phs if p[0] == 7]
    assert len(tls) == 1, 'missing PT_TLS'
    tls_addr, align = tls[0][3], tls[0][7]
    assert align >= 16 and tls_addr % align == 0
    pad = (tls_addr - 16) % align
    shs = [struct.unpack_from('<IIQQQQIIQQ', raw, shoff + i * 64)
           for i in range(shnum)]
    sp = shs[shstrndx]
    strings = raw[sp[4]:sp[4] + sp[5]]
    def name(off):
        return strings[off:strings.index(0, off)].decode()
    secs = {name(s[0]): (s[3], raw[s[4]:s[4] + s[5]]) for s in shs}
    text_addr, code = secs['.text']
    got_addr, got = secs['.got']
    assert len(got) == 24
    values = []
    for off in (0, 8, 16):
        adrp, ldr = struct.unpack_from('<II', code, off)
        assert adrp & 0x9f000000 == 0x90000000
        assert ldr & 0xffc00000 == 0xf9400000
        imm = ((adrp >> 29) & 3) | (((adrp >> 5) & 0x7ffff) << 2)
        if imm & (1 << 20): imm -= 1 << 21
        slot = ((text_addr + off) & ~0xfff) + imm * 4096
        slot += (((ldr >> 10) & 0xfff) << 3)
        assert got_addr <= slot < got_addr + len(got)
        values.append(struct.unpack_from('<Q', got, slot - got_addr)[0])
    assert values == [16 + pad, 4112 + pad, 0], values
    # These instructions were relocated by LE types 547,549,551,559,551.
    movz, add_hi, add_lo, ld64, add_weak = [
        struct.unpack_from('<I', code, off)[0]
        for off in (18*4, 20*4, 21*4, 23*4, 24*4)]
    assert movz & 0xff800000 == 0xd2800000
    assert (movz >> 5) & 0xffff == 16 + pad
    assert add_hi & 0x7f400000 == 0x11400000
    assert (add_hi >> 10) & 0xfff == (4112 + pad) >> 12
    assert add_lo & 0x7f400000 == 0x11000000
    assert (add_lo >> 10) & 0xfff == (4112 + pad) & 0xfff
    assert ld64 & 0xffc00000 == 0xf9400000
    assert (ld64 >> 10) & 0xfff == ((16 + pad) & 0xfff) >> 3
    assert add_weak & 0x7f400000 == 0x11000000
    assert (add_weak >> 10) & 0xfff == 0
    print(f'MINILD_AARCH64_TLS=PASS file={Path(path).name} '
          'IE=3 LE=5 PT_TLS=1 TCB=16 weak_undef=0')
for p in sys.argv[1:]: check(p)
PY
# A TLSIE GOT proxy cannot carry an addend and its ADRP must have the
# correct machine instruction encoding; fail closed on either mutation.
python3 - "$work/entry.o" "$work/bad-addend.o" "$work/bad-opcode.o" <<'PY'
from pathlib import Path
import struct
import sys
raw = bytearray(Path(sys.argv[1]).read_bytes())
shoff = struct.unpack_from('<Q', raw, 40)[0]
shnum, shstrndx = struct.unpack_from('<HH', raw, 60)
shs = [struct.unpack_from('<IIQQQQIIQQ', raw, shoff + i * 64)
       for i in range(shnum)]
rel = next(s for s in shs if s[1] == 4 and s[5] >= 24)
bad = bytearray(raw)
struct.pack_into('<q', bad, rel[4] + 16, 1)
Path(sys.argv[2]).write_bytes(bad)
names = shs[shstrndx]
pool = raw[names[4]:names[4] + names[5]]
def name(off):
    return pool[off:pool.index(0, off)].decode()
text = next(s for s in shs if name(s[0]) == '.text')
bad = bytearray(raw)
struct.pack_into('<I', bad, text[4], 0xd503201f) # NOP, not ADRP
Path(sys.argv[3]).write_bytes(bad)
PY
for mode in addend opcode; do
    if "$MINILD" -m aarch64linux -static -e _start \
        -o "$work/bad-$mode.elf" "$work/bad-$mode.o" "$work/data.o" \
        >"$work/bad-$mode.stdout" 2>"$work/bad-$mode.stderr"; then
        echo "MINILD_AARCH64_STATIC_TLS=FAIL invalid_$mode_accepted" >&2
        exit 1
    fi
done
grep -q 'invalid-aarch64-static-got-relocation:541' "$work/bad-addend.stderr"
grep -q 'nonriscv-relocation-overflow-or-encoding:183:541' "$work/bad-opcode.stderr"
echo 'MINILD_AARCH64_TLS=PASS fail_closed=2'
if [[ ${MINILD_REQUIRE_GNU:-0} == 1 ]] && \
   ! command -v aarch64-linux-gnu-ld >/dev/null 2>&1; then
    echo 'MINILD_AARCH64_STATIC_TLS=FAIL required_gnu_oracle_missing' >&2
    exit 1
fi
if [[ ${MINILD_REQUIRE_QEMU:-0} == 1 ]] && \
   ! command -v qemu-aarch64 >/dev/null 2>&1; then
    echo 'MINILD_AARCH64_STATIC_TLS=FAIL required_qemu_missing' >&2
    exit 1
fi
if command -v aarch64-linux-gnu-ld >/dev/null 2>&1; then
    aarch64-linux-gnu-ld -m aarch64linux -static -e _start \
        -o "$work/gnu.elf" "$work/entry.o" "$work/data.o"
    if command -v qemu-aarch64 >/dev/null 2>&1; then
        for binary in "$work/direct.elf" "$work/roundtrip.elf" "$work/gnu.elf"; do
            result=0
            timeout 5s qemu-aarch64 "$binary" || result=$?
            if [[ $result != 42 ]]; then
                echo "MINILD_AARCH64_STATIC_TLS=FAIL qemu_exit=$result file=$binary" >&2
                exit 1
            fi
        done
        echo 'MINILD_AARCH64_STATIC_TLS=PASS qemu=3 gnu_bfd=1 expected_exit=42'
    else
        echo 'MINILD_AARCH64_STATIC_TLS=SKIP qemu_runtime_oracle_missing'
    fi
else
    echo 'MINILD_AARCH64_STATIC_TLS=SKIP cross_gnu_qemu_parity_missing'
fi
