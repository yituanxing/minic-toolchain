#!/usr/bin/env bash
set -Eeuo pipefail
mini=${1:?MiniC compiler}
test -x "$mini"
here=$(cd "$(dirname "$0")" && pwd)
src="$here/loongarch64_packed_asm_output.c"
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT

# A host GCC syntax oracle checks the source is genuine GNU C; native target
# GNU GAS/QEMU execution is separate and must not be claimed here.
cc -std=gnu11 -Wall -Wextra -Werror -fsyntax-only "$src"
cc -E -P -x c "$src" -o "$dir/source.i"
"$mini" --target=loongarch64-linux -S "$dir/source.i" -o "$dir/mini.s"
# Original LoongArch Linux uses LP64S.  Source-declared FPR transfers are
# still hardware instructions even though the compiler must never select
# FP arithmetic or assign FP argument registers under that C ABI.
"$mini" --target=loongarch64-linux -mabi=lp64s \
  -fshort-wchar -funsigned-char -fno-PIE \
  -S "$dir/source.i" -o "$dir/loong-lp64s.s"
grep -Fq 'movfr2gr.d $t0, $f0' "$dir/loong-lp64s.s"
! grep -Eq '^  (fadd|fsub|fmul|fdiv)\.' "$dir/loong-lp64s.s"

for fn in loong_fpr_packed loong_percpu_packed; do
  sed -n "/^${fn}:/,/^\.size ${fn},/p" "$dir/mini.s" > "$dir/$fn.s"
  test -s "$dir/$fn.s"
  # Physical M1 uses a different scratch address register for a scalar FPR
  # output than for an indexed perCPU output. Both still require a native
  # alignment gate and byte-exact fallback for an unknown packed lvalue.
  if [[ "$fn" == loong_fpr_packed ]]; then addr=t7; else addr=t1; fi
  grep -Fq "andi \$t3, \$$addr, 7" "$dir/$fn.s"
  grep -Fq "st.d \$t0, \$$addr, 0" "$dir/$fn.s"
  # In addition to its late byte scatter, the fallback writes the first
  # byte from the live scalar value; this guards against shifted value/ptr.
  grep -Fq "st.b \$t0, \$$addr, 0" "$dir/$fn.s"
  if [[ "$fn" == loong_fpr_packed ]]; then
    # Scalar FPR output is eight selected byte stores, with a real CFG
    # branch around them when the address is naturally aligned.
    grep -Eq '^  beqz \$t3, \.Lminic_mir_' "$dir/$fn.s"
    test "$(grep -Ec '^  st\.b \$t[0-9]+, \$t7, [0-7]$' "$dir/$fn.s")" -eq 8
    grep -Fq 'st.b $t3, $t7, 7' "$dir/$fn.s"
  else
    # Indexed perCPU has the same safety property via an eight-trip
    # outlined loop. PC+8 must land after exactly one branch instruction.
    grep -Eq '^  bnez \$t3, \.Lminic_mir_' "$dir/$fn.s"
    grep -Fq '  li.d $t3, 8' "$dir/$fn.s"
    grep -Fq '  srli.d $t0, $t0, 8' "$dir/$fn.s"
    grep -Fq '  addi.d $t3, $t3, -1' "$dir/$fn.s"
    grep -Eq '^  bnez \$t3, \.Lminic_mir_' "$dir/$fn.s"
    grep -Fq 'jirl $zero, $t6, 0' "$dir/$fn.s"
    awk '
      /^  pcaddi \$t6, 2$/ {
        seen++
        if (getline branch != 1 ||
            branch !~ /^  b \.Lminic_mir_[a-z_0-9]+_[0-9]+$/ ||
            getline continuation != 1 ||
            continuation !~ /^\.Lminic_mir_[a-z_0-9]+_[0-9]+:$/) bad++
      }
      END { if (seen != 1 || bad) exit 1 }
    ' "$dir/$fn.s"
  fi
done
grep -Fq 'movfr2gr.d $t0, $f0' "$dir/loong_fpr_packed.s"
grep -Fq 'ldx.d $t0, $r21, $t1' "$dir/loong_percpu_packed.s"

# The native-width path and byte fallback must both be reached through
# physical MachineIR CFG, not source-local labels or an old private helper.
grep -Eq '^  beqz \$t3, \.Lminic_mir_' "$dir/loong_fpr_packed.s"
grep -Fq '.Lminic_mir_loong_fpr_packed_' "$dir/loong_fpr_packed.s"
for bad in bad-fpr-register bad-percpu-base; do
  case "$bad" in
    bad-fpr-register)
      sed 's/movfr2gr.d %0, \$f0/movfr2gr.d %0, $f32/' "$dir/source.i" > "$dir/bad.i" ;;
    bad-percpu-base)
      sed 's/ldx.d %\[ret\], \$r21, %\[ptr\]/ldx.d %[ret], $r20, %[ptr]/' "$dir/source.i" > "$dir/bad.i" ;;
  esac
  if "$mini" --target=loongarch64-linux -S "$dir/bad.i" -o "$dir/bad.s" >/dev/null 2>&1; then
    echo "LOONG_PACKED_ASM_OUTPUT_UNQUALIFIED=$bad" >&2
    exit 1
  fi
  test ! -e "$dir/bad.s"
  if "$mini" --target=loongarch64-linux -mabi=lp64s -S \
      "$dir/bad.i" -o "$dir/bad-lp64s.s" >/dev/null 2>&1; then
    echo "LOONG_PACKED_ASM_OUTPUT_UNQUALIFIED=lp64s-$bad" >&2
    exit 1
  fi
  test ! -e "$dir/bad-lp64s.s"
done
echo 'LOONG_PACKED_ASM_OUTPUT=PASS GNU_C_SOURCE=PASS guarded_native_and_byte_scatter=2 lp64s_source_FPR=1 negatives=4 Loong_QEMU=NOT_RUN'
