#!/usr/bin/env bash
# Genuine GNU CRT + MiniLD execution, without libc.  Covers an input object
# lacking .symtab, R_X86_64_{REX_,}GOTPCRELX and undefined weak GOT loading.
set -Eeuo pipefail

repo=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
minild=${MINILD:-"$repo/build/release/bin/minic-ld"}
work=${WORK_DIR:-"$repo/build/minild-x86-crt-got-relax"}
mkdir -p "$work"

for tool in gcc as ld.bfd readelf timeout cmp "$minild"; do
  if ! command -v "$tool" >/dev/null; then
    echo "MINILD_X86_CRT=ERROR missing_tool=$tool" >&2
    exit 127
  fi
done
gcc_input() {
  local result
  result=$(gcc "-print-file-name=$1")
  if [[ $result == "$1" || ! -f $result ]]; then
    echo "MINILD_X86_CRT=ERROR missing_crt=$1" >&2
    exit 127
  fi
  printf '%s\n' "$result"
}
crt1=$(gcc_input crt1.o)
crti=$(gcc_input crti.o)
crtn=$(gcc_input crtn.o)
if readelf -SW "$crtn" | grep -Eq '\] \.symtab[[:space:]]'; then
  echo 'MINILD_X86_CRT=ERROR crtn_is_not_symtabless' >&2
  exit 1
fi
readelf -Wr "$crt1" >"$work/crt1.relocations"
readelf -Wr "$crti" >"$work/crti.relocations"
grep -q R_X86_64_GOTPCRELX "$work/crt1.relocations"
grep -q R_X86_64_REX_GOTPCRELX "$work/crt1.relocations"
grep -q __gmon_start__ "$work/crti.relocations"

as -o "$work/stub.o" "$repo/tests/linker/fixtures/x86-crt-relax.s"
inputs=("$crt1" "$crti" "$work/stub.o" "$crtn")
ld.bfd -m elf_x86_64 -static -e _start -o "$work/gnu" \
  "${inputs[@]}" >"$work/gnu-link.log" 2>&1
"$minild" -m elf_x86_64 -static -e _start -o "$work/mini" \
  "${inputs[@]}" >"$work/mini-link.log" 2>&1

for flavor in gnu mini; do
  readelf -h "$work/$flavor" >"$work/$flavor.elf-header"
  grep -q 'EXEC (Executable file)' "$work/$flavor.elf-header"
  grep -q 'Advanced Micro Devices X86-64' "$work/$flavor.elf-header"
  status=0
  timeout --signal=TERM 10s "$work/$flavor" \
    >"$work/$flavor.stdout" 2>"$work/$flavor.stderr" || status=$?
  printf '%s\n' "$status" >"$work/$flavor.exit"
  if (( status != 0 )); then
    echo "MINILD_X86_CRT=FAIL runtime=$flavor exit=$status" >&2
    exit 1
  fi
done
for channel in stdout stderr exit; do
  if ! cmp -s "$work/gnu.$channel" "$work/mini.$channel"; then
    echo "MINILD_X86_CRT=FAIL differential=$channel" >&2
    exit 1
  fi
done

# A non-relaxable GOTPCREL is different: its MOV must actually dereference
# an allocated GOT slot containing the symbol address (G+A-P, not S+A-P).
as --64 -mrelax-relocations=no -o "$work/static-got.o" \
  "$repo/tests/linker/fixtures/x86-static-got.s"
readelf -Wr "$work/static-got.o" >"$work/static-got.relocations"
grep -Eq 'R_X86_64_GOTPCREL[[:space:]]' "$work/static-got.relocations"
ld.bfd -m elf_x86_64 -static -e _start -o "$work/gnu-got" \
  "$work/static-got.o" >"$work/gnu-got-link.log" 2>&1
"$minild" -m elf_x86_64 -static -e _start -o "$work/mini-got" \
  "$work/static-got.o" >"$work/mini-got-link.log" 2>&1
readelf -SW "$work/mini-got" >"$work/mini-got.sections"
grep -Eq '\] \.got[[:space:]]+PROGBITS' "$work/mini-got.sections"
for flavor in gnu mini; do
  status=0
  timeout --signal=TERM 10s "$work/$flavor-got" \
    >"$work/$flavor-got.stdout" 2>"$work/$flavor-got.stderr" || status=$?
  printf '%s\n' "$status" >"$work/$flavor-got.exit"
  if (( status != 0 )); then
    echo "MINILD_X86_CRT=FAIL got_runtime=$flavor exit=$status" >&2
    exit 1
  fi
done
for channel in stdout stderr exit; do
  if ! cmp -s "$work/gnu-got.$channel" "$work/mini-got.$channel"; then
    echo "MINILD_X86_CRT=FAIL got_differential=$channel" >&2
    exit 1
  fi
done

as --64 -mrelax-relocations=no -o "$work/ifunc-negative.o" \
  "$repo/tests/linker/fixtures/x86-ifunc-got-negative.s"
if "$minild" -m elf_x86_64 -static -e _start \
    -o "$work/ifunc-should-not-exist" "$work/ifunc-negative.o" \
    >"$work/ifunc-negative.stdout" 2>"$work/ifunc-negative.stderr"; then
  echo 'MINILD_X86_CRT=FAIL accepted_unsupported_ifunc_got' >&2
  exit 1
fi
if [[ -e "$work/ifunc-should-not-exist" ]] ||
   ! grep -Fq 'unsupported-x86-static-ifunc-got' \
     "$work/ifunc-negative.stderr"; then
  echo 'MINILD_X86_CRT=FAIL bad_ifunc_rejection' >&2
  exit 1
fi

# Positive IFUNC shape: a real SHT_RELA/R_X86_64_IRELATIVE table must map
# a resolver to a writable slot, and the IPLT dispatch must call its result.
# The fixture's CRT increments a counter in the resolver exactly once.
as -o "$work/ifunc-positive.o" \
  "$repo/tests/linker/fixtures/x86-static-ifunc-irelative.s"
ld.bfd -m elf_x86_64 -static -e _start -o "$work/gnu-ifunc" \
  "$work/ifunc-positive.o"
"$minild" -m elf_x86_64 -static -e _start -o "$work/mini-ifunc" \
  "$work/ifunc-positive.o"
for flavor in gnu mini; do
  readelf -Wr "$work/$flavor-ifunc" >"$work/$flavor-ifunc.relocations"
  grep -Fq R_X86_64_IRELATIVE "$work/$flavor-ifunc.relocations"
  readelf -sW "$work/$flavor-ifunc" >"$work/$flavor-ifunc.symbols"
  grep -Fq __rela_iplt_start "$work/$flavor-ifunc.symbols"
  grep -Fq __rela_iplt_end "$work/$flavor-ifunc.symbols"
  status=0
  timeout --signal=TERM 10s "$work/$flavor-ifunc" \
    >"$work/$flavor-ifunc.stdout" 2>"$work/$flavor-ifunc.stderr" || status=$?
  printf '%s\n' "$status" >"$work/$flavor-ifunc.exit"
  if (( status != 0 )); then
    echo "MINILD_X86_CRT=FAIL ifunc_runtime=$flavor exit=$status" >&2
    exit 1
  fi
done
for channel in stdout stderr exit; do
  cmp "$work/gnu-ifunc.$channel" "$work/mini-ifunc.$channel"
done
echo 'MINILD_X86_CRT=PASS crt_symtabless=1 gotpcrelx=2 gotpcrel=1 weak_undef=1 gnu_ld_parity=3 runtime=6 ifunc_irelative=1 ifunc_resolver_side_effect=1 ifunc_got_rejected=1'
