#!/usr/bin/env bash
# RV32/RV64 LI lowering must have GNU GAS's exact uncompressed instruction
# sequence.  The same retained MCInst plan drives fragment size and emission;
# the local branch, function .size and CFI range catch layout drift.
set -Eeuo pipefail
: "${MINIAS:?exact MiniAS binary required}"

for program in riscv64-linux-gnu-as riscv64-linux-gnu-objcopy readelf cmp; do
    if ! command -v "$program" >/dev/null 2>&1; then
        echo "MINIAS_RV_LI_GNU=SKIP missing=$program"
        exit 0
    fi
done
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cat >"$work/rv64.s" <<'ASM'
.option norelax
.option norvc
.text
.globl rv_li_probe
.type rv_li_probe,@function
rv_li_probe:
  .cfi_startproc
  addi sp,sp,-16
  .cfi_def_cfa_offset 16
  li t0,16711680
  li t1,-9223372036854775801
  li t2,4294967295
  li a0,0
  li a1,2047
  li a2,2048
  li a3,-2048
  li a4,-2049
  li a5,4096
  li a6,-4096
  li a7,65280
  li s0,2147483647
  li s1,-2147483648
  li s2,2147483648
  li s3,-2147483649
  li s4,4278190080
  li s5,1095216660480
  li s6,280375465082880
  li s7,71776119061217280
  li s8,-72057594037927936
  li s9,0x7fffffffffffffff
  li s10,0x8000000000000000
  li s11,0x8000000000000007
  li t3,0xffffffffffffffff
  li t4,0x123456789abcdef0
  li t5,0xfedcba9876543210
  li t6,0x1000000000000001
  beq t0,t1,.Lli_done
  li t0,-9223372036854775808
.Lli_done:
  addi sp,sp,16
  .cfi_def_cfa_offset 0
  ret
  .cfi_endproc
.size rv_li_probe,.-rv_li_probe
ASM

cat >"$work/rv32.s" <<'ASM'
.option norelax
.option norvc
.text
.globl rv_li_probe
.type rv_li_probe,@function
rv_li_probe:
  li a0,0
  li a1,2047
  li a2,2048
  li a3,-2048
  li a4,-2049
  li a5,4096
  li a6,-4096
  li a7,0x7fffffff
  li t0,0x80000000
  li t1,0xffffffff
  li t2,-2147483648
  li t3,0xfffff000
  li t4,0x7ffff000
  ret
.size rv_li_probe,.-rv_li_probe
ASM

# Reproducible wide-bit-pattern stress, with no runtime randomness. Bash's
# signed 64-bit arithmetic wraps modulo 2^64 and %016x preserves those bits.
seed=0x243f6a8885a308d3
{
    printf '.option norelax\n.option norvc\n.text\n.globl rv_li_probe\n.type rv_li_probe,@function\nrv_li_probe:\n  .cfi_startproc\n'
    for ((i=0; i<64; i++)); do
        seed=$((seed * 6364136223846793005 + 1442695040888963407))
        printf '  li a0,0x%016x\n' "$seed"
    done
    printf '  ret\n  .cfi_endproc\n.size rv_li_probe,.-rv_li_probe\n'
} >"$work/rv64-wide.s"

cases=0
check_profile() {
    local name=$1 march=$2 mabi=$3 source=$4 producer section
    riscv64-linux-gnu-as "-march=$march" "-mabi=$mabi" -mno-relax \
        -o "$work/$name.gnu.o" "$work/$source.s"
    "$MINIAS" "-march=$march" "-mabi=$mabi" \
        -o "$work/$name.mini.o" "$work/$source.s"
    for producer in gnu mini; do
        riscv64-linux-gnu-objcopy -O binary -j .text \
            "$work/$name.$producer.o" "$work/$name.$producer.text"
    done
    if ! cmp -s "$work/$name.gnu.text" "$work/$name.mini.text"; then
        echo "MINIAS_RV_LI_GNU=FAIL profile=$march bytes" >&2
        cmp "$work/$name.gnu.text" "$work/$name.mini.text" >&2 || true
        exit 1
    fi
    # Both symbol size and FDE range derive from the measured fragment size.
    for producer in gnu mini; do
        readelf -Ws "$work/$name.$producer.o" | \
            awk '$NF == "rv_li_probe" {print $3}' \
            >"$work/$name.$producer.size"
        readelf -W -S "$work/$name.$producer.o" | \
            awk '$3 == ".text" {print $7}' \
            >"$work/$name.$producer.textsize"
        readelf -Wr "$work/$name.$producer.o" | \
            awk '/R_RISCV_/ {n++} END {print n+0}' \
            >"$work/$name.$producer.reloc_count"
    done
    for section in size textsize reloc_count; do
        cmp "$work/$name.gnu.$section" "$work/$name.mini.$section"
    done
    if [[ "$source" == rv64* ]]; then
        for producer in gnu mini; do
            readelf -wf "$work/$name.$producer.o" | \
                grep -q 'FDE .*pc=' || {
                    echo "MINIAS_RV_LI_GNU=FAIL profile=$march missing-CFI-$producer" >&2
                    exit 1
                }
        done
    fi
    cases=$((cases + 1))
}

# RVC is deliberately absent: its compressed-instruction selection has a
# separate GNU oracle, while this checks exact 32-bit Base-I LI lowering.
check_profile rv64i rv64i lp64 rv64
check_profile rv64ima rv64ima_zicsr_zifencei_zihintpause lp64 rv64
check_profile rv64wide rv64i lp64 rv64-wide
check_profile rv32i rv32i ilp32 rv32
check_profile rv32ima rv32ima_zicsr_zifencei ilp32 rv32

printf '.text\nli a0,0x100000000\n' >"$work/rv32-overflow.s"
if "$MINIAS" -march=rv32i -mabi=ilp32 \
        -o "$work/rv32-overflow.o" "$work/rv32-overflow.s" \
        >"$work/rv32-overflow.out" 2>"$work/rv32-overflow.err"; then
    echo 'MINIAS_RV_LI_GNU=FAIL RV32 overflow accepted' >&2
    exit 1
fi
test ! -e "$work/rv32-overflow.o"

echo "MINIAS_RV_LI_GNU=PASS GNU=enabled profiles=$cases rv64_values=28 random_wide=64 rv32_values=13 cfi=1 overflow_reject=1"
