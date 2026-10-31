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
  grep -Fq '.Lloong_store_native_' "$d/$name.s"
  grep -Fq 'st.b $t3,' "$d/$name.s"
done
grep -Fq 'rdtime.d $t0, $t1' "$d/packed_clock.s"
grep -Fq 'andi $t3, $t2, 7' "$d/packed_clock.s"
grep -Fq 'andi $t3, $t2, 3' "$d/packed_clock.s"
grep -Fq 'cpucfg $t0, $t1' "$d/packed_cpucfg.s"
grep -Fq 'andi $t3, $t2, 3' "$d/packed_cpucfg.s"
grep -Fq 'sltu $t2, $t0, $t1' "$d/packed_nospec.s"
grep -Fq 'andi $t3, $t1, 7' "$d/packed_nospec.s"
grep -Fq 'amadd.d $t2, $t0, $t1' "$d/packed_percpu_amo.s"
grep -Fq 'andi $t3, $t1, 7' "$d/packed_percpu_amo.s"
grep -Fq 'csrrd $t0, 7' "$d/packed_frametrace_csr.s"
grep -Fq 'andi $t3, $t1, 7' "$d/packed_frametrace_csr.s"

sed -n '/^aligned_global_frametrace_csr:/,/^\.size aligned_global_frametrace_csr,/p' \
  "$d/source.s" > "$d/aligned.s"
grep -Fq 'csrrd $t0, 7' "$d/aligned.s"
grep -Fq 'st.d $t0, $t1, 0' "$d/aligned.s"
if grep -Fq '.Lloong_store_native_' "$d/aligned.s"; then
  echo 'LOONG_PACKED_RESULT=FAIL static aligned global gained guard' >&2
  exit 1
fi

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
echo 'LOONG_PACKED_ASM_RESULT_FAMILY=PASS GNU_C=PASS positive=5 native_global_fastpath=1 negatives=5 QEMU=NOT_RUN'
