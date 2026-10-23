#!/usr/bin/env bash
# Real GNU-Kbuild route, exactly one C-owned source:
# GNU-preprocessed .i -> strict-I MiniC .s -> GNU assembler .o.
# The kernel's explicit inline M/A code is preserved; runtime arithmetic in
# compiler output must lower to a software library call.
set -Eeuo pipefail
if (( $# != 1 )); then
  echo "usage: $0 /absolute/path/to/minic" >&2
  exit 64
fi
minic="$1"
command -v riscv64-linux-gnu-gcc >/dev/null
command -v riscv64-linux-gnu-objdump >/dev/null
command -v riscv64-linux-gnu-nm >/dev/null
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat > "$work/linux.i" <<'SRC'
/* Same original Kbuild contract: kernel-owned inline ISA, compiler-owned C. */
unsigned long kernel_c_math(unsigned long a, unsigned long b) {
    unsigned long product = a * b;
    __asm__ __volatile__("mul t0, t1, t2");
    __asm__ __volatile__("amoadd.w t0, t1, (t2)");
    __asm__ __volatile__("csrr t0, sstatus");
    return product + 1;
}
SRC
# Explicit -march=rv64gc belongs only to GNU AS and user-owned inline ASM.
# The wrapper must *not* pass it to MiniC in strict-I mode.
MINIC="$minic" REAL_CC=riscv64-linux-gnu-gcc \
MINIC_KBUILD_RV64I_ORIGIN_AUDIT=1 MINIC_KEEP_INTERMEDIATES=1 \
MINIC_KBUILD_TRACE="$work/trace.log" \
  bash tests/external/linux/stage2_kbuild_cc.sh \
    -c -march=rv64gc -mabi=lp64 -o "$work/linux.o" "$work/linux.i"
test -s "$work/linux.o"
test -s "$work/linux.minic-stage2.s"
grep -q '^minic rv64i_origin_audit=1 ' "$work/trace.log"
python3 tools/ci/audit-rv64i-asm-origin.py \
  --root "$work" --json-out "$work/audit.json" \
  --expected-files 1 --require-markers --fail-on-compiler-non-i \
  --opcode-tsv-out "$work/opcodes.tsv"
python3 - "$work/audit.json" <<'PY'
import json,sys
s=json.load(open(sys.argv[1]))['summary']
assert s['compiler_non_i'] == 0, s
assert s['inline_regions'] == 3, s
for kind in ('M','A','Zicsr'):
    assert s['inline'].get(kind,0)>=1,(kind,s)
print("RV64I_KBUILD_ORIGIN=PASS inline_M_A_Zicsr=true compiler_non_I=0")
PY
# One ELF64 RISC-V object, plus a real software arithmetic libcall for
# compiler-generated multiplication (not an M opcode hidden in the object).
riscv64-linux-gnu-readelf -h "$work/linux.o" |
    grep -q 'Machine:.*RISC-V'
riscv64-linux-gnu-nm -u "$work/linux.o" | grep -Eq '__muldi3|__multi3'
riscv64-linux-gnu-objdump -dr "$work/linux.o" >"$work/linux.objdump"
grep -q 'mul[[:space:]]' "$work/linux.objdump"
echo 'RV64I_KBUILD_ELF=PASS real_GNU_AS=true software_multiplication=true inline_M_preserved=true'
