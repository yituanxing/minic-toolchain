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

for fn in loong_fpr_packed loong_percpu_packed; do
  sed -n "/^${fn}:/,/^\.size ${fn},/p" "$dir/mini.s" > "$dir/$fn.s"
  test -s "$dir/$fn.s"
  # Physical M1 uses a different scratch address register for a scalar FPR
  # output than for an indexed perCPU output. Both still require a native
  # alignment gate and byte-exact fallback for an unknown packed lvalue.
  if [[ "$fn" == loong_fpr_packed ]]; then addr=t7; else addr=t1; fi
  grep -Fq "andi \$t3, \$$addr, 7" "$dir/$fn.s"
  grep -Fq "st.b \$t3, \$$addr, 7" "$dir/$fn.s"
  grep -Fq "st.d \$t0, \$$addr, 0" "$dir/$fn.s"
done
grep -Fq 'movfr2gr.d $t0, $f0' "$dir/loong_fpr_packed.s"
grep -Fq 'ldx.d $t0, $r21, $t1' "$dir/loong_percpu_packed.s"

# The native-width path and byte fallback must both be reached through
# physical MachineIR CFG, not source-local labels or an old private helper.
grep -Eq '^  beqz \$t3, \.Lminic_mir_' "$dir/loong_fpr_packed.s"
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
done
echo 'LOONG_PACKED_ASM_OUTPUT=PASS GNU_C_SOURCE=PASS guarded_native_and_byte_scatter=2 negatives=2 Loong_QEMU=NOT_RUN'
