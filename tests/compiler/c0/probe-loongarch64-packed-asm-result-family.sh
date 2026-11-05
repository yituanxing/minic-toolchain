#!/usr/bin/env bash
set -Eeuo pipefail
mini=${1:?MiniC compiler}
test -x "$mini"
here=$(cd "$(dirname "$0")" && pwd)
source_file="$here/loongarch64_packed_asm_result_family.c"
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT

# Host GCC verifies that packed register-output lvalues are legal GNU C.
cc -std=gnu11 -Wall -Wextra -Werror -fsyntax-only "$source_file"
cc -E -P -x c "$source_file" -o "$d/source.i"
"$mini" --target=loongarch64-linux -S "$d/source.i" -o "$d/source.s"

for name in packed_clock packed_cpucfg packed_nospec \
            packed_percpu_amo packed_frametrace_csr; do
  sed -n "/^${name}:/,/^\.size ${name},/p" "$d/source.s" > "$d/$name.s"
  test -s "$d/$name.s"
  # Unknown-alignment writes need a real MIR guard, byte scatter and
  # physical CFG continuation, never a private source-label helper.
  grep -Fq '.Lminic_mir_' "$d/$name.s"
  case "$name" in
    packed_clock|packed_cpucfg|packed_frametrace_csr)
      addr=t7
      # These target families select an inline eight/four-byte scatter.
      # The zero-misalignment branch bypasses every single-byte store.
      grep -Eq '^  beqz \$t3, \.Lminic_mir_' "$d/$name.s"
      if [[ "$name" == packed_cpucfg ]]; then last=3; else last=7; fi
      grep -Fq "st.b \$t3, \$$addr, $last" "$d/$name.s"
      ;;
    packed_nospec|packed_percpu_amo)
      addr=t1
      # A nonzero alignment value enters the outlined eight-trip helper.
      grep -Eq '^  bnez \$t3, \.Lminic_mir_' "$d/$name.s"
      grep -Fq '  pcaddi $t6, 2' "$d/$name.s"
      grep -Fq '  li.d $t3, 8' "$d/$name.s"
      grep -Fq '  jirl $zero, $t6, 0' "$d/$name.s"
      ;;
  esac
  grep -Eq "^  andi \\\$t3, \\\$$addr, (3|7)$" "$d/$name.s"
  grep -Eq "^  st\\.b \\\$(t0|t2|t3), \\\$$addr, [0-9]+$" "$d/$name.s"
done
grep -Fq 'rdtime.d $t0, $t1' "$d/packed_clock.s"
grep -Fq 'andi $t3, $t7, 7' "$d/packed_clock.s"
grep -Fq 'andi $t3, $t7, 3' "$d/packed_clock.s"
grep -Fq 'cpucfg $t0, $t1' "$d/packed_cpucfg.s"
grep -Fq 'andi $t3, $t7, 3' "$d/packed_cpucfg.s"
grep -Eq '^  sltu[[:space:]]+\$t2,[[:space:]]*\$t0,[[:space:]]*\$t1$' "$d/packed_nospec.s"
grep -Fq 'andi $t3, $t1, 7' "$d/packed_nospec.s"
grep -Fq 'amadd.d $t2, $t0, $t1' "$d/packed_percpu_amo.s"
grep -Fq 'andi $t3, $t1, 7' "$d/packed_percpu_amo.s"
grep -Eq '^  csrrd \$t0, (0x7|7)$' "$d/packed_frametrace_csr.s"
grep -Fq 'andi $t3, $t7, 7' "$d/packed_frametrace_csr.s"

sed -n '/^aligned_global_frametrace_csr:/,/^\.size aligned_global_frametrace_csr,/p' \
  "$d/source.s" > "$d/aligned.s"
grep -Eq '^  csrrd \$t0, (0x7|7)$' "$d/aligned.s"
grep -Eq '^  st\.d \$t0, \$(t7|t1), 0$' "$d/aligned.s"
if grep -Eq '^  andi \$t3, \$(t7|t1), 7$' "$d/aligned.s" ||
   grep -Fq '  pcaddi $t6, 2' "$d/aligned.s"; then
  echo 'LOONG_PACKED_RESULT=FAIL static aligned global gained guard' >&2
  exit 1
fi

# For source forms that emit an outlined helper, a selected CFG branch must
# follow pcaddi immediately and its return land at the zero-width MIR label.
# Clock/CPUCFG/CSR deliberately use inlined byte stores and need no pcaddi.
awk '
  /^packed_(clock|nospec|percpu_amo|frametrace_csr):$/ {
    name=$0; sub(/:$/, "", name)
  }
  /^  pcaddi \$t6, 2$/ {
    if (name !~ /^packed_(nospec|percpu_amo)$/) bad++
    seen[name]++
    if (getline branch != 1 ||
        branch !~ /^  b \.Lminic_mir_[a-z_0-9]+_[0-9]+$/ ||
        getline continuation != 1 ||
        continuation !~ /^\.Lminic_mir_[a-z_0-9]+_[0-9]+:$/) bad++
  }
  END {
    if (bad || seen["packed_clock"] != 0 ||
        seen["packed_nospec"] != 1 ||
        seen["packed_percpu_amo"] != 1 ||
        seen["packed_frametrace_csr"] != 0) exit 1
  }
' "$d/source.s" || {
  echo 'LOONG_PACKED_RESULT=FAIL pcaddi/branch/continuation layout' >&2
  exit 1
}

for bad in clock cpucfg nospec percpu_amo frametrace; do
  case "$bad" in
    clock) sed 's/rdtime.d %0, %1/rdtime.d %0, %1; nop/' "$d/source.i" > "$d/bad.i" ;;
    cpucfg) sed 's/cpucfg %0, %1/cpucfg %0, %1; nop/' "$d/source.i" > "$d/bad.i" ;;
    nospec) sed 's/sltu\\t%0/slt\\t%0/' "$d/source.i" > "$d/bad.i" ;;
    percpu_amo) sed 's/amadd.d %\[ret\]/amadd.d %[ret]; nop/' "$d/source.i" > "$d/bad.i" ;;
    frametrace) sed 's/csrrd %0, 0x7/csrrd %0, 0x8/' "$d/source.i" > "$d/bad.i" ;;
  esac
  if "$mini" --target=loongarch64-linux -S "$d/bad.i" \
      -o "$d/bad.s" >/dev/null 2>&1; then
    echo "LOONG_PACKED_RESULT=FAIL unqualified $bad" >&2
    exit 1
  fi
  test ! -e "$d/bad.s"
done
echo 'LOONG_PACKED_ASM_RESULT_FAMILY=PASS GNU_C=PASS positive=5 typed_cfg_guarded_writeback=5 native_global_fastpath=1 negatives=5 QEMU=NOT_RUN'
