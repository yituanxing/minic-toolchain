#!/usr/bin/env bash
# Original Linux 6.6.143 .discard.addressable, .data..percpu and ELF weak
# attributes. The emitter owns *actual* target RELA, not just output .s.
set -Eeuo pipefail
cc=${1:?MiniC compiler binary}
target=${2:?Target name}
test -x "$cc"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat > "$work/linux.i" <<'SRC'
extern unsigned long addressable_export;
static void * __attribute__((used,section(".discard.addressable")))
__UNIQUE_ID___addressable_addressable_export250 = (void *)&addressable_export;
struct cpu_info { unsigned long data[4]; };
__attribute__((section(".data..percpu"))) struct cpu_info cpu_data;
unsigned long __attribute__((weak,section(".data.weak"))) weak_data = 42;
SRC
"$cc" --target="$target-linux" -S "$work/linux.i" -o "$work/minic.s"
test -s "$work/minic.s"
for section in .discard.addressable .data..percpu .data.weak; do
  grep -Fq ".section $section" "$work/minic.s"
done
grep -Fq '.weak weak_data' "$work/minic.s"
if [[ "${MINIC_SECTION_ONLY:-0}" != 1 ]]; then
  case "$target" in
    x86_64) gnu=gcc; elftool=readelf ;;
    aarch64) gnu=aarch64-linux-gnu-gcc; elftool=aarch64-linux-gnu-readelf ;;
    loongarch64) gnu=loongarch64-linux-gnu-gcc-13; elftool=loongarch64-linux-gnu-readelf ;;
    riscv64) gnu=riscv64-linux-gnu-gcc; elftool=riscv64-linux-gnu-readelf ;;
    *) echo "not a GNU oracle target $target" >&2; exit 1 ;;
  esac
  command -v "$gnu" >/dev/null
  "$gnu" -c -x assembler "$work/minic.s" -o "$work/minic.o"
  "$gnu" -c -x c "$work/linux.i" -o "$work/gnu.o"
  for owner in minic gnu; do
    "$elftool" -SW "$work/$owner.o" > "$work/$owner.sections"
    "$elftool" -sW "$work/$owner.o" > "$work/$owner.symbols"
    "$elftool" -rW "$work/$owner.o" > "$work/$owner.relocs"
    for section in .discard.addressable .data..percpu .data.weak; do
      grep -Fq "$section" "$work/$owner.sections"
    done
    grep -E 'WEAK[[:space:]]+DEFAULT' "$work/$owner.symbols" | grep -Fq weak_data
    grep -Fq addressable_export "$work/$owner.relocs"
  done
fi
cat > "$work/inject.i" <<'SRC'
int __attribute__((section(".evil; nop"))) injected=1;
SRC
if "$cc" --target="$target-linux" -S "$work/inject.i" -o "$work/inject.s" >"$work/log" 2>&1; then
  echo "unsafe custom ELF section accepted $target" >&2
  exit 1
fi
test ! -e "$work/inject.s"
echo "MINIC_LINUX_GLOBAL_SECTION=PASS target=$target sections=3 weak=1 reloc=1 injected_directive=rejected"
