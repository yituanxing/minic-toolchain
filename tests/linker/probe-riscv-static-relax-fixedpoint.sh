#!/usr/bin/env bash
# Exact MiniLD RV32/RV64 post-layout relaxation contract: one in-range
# cross-input-section CALL becomes JAL, an out-of-range CALL stays AUIPC/JALR,
# and an ALIGN after both calls is recomputed at the final address.
set -Eeuo pipefail
: "${MINIAS:?set MINIAS to minic-as}"
: "${MINILD:?set MINILD to minic-ld}"

work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

cat >"$work/relax.s" <<'ASM'
.option relax
.section .text.start,"ax",@progbits
.globl _start
.type _start,@function
_start:
    call near_target
    call far_target
    .p2align 4
.globl aligned_label
aligned_label:
    ret
.size _start,.-_start

.section .text.near,"ax",@progbits
.globl near_target
near_target:
    ret

.section .text.far,"ax",@progbits
.globl far_target
far_target:
    ret
ASM

cat >"$work/layout.ld" <<'SCRIPT'
ENTRY(_start)
SECTIONS {
  . = 0x10000;
  .text : { *(.text.start) *(.text.near) }
  . = 0x310000;
  .fartext : { *(.text.far) }
}
SCRIPT

for bits in 32 64; do
    if [[ $bits == 32 ]]; then
        abi=ilp32
    else
        abi=lp64
    fi
    "$MINIAS" --target=riscv "-march=rv${bits}i2p1" "-mabi=$abi" \
        -o "$work/$bits.o" "$work/relax.s"
    "$MINILD" "-melf${bits}lriscv" -static -e _start \
        -T "$work/layout.ld" -o "$work/$bits.elf" "$work/$bits.o"
done

# CFI advance_loc operands are emitted as R_RISCV_SET6/SUB6 arithmetic pairs.
# The code shrink must propagate to *both* frame sections, not just .text
# and the .symtab address/size fields.
cat >"$work/cfi.s" <<'ASM'
.cfi_sections .eh_frame, .debug_frame
.option relax
.text
.globl cfi_start
.type cfi_start,@function
cfi_start:
    .cfi_startproc simple
    .cfi_def_cfa 2,0
    call cfi_target
.globl cfi_after_call
cfi_after_call:
    .cfi_def_cfa_offset 16
    addi x0,x0,0
.globl cfi_target
cfi_target:
    .cfi_def_cfa_offset 24
    ret
    .cfi_endproc
.size cfi_start,.-cfi_start
ASM
for bits in 32 64; do
    if [[ $bits == 32 ]]; then abi=ilp32; else abi=lp64; fi
    "$MINIAS" --target=riscv "-march=rv${bits}i2p1" "-mabi=$abi" \
        -o "$work/$bits-cfi.o" "$work/cfi.s"
    "$MINILD" "-melf${bits}lriscv" -static -e cfi_start \
        -o "$work/$bits-cfi.elf" "$work/$bits-cfi.o"
done

# Fixed-point witness: CALL #1 initially misses JAL's upper bound by 2B.
# CALL #2 shortens first and moves the distant target back into range.
# A single post-layout pass would leave CALL #1 as an 8-byte pair.
cat >"$work/second-pass.s" <<'ASM'
.option relax
.text
.globl iteration_start
iteration_start:
    call iteration_distant
    call iteration_near
.globl iteration_near
iteration_near:
    ret
    .space 1048556
.globl iteration_distant
iteration_distant:
    ret
ASM
for bits in 32 64; do
    if [[ $bits == 32 ]]; then abi=ilp32; else abi=lp64; fi
    "$MINIAS" --target=riscv "-march=rv${bits}i2p1" "-mabi=$abi" \
        -o "$work/$bits-iteration.o" "$work/second-pass.s"
    "$MINILD" "-melf${bits}lriscv" -static -e iteration_start \
        -o "$work/$bits-iteration.elf" "$work/$bits-iteration.o"
done

# GNU BFD conservatively bounds a CALL to a same-output-section target by
# that output section's sh_addralign.  A +1MiB forward call therefore stays
# AUIPC/JALR at this boundary: BFD must not assume its own future deletion
# makes an otherwise out-of-range relocation fit.
cat >"$work/self-shrink.s" <<'ASM'
.option relax
.text
.globl self_shrink_start
self_shrink_start:
    call self_shrink_target
    .space 1048568
.globl self_shrink_target
self_shrink_target:
    ret
ASM
for bits in 32 64; do
    if [[ $bits == 32 ]]; then abi=ilp32; else abi=lp64; fi
    "$MINIAS" --target=riscv "-march=rv${bits}i2p1" "-mabi=$abi" \
        -o "$work/$bits-self.o" "$work/self-shrink.s"
    "$MINILD" "-melf${bits}lriscv" -static -e self_shrink_start \
        -o "$work/$bits-self.elf" "$work/$bits-self.o"
done

cat >"$work/rvc-boundary.s" <<'ASM'
.option rvc
.option relax
.text
.globl rvc_boundary_start
rvc_boundary_start:
    call rvc_boundary_target
    .space 2040
.globl rvc_boundary_target
rvc_boundary_target:
    ret
ASM
"$MINIAS" --target=riscv -march=rv32ic -mabi=ilp32 \
    -o "$work/32-rvc-boundary.o" "$work/rvc-boundary.s"
"$MINILD" -melf32lriscv -static -e rvc_boundary_start \
    -o "$work/32-rvc-boundary.elf" "$work/32-rvc-boundary.o"

# RVC is an ELF-unit capability, distinct from the active XLEN.  A near CALL
# may become C.JAL on RV32C, but must stay JAL on RV64C (opcode 001 = ADDIW).
cat >"$work/compressed.s" <<'ASM'
.option rvc
.option relax
.text
.globl compressed_start
compressed_start:
    call compressed_target
    .2byte 0x0001
.globl compressed_target
compressed_target:
    ret
ASM
for bits in 32 64; do
    if [[ $bits == 32 ]]; then abi=ilp32; else abi=lp64; fi
    "$MINIAS" --target=riscv "-march=rv${bits}ic" "-mabi=$abi" \
        -o "$work/$bits-compressed.o" "$work/compressed.s"
    "$MINILD" "-melf${bits}lriscv" -static -e compressed_start \
        -o "$work/$bits-compressed.elf" "$work/$bits-compressed.o"
done

python3 - "$work" <<'PY'
import pathlib
import re
import struct
import subprocess
import sys

root = pathlib.Path(sys.argv[1])

def section(path, name):
    out = subprocess.check_output(['readelf', '-SW', str(path)], text=True)
    for line in out.splitlines():
        match = re.match(
            r'^\s*\[\s*\d+\]\s+(\S+)\s+\S+\s+([0-9a-f]+)\s+'
            r'([0-9a-f]+)\s+([0-9a-f]+)', line, re.I)
        if match and match.group(1) == name:
            return tuple(int(v, 16) for v in match.groups()[1:])
    raise RuntimeError(f'missing section {name}')

def symbol(path, name):
    out = subprocess.check_output(['readelf', '-Ws', str(path)], text=True)
    for line in out.splitlines():
        cols = line.split()
        if len(cols) >= 8 and cols[0].endswith(':') and cols[-1] == name:
            return int(cols[1], 16), int(cols[2])
    raise RuntimeError(f'missing symbol {name}')

for bits in (32, 64):
    path = root / f'{bits}.elf'
    start, start_size = symbol(path, '_start')
    aligned, _ = symbol(path, 'aligned_label')
    near, _ = symbol(path, 'near_target')
    far, _ = symbol(path, 'far_target')
    text_vma, text_file, text_size = section(path, '.text')
    far_vma, _, _ = section(path, '.fartext')
    blob = path.read_bytes()[text_file:text_file + text_size]
    call = struct.unpack_from('<I', blob, start - text_vma)[0]
    unrelaxed_call = struct.unpack_from('<I', blob, start - text_vma + 4)[0]
    unrelaxed_jalr = struct.unpack_from('<I', blob, start - text_vma + 8)[0]

    assert start == 0x10000, (bits, start)
    assert aligned == 0x10010, (bits, aligned)
    assert near == 0x10014, (bits, near)
    assert far == far_vma == 0x310000, (bits, far)
    assert start_size == 20, (bits, start_size)
    assert call & 0x7f == 0x6f and (call >> 7) & 31 == 1, (bits, hex(call))
    assert unrelaxed_call & 0x7f == 0x17, (bits, hex(unrelaxed_call))
    assert unrelaxed_jalr & 0x707f == 0x67, (bits, hex(unrelaxed_jalr))
    print(f'MINILD_RV_RELAX_CASE=PASS rv={bits} text_size={text_size} '
          f'near_call=JAL far_call=AUIPC_JALR '
          f'align=0x{aligned:x} st_size={start_size}')

for bits in (32, 64):
    path = root / f'{bits}-cfi.elf'
    begin, begin_size = symbol(path, 'cfi_start')
    after, _ = symbol(path, 'cfi_after_call')
    target, _ = symbol(path, 'cfi_target')
    assert after - begin == 4, (bits, 'CALL not relaxed', begin, after)
    assert target - after == 4 and begin_size == 12, (
        bits, 'st_size wrong', begin_size, after, target)
    dump = subprocess.check_output(
        ['readelf', '--debug-dump=frames', str(path)], text=True)
    block = {}
    current = None
    for line in dump.splitlines():
        frame = re.search(
            r'Contents of the (\.(?:eh|debug)_frame) section:', line)
        if frame:
            current = frame.group(1)
            block[current] = []
        elif current and 'DW_CFA_advance_loc:' in line:
            pc = re.search(r'\bto\s+(?:0x)?([0-9a-fA-F]+)\b', line)
            if pc:
                block[current].append(int(pc.group(1), 16))
    for frame in ('.eh_frame', '.debug_frame'):
        assert block.get(frame, [])[:2] == [after, target], (
            bits, frame, block, dump)
    print(f'MINILD_RV_RELAX_CFI=PASS rv={bits} '
          'frames=eh,debug FDE_rows=exact st_size=12')

for bits in (32, 64):
    path = root / f'{bits}-iteration.elf'
    start, _ = symbol(path, 'iteration_start')
    near, _ = symbol(path, 'iteration_near')
    distant, _ = symbol(path, 'iteration_distant')
    text_vma, text_file, text_size = section(path, '.text')
    data = path.read_bytes()[text_file:text_file + text_size]
    first = struct.unpack_from('<I', data, start - text_vma)[0]
    second = struct.unpack_from('<I', data, start - text_vma + 4)[0]
    assert (first & 0x7f, second & 0x7f) == (0x6f, 0x6f), (
        bits, hex(first), hex(second))
    assert near - start == 8, (bits, near - start)
    assert distant - start == 1048568, (bits, distant - start)
    print(f'MINILD_RV_RELAX_ITERATION=PASS rv={bits} '
          f'call1=JAL call2=JAL distant_offset={distant-start}')

for bits in (32, 64):
    path = root / f'{bits}-self.elf'
    start, _ = symbol(path, 'self_shrink_start')
    target, _ = symbol(path, 'self_shrink_target')
    text_vma, text_file, text_size = section(path, '.text')
    first = struct.unpack_from(
        '<I', path.read_bytes(), text_file + start-text_vma)[0]
    second = struct.unpack_from(
        '<I', path.read_bytes(), text_file + start-text_vma+4)[0]
    assert first & 0x7f == 0x17, (bits, hex(first))
    assert second & 0x707f == 0x67, (bits, hex(second))
    assert target-start == 1048576, (bits,target-start)
    print(f'MINILD_RV_RELAX_SELF=PASS rv={bits} '
          f'AUIPC_JALR_boundary={target-start}')

for bits, expected_delta in ((32, 4), (64, 6)):
    path = root / f'{bits}-compressed.elf'
    start, _ = symbol(path, 'compressed_start')
    target, _ = symbol(path, 'compressed_target')
    text_vma, text_file, text_size = section(path, '.text')
    data = path.read_bytes()[text_file:text_file + text_size]
    assert target - start == expected_delta, (
        bits, target-start, expected_delta)
    if bits == 32:
        first = struct.unpack_from('<H', data, start-text_vma)[0]
        assert first & 0xe003 == 0x2001, (bits, hex(first))
    else:
        first = struct.unpack_from('<I', data, start-text_vma)[0]
        assert first & 0x7f == 0x6f, (bits, hex(first))
    print(f'MINILD_RV_RELAX_RVC=PASS rv={bits} '
          f'call_width={2 if bits == 32 else 4} '
          f'target_offset={expected_delta}')

path = root / '32-rvc-boundary.elf'
start, _ = symbol(path, 'rvc_boundary_start')
target, _ = symbol(path, 'rvc_boundary_target')
text_vma, text_file, text_size = section(path, '.text')
first = struct.unpack_from('<H',path.read_bytes(),text_file+start-text_vma)[0]
assert first & 0xe003 == 0x2001,hex(first)
assert target-start == 2042,target-start
print('MINILD_RV_RELAX_RVC_BOUNDARY=PASS rv=32 '
      'C_JAL=2 target_offset=2042')

print('MINILD_RV_RELAX_FIXEDPOINT=PASS targets=RV32,RV64 '
      'cross_section_call=2 far_call=2 align=2 st_size=4 '
      'CFI_frames=4 multi_round_calls=2 self_boundary=2 '
      'compressed_calls=2 compressed_boundary=1')
PY
