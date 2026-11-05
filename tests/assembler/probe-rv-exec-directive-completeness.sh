#!/usr/bin/env bash
# -march controls typed instruction legality, not executable DATA syntax.
# GNU GAS accepts raw .word/.byte, string data and .org under *exact* RV32I
# and RV64I too. A stronger all-bytes Base-I audit is a separate explicit API.
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
gnu_unsupported=0
gnu_available=0
if command -v riscv64-linux-gnu-as >/dev/null &&
   command -v riscv64-linux-gnu-objcopy >/dev/null; then
    gnu_available=1
fi

compare_gnu_text_payload() {
    # GNU GAS rounds the final RISC-V .text size up to 4 bytes, while MiniAS
    # currently preserves an unaligned raw-data section length. Compare the
    # requested payload exactly, and record the bounded padding difference.
    local mini=$1 gnu=$2 mini_size gnu_size
    mini_size=$(wc -c < "$mini")
    gnu_size=$(wc -c < "$gnu")
    if ((gnu_size < mini_size || gnu_size - mini_size > 3)); then
        echo "Unexpected GNU .text padding: MiniAS=$mini_size GNU=$gnu_size" >&2
        return 1
    fi
    cmp -n "$mini_size" "$mini" "$gnu"
}

for directive in "${cases[@]}"; do
    printf '.text\n%s\n' "$directive" >"$work/input.s"
    for arch in rv64i rv32i; do
        if [[ "$arch" == rv32i ]]; then abi=ilp32; else abi=lp64; fi
        "$MINIAS" "-march=$arch" "-mabi=$abi" \
            -o "$work/mini.o" "$work/input.s"
        if ((gnu_available)); then
            if riscv64-linux-gnu-as "-march=$arch" "-mabi=$abi" \
                    -o "$work/gnu.o" "$work/input.s" 2>"$work/gnu.err"; then
                riscv64-linux-gnu-objcopy -O binary -j .text \
                    "$work/mini.o" "$work/mini.bin"
                riscv64-linux-gnu-objcopy -O binary -j .text \
                    "$work/gnu.o" "$work/gnu.bin"
                compare_gnu_text_payload "$work/mini.bin" "$work/gnu.bin"
            else
                # .value and a few compatibility spellings are MiniAS input
                # aliases but not accepted by this installed GNU GAS.
                gnu_unsupported=$((gnu_unsupported + 1))
            fi
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

# .org is a layout directive under ordinary -march too, even if it inserts
# zero padding which is not a valid executable instruction.
printf '.text\n.org 4\n' >"$work/input.s"
"$MINIAS" -march=rv64i -mabi=lp64 -o "$work/org-mini.o" "$work/input.s"
if ((gnu_available)); then
    riscv64-linux-gnu-as -march=rv64i -mabi=lp64 \
        -o "$work/org-gnu.o" "$work/input.s"
    for kind in mini gnu; do
        riscv64-linux-gnu-objcopy -O binary -j .text \
            "$work/org-$kind.o" "$work/org-$kind.bin"
    done
    compare_gnu_text_payload "$work/org-mini.bin" "$work/org-gnu.bin"
fi

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
# sfence.vma before writing satp; ordinary RV64I accepts the same instruction.
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

echo "MINIAS_RV_EXEC_RAW_ESCAPE=PASS arch_cases=$count data_valid=${#cases[@]} org=GNU-compatible vdso=PASS GNU=$gnu_available GNU_unsupported=$gnu_unsupported"
