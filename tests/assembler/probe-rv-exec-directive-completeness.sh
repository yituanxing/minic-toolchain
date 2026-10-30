#!/usr/bin/env bash
# Base-I raw-instruction safety must not impose a non-GNU data-directive
# restriction on ordinary extended -march profiles. The Linux vDSO emits
# `.4byte 0x0100000f` in its executable GCC assembly.
set -Eeuo pipefail
: "${MINIAS:?exact-head MiniAS binary required}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cases=(
    '.byte 0x13'
    '.half 0x0013'
    '.short 0x0013'
    '.hword 0x0013'
    '.value 0x0013'
    '.2byte 0x0013'
    '.word 0x00000013'
    '.long 0x00000013'
    '.4byte 0x00000013'
    '.dword 0x00000013'
    '.quad 0x00000013'
    '.xword 0x00000013'
    '.8byte 0x00000013'
    '.zero 4'
    '.space 4'
    '.skip 4'
    '.fill 1,4,0x13'
    '.uleb128 19'
    '.sleb128 19'
    '.asciz "A"'
    '.string "A"'
    '.ascii "A"'
)

count=0
for directive in "${cases[@]}"; do
    op=${directive%% *}
    printf '.text\n%s\n' "$directive" >"$work/input.s"
    for arch in rv64i rv32i; do
        if "$MINIAS" "-march=$arch" -o "$work/out.o" "$work/input.s" \
                >"$work/standard.out" 2>"$work/standard.err"; then
            printf 'RISC-V ISA raw-data escape succeeded: %s / %s\n' \
                "$arch" "$directive" >&2
            exit 1
        fi
        if ! grep -Fq "unsupported-isa-escape:$op-in-exec" "$work/standard.err"; then
            printf 'Wrong RISC-V ISA escape rejection for %s / %s\n' \
                "$arch" "$directive" >&2
            sed -n '1,12p' "$work/standard.err" >&2
            exit 1
        fi
        count=$((count + 1))
    done
    for arch in rv64im rv32im; do
        "$MINIAS" "-march=$arch" -o "$work/extended.o" "$work/input.s"
        count=$((count + 1))
    done
    printf '.data\n%s\n' "$directive" >"$work/data.s"
    "$MINIAS" -march=rv64im -o "$work/data.o" "$work/data.s"
done

# GNU-style .org remains a separate executable-fragment escape;
# numeric LEB encodings are never implicitly trusted as instructions.
printf '.text\n.org 4\n' >"$work/input.s"
if "$MINIAS" -march=rv64i -o "$work/out.o" "$work/input.s" \
        >"$work/standard.out" 2>"$work/standard.err"; then
    echo 'RISC-V selected ISA accepted executable .org escape' >&2
    exit 1
fi
grep -Fq 'unsupported-isa-escape:.org' "$work/standard.err"

# A real GCC Linux vDSO instruction word is raw source data, not an opcode
# chosen by MiniAS. GNU's output remains the machine-byte authority.
printf '.text\n.4byte 0x0100000f\n' >"$work/vdso.s"
"$MINIAS" -march=rv64imac_zicsr_zifencei -mabi=lp64 \
    -o "$work/vdso-minias.o" "$work/vdso.s"
if command -v riscv64-linux-gnu-as >/dev/null && \
   command -v riscv64-linux-gnu-objcopy >/dev/null; then
    riscv64-linux-gnu-as -march=rv64imac_zicsr_zifencei -mabi=lp64 \
        -o "$work/vdso-gnu.o" "$work/vdso.s"
    riscv64-linux-gnu-objcopy -O binary -j .text \
        "$work/vdso-gnu.o" "$work/vdso-gnu.bin"
    riscv64-linux-gnu-objcopy -O binary -j .text \
        "$work/vdso-minias.o" "$work/vdso-minias.bin"
    cmp "$work/vdso-gnu.bin" "$work/vdso-minias.bin"
fi

# Supervisor instructions are selected by execution privilege, not by a
# fictitious S bit in GCC's -march=rv64imac profile. Real Linux head.S uses
# sfence.vma before writing satp; strict RV64I still rejects the same word.
printf '.text\nsfence.vma\nsfence.vma a0, a1\nsret\nwfi\n' >"$work/priv.s"
"$MINIAS" -march=rv64imac_zicsr_zifencei -mabi=lp64 \
    -o "$work/priv-minias.o" "$work/priv.s"
if command -v riscv64-linux-gnu-as >/dev/null && \
   command -v riscv64-linux-gnu-objcopy >/dev/null; then
    riscv64-linux-gnu-as -march=rv64imac_zicsr_zifencei -mabi=lp64 \
        -o "$work/priv-gnu.o" "$work/priv.s"
    riscv64-linux-gnu-objcopy -O binary -j .text \
        "$work/priv-gnu.o" "$work/priv-gnu.bin"
    riscv64-linux-gnu-objcopy -O binary -j .text \
        "$work/priv-minias.o" "$work/priv-minias.bin"
    cmp "$work/priv-gnu.bin" "$work/priv-minias.bin"
fi

echo "MINIAS_RV_EXEC_RAW_ESCAPE=PASS arch_cases=$count data_valid=${#cases[@]} vdso=PASS"
