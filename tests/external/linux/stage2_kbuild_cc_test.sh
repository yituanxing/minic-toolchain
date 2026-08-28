#!/usr/bin/env bash
set -Eeuo pipefail

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
wrapper="$root/tests/external/linux/stage2_kbuild_cc.sh"
work=${BUILD_DIR:-"$root/build/stage2-kbuild-wrapper-test"}

rm -rf "$work"
mkdir -p "$work/bin" "$work/out"

cat >"$work/bin/fake-real-cc" <<'SH'
#!/usr/bin/env bash
set -Eeuo pipefail
{
  printf 'real'
  printf ' %q' "$@"
  printf '\n'
} >>"$FAKE_REAL_LOG"

mode=delegate
out=
src=
for ((i=1; i<=$#; ++i)); do
  arg=${!i}
  case "$arg" in
    -E) mode=preprocess ;;
    -c) [[ "$mode" == preprocess ]] || mode=assemble ;;
    -o)
      j=$((i+1))
      out=${!j}
      ((++i))
      ;;
    *.c|*.i|*.s|*.S)
      [[ -n "$src" ]] || src=$arg
      ;;
  esac
done

case "$mode" in
  preprocess)
    [[ -n "$out" && -n "$src" ]]
    printf '/* preprocessed */\n' >"$out"
    cat "$src" >>"$out"
    ;;
  assemble)
    [[ -n "$out" ]]
    printf 'FAKE_OBJECT\n' >"$out"
    ;;
  delegate)
    exit 0
    ;;
esac
SH
chmod +x "$work/bin/fake-real-cc"

cat >"$work/bin/fake-minic" <<'SH'
#!/usr/bin/env bash
set -Eeuo pipefail
{
  printf 'minic'
  printf ' %q' "$@"
  printf '\n'
} >>"$FAKE_MINIC_LOG"

in=
out=
while (( $# )); do
  case "$1" in
    -S)
      shift
      in=$1
      ;;
    -o)
      shift
      out=$1
      ;;
  esac
  shift
done
[[ -n "$in" && -n "$out" ]]
printf '.text\n.globl fake_symbol\nfake_symbol:\n  ret\n' >"$out"
SH
chmod +x "$work/bin/fake-minic"

export FAKE_REAL_LOG="$work/real.log"
export FAKE_MINIC_LOG="$work/minic.log"
trace="$work/trace.log"

cat >"$work/input.c" <<'C'
int kbuild_wrapper_probe(void) { return 7; }
C
cat >"$work/input.S" <<'S'
.text
S

MINIC="$work/bin/fake-minic" REAL_CC="$work/bin/fake-real-cc" MINIC_KBUILD_TRACE="$trace" MINIC_KEEP_INTERMEDIATES=1   "$wrapper"     -Wp,-MMD,"$work/out/.probe.o.d"     -nostdinc -Iinclude -include include/linux/kconfig.h     -D__KERNEL__ -std=gnu11 -O2     -mabi=lp64 -march=rv64imac_zicsr_zifencei     -mcmodel=medany -mstrict-align -Wa,-mno-arch-attr     -c -o "$work/out/probe.o" "$work/input.c"

test -s "$work/out/probe.o"
test -s "$work/out/probe.minic-stage2.i"
test -s "$work/out/probe.minic-stage2.s"

echo "STAGE2_KBUILD_WRAPPER_REAL_LOG_BEGIN"
cat "$FAKE_REAL_LOG"
echo "STAGE2_KBUILD_WRAPPER_REAL_LOG_END"
echo "STAGE2_KBUILD_WRAPPER_MINIC_LOG_BEGIN"
cat "$FAKE_MINIC_LOG"
echo "STAGE2_KBUILD_WRAPPER_MINIC_LOG_END"
echo "STAGE2_KBUILD_WRAPPER_TRACE_BEGIN"
cat "$trace"
echo "STAGE2_KBUILD_WRAPPER_TRACE_END"
grep -F -- '-E' "$FAKE_REAL_LOG" >/dev/null
grep -F -- '-P' "$FAKE_REAL_LOG" >/dev/null
grep -F -- '-MT' "$FAKE_REAL_LOG" >/dev/null
grep -F -- "$work/out/probe.o" "$FAKE_REAL_LOG" >/dev/null
grep -F -- '-march=rv64imac_zicsr_zifencei' "$FAKE_REAL_LOG" >/dev/null
grep -F -- '-mabi=lp64' "$FAKE_REAL_LOG" >/dev/null
grep -F -- '-Wa\,-mno-arch-attr' "$FAKE_REAL_LOG" >/dev/null
grep -F -- "$work/out/probe.minic-stage2.i" "$FAKE_MINIC_LOG" >/dev/null
grep -F -- "$work/out/probe.minic-stage2.s" "$FAKE_MINIC_LOG" >/dev/null
grep -F 'pass source=' "$trace" >/dev/null

before=$(wc -l <"$FAKE_MINIC_LOG")
MINIC="$work/bin/fake-minic" REAL_CC="$work/bin/fake-real-cc"   "$wrapper" -E -D__KERNEL__ "$work/input.c" -o "$work/out/probe.i"
after=$(wc -l <"$FAKE_MINIC_LOG")
test "$before" -eq "$after"

MINIC="$work/bin/fake-minic" REAL_CC="$work/bin/fake-real-cc"   "$wrapper" -march=rv64imac -mabi=lp64 -c -o "$work/out/probe-S.o" "$work/input.S"
after2=$(wc -l <"$FAKE_MINIC_LOG")
test "$before" -eq "$after2"

echo "STAGE2_KBUILD_WRAPPER_TEST=PASS"
