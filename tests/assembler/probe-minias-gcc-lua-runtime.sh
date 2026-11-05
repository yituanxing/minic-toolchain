#!/usr/bin/env bash
# Real upstream Lua 5.5 runtime, linked only by GNU BFD and executed under
# QEMU-user. The ONLY tool changed between the two flavors is GNU AS/MiniAS.
# Identical GCC-generated .s is assembled in both cases; no ELF byte match gate.
set -Eeuo pipefail

repo=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
arch=${1:?usage: probe-minias-gcc-lua-runtime.sh x86_64|aarch64|riscv64|loongarch64}
minias=${MINIAS:-"$repo/build/release/bin/minic-as"}
work=${WORK_DIR:-"$repo/build/minias-gcc-lua-runtime/$arch"}
release=5.5.0
upstream_sha256=57ccc32bbbd005cab75bcc52444052535af691789dba2b9016d5c50640d68b3d
url="https://www.lua.org/ftp/lua-$release.tar.gz"
script="$repo/tests/assembler/runtime-gcc-lua/smoke.lua"

case "$arch" in
  x86_64)
    cc=${CC:-gcc}; gas=${GNU_AS:-as}; readelf=${GNU_READELF:-readelf}
    qemu=${QEMU_USER:-qemu-x86_64}
    gas_flags=(--64); gcc_flags=()
    mini_flags=(--target=x86_64 -march=x86-64 -mabi=sysv)
    machine='Advanced Micro Devices X86-64'
    ;;
  aarch64)
    cc=${CC:-aarch64-linux-gnu-gcc}
    gas=${GNU_AS:-aarch64-linux-gnu-as}
    readelf=${GNU_READELF:-aarch64-linux-gnu-readelf}
    qemu=${QEMU_USER:-qemu-aarch64}
    gas_flags=(-march=armv8-a); gcc_flags=()
    mini_flags=(--target=aarch64 -march=armv8-a -mabi=lp64)
    machine='AArch64'
    ;;
  riscv64)
    cc=${CC:-riscv64-linux-gnu-gcc}
    gas=${GNU_AS:-riscv64-linux-gnu-as}
    readelf=${GNU_READELF:-riscv64-linux-gnu-readelf}
    qemu=${QEMU_USER:-qemu-riscv64}
    gcc_flags=(-march=rv64gc -mabi=lp64d -mno-relax)
    gas_flags=(-march=rv64gc -mabi=lp64d -mno-relax)
    mini_flags=(--target=riscv -march=rv64gc -mabi=lp64d)
    machine='RISC-V'
    ;;
  loongarch64)
    cc=${CC:-loongarch64-linux-gnu-gcc-13}
    gas=${GNU_AS:-loongarch64-linux-gnu-as}
    readelf=${GNU_READELF:-loongarch64-linux-gnu-readelf}
    qemu=${QEMU_USER:-qemu-loongarch64}
    gcc_flags=(-mabi=lp64d); gas_flags=(-mabi=lp64d)
    mini_flags=(--target=loongarch64 -march=loongarch64 -mabi=lp64d)
    machine='LoongArch'
    ;;
  *) echo "MINIAS_LUA_RUNTIME=ERROR unsupported_arch=$arch" >&2; exit 64 ;;
esac

for tool in "$cc" "$gas" "$readelf" "$qemu" "$minias" curl sha256sum tar timeout cmp; do
  if ! command -v "$tool" >/dev/null; then
    echo "MINIAS_LUA_RUNTIME=ERROR arch=$arch missing_tool=$tool" >&2
    exit 127
  fi
done

mkdir -p "$work"/{source,asm,gnu,mini,evidence}
# Failed reruns must not borrow a previous binary or successful runtime record.
for flavor in gnu mini; do
  rm -f "$work/$flavor/lua" "$work/evidence/$flavor"-{stdout,stderr,exit}.txt \
        "$work/evidence/$flavor-link.log"
done
: >"$work/evidence/summary.txt"
archive="$work/source/lua-$release.tar.gz"
src="$work/source/lua-$release/src"
printf 'source_url=%s\nsource_sha256=%s\nhead=%s\narch=%s\nrunner=%s\n' \
  "$url" "$upstream_sha256" "${GITHUB_SHA:-local-uncommitted}" "$arch" "$qemu" \
  >"$work/evidence/identity.txt"
"$cc" --version >"$work/evidence/gcc-version.txt"
"$gas" --version >"$work/evidence/gnu-as-version.txt"
sha256sum "$minias" >"$work/evidence/minias.sha256"
sha256sum "$script" >"$work/evidence/lua-script.sha256"
"$cc" -print-prog-name=ld.bfd >"$work/evidence/gnu-ld-path.txt"

if ! test -s "$archive"; then
  if ! curl --fail --location --retry 3 --max-time 90 --silent --show-error \
       "$url" -o "$archive" >"$work/evidence/curl.log" 2>&1; then
    echo "MINIAS_LUA_RUNTIME=FAIL arch=$arch stage=download" >&2
    exit 1
  fi
fi
if ! printf '%s  %s\n' "$upstream_sha256" "$archive" | sha256sum -c - \
     >"$work/evidence/source-checksum.log" 2>&1; then
  echo "MINIAS_LUA_RUNTIME=FAIL arch=$arch stage=source-sha256" >&2
  exit 1
fi
if ! test -f "$src/lua.c"; then
  if ! tar --no-same-owner -xzf "$archive" -C "$work/source" \
       >"$work/evidence/extract.log" 2>&1; then
    echo "MINIAS_LUA_RUNTIME=FAIL arch=$arch stage=extract" >&2
    exit 1
  fi
fi

# Official upstream Lua interpreter (not its bytecode compiler or a synthetic
# parser). The library and interpreter include the real allocator, GC, tables,
# VM, parser, coroutine, Lua standard library, and POSIX/dlopen integration.
units=(lapi lcode lctype ldebug ldo ldump lfunc lgc llex lmem lobject lopcodes
       lparser lstate lstring ltable ltm lundump lvm lzio
       lauxlib lbaselib lcorolib ldblib liolib lmathlib loadlib loslib
       lstrlib ltablib lutf8lib linit lua)

failures=()
fail() {
  failures+=("$1:$2")
  echo "MINIAS_LUA_RUNTIME_STAGE=FAIL arch=$arch stage=$1 detail=$2" >&2
  # Independent TU diagnostics are useful on a 33-file interpreter build:
  # every failing GCC/AS owner is printed, not only the first one. The CI
  # console stays bounded while full stderr survives in uploaded artifacts.
  local stage=$1 detail=$2 log=''
  case "$stage" in
    gcc-S) log="$work/evidence/gcc-$detail.log" ;;
    gnu-as) log="$work/evidence/gnu-as-$detail.log" ;;
    mini-as) log="$work/evidence/mini-as-$detail.log" ;;
    gnu|mini)
      if [[ "$detail" == gnu-bfd-link ]]; then
        log="$work/evidence/$stage-link.log"
      fi
      ;;
  esac
  if [[ -n "$log" ]]; then
    printf 'MINIAS_LUA_DIAGNOSTIC arch=%s stage=%s unit=%s file=%s\n' \
      "$arch" "$stage" "$detail" "$log" >&2
    if [[ -f "$log" ]]; then
      sed -n '1,20p' "$log" >&2
    else
      printf 'MINIAS_LUA_DIAGNOSTIC arch=%s missing_log=%s\n' \
        "$arch" "$log" >&2
    fi
  fi
}

: >"$work/evidence/sources.sha256"
for unit in "${units[@]}"; do
  source="$src/$unit.c"
  asm="$work/asm/$unit.s"
  rm -f "$asm" "$work/gnu/$unit.o" "$work/mini/$unit.o"
  if ! "$cc" "${gcc_flags[@]}" -O1 -g0 -std=gnu99 -DLUA_USE_LINUX \
       -fno-pie -fno-pic -I"$src" -S "$source" -o "$asm" \
       >"$work/evidence/gcc-$unit.log" 2>&1 || ! test -s "$asm"; then
    fail gcc-S "$unit"
    continue
  fi
  sha256sum "$source" "$asm" >>"$work/evidence/sources.sha256"
  if ! "$gas" "${gas_flags[@]}" -o "$work/gnu/$unit.o" "$asm" \
       >"$work/evidence/gnu-as-$unit.log" 2>&1; then
    fail gnu-as "$unit"
  fi
  if ! "$minias" "${mini_flags[@]}" -o "$work/mini/$unit.o" "$asm" \
       >"$work/evidence/mini-as-$unit.log" 2>&1; then
    fail mini-as "$unit"
  fi
done

# Continue to linker/runtime attempts on either complete side; one failed TU
# must not conceal assembly blockers in later files.
for flavor in gnu mini; do
  objects=()
  for unit in "${units[@]}"; do
    if test -s "$work/$flavor/$unit.o"; then
      objects+=("$work/$flavor/$unit.o")
    fi
  done
  if (( ${#objects[@]} != ${#units[@]} )); then
    fail "$flavor" "missing-objects:${#objects[@]}/${#units[@]}"
    continue
  fi
  program="$work/$flavor/lua"
  rm -f "$program" "$work/evidence/$flavor"-{stdout,stderr,exit}.txt
  if ! "$cc" "${gcc_flags[@]}" -static -no-pie -fuse-ld=bfd -Wl,-v \
       -o "$program" "${objects[@]}" -lm -ldl -pthread \
       >"$work/evidence/$flavor-link.log" 2>&1 || ! test -s "$program"; then
    fail "$flavor" gnu-bfd-link
    continue
  fi
  if ! grep -Fq 'GNU ld' "$work/evidence/$flavor-link.log" ||
     ! "$readelf" -h "$program" >"$work/evidence/$flavor-elf-header.txt" ||
     ! grep -Fq "$machine" "$work/evidence/$flavor-elf-header.txt" ||
     ! "$readelf" -l "$program" >"$work/evidence/$flavor-program-headers.txt" ||
     grep -q INTERP "$work/evidence/$flavor-program-headers.txt"; then
    fail "$flavor" link-provenance-or-static
    continue
  fi
  result=0
  timeout --signal=TERM 60s "$qemu" "$program" "$script" \
    >"$work/evidence/$flavor-stdout.txt" \
    2>"$work/evidence/$flavor-stderr.txt" || result=$?
  printf '%s\n' "$result" >"$work/evidence/$flavor-exit.txt"
  if (( result != 0 )); then
    fail "$flavor" "runtime-exit:$result"
  fi
  expected='MINIAS_LUA_RUNTIME squares=385 closure=47 coroutine=21 result=PASS'
  if test "$(printf '%s\n' "$expected")" != \
       "$(head -n 1 "$work/evidence/$flavor-stdout.txt")" ||
     test "$(wc -l <"$work/evidence/$flavor-stdout.txt")" != 1; then
    fail "$flavor" runtime-contract
  fi
  if test -s "$work/evidence/$flavor-stderr.txt"; then
    fail "$flavor" runtime-stderr
  fi
done

if test -f "$work/evidence/gnu-exit.txt" &&
   test -f "$work/evidence/mini-exit.txt"; then
  for channel in stdout stderr exit; do
    if ! cmp -s "$work/evidence/gnu-$channel.txt" \
                 "$work/evidence/mini-$channel.txt"; then
      fail comparison "runtime-difference:$channel"
    fi
  done
else
  fail comparison missing-runtime-side
fi

printf '%s\n' "${failures[@]}" >"$work/evidence/failures.txt"
if (( ${#failures[@]} != 0 )); then
  echo "MINIAS_LUA_RUNTIME=FAIL arch=$arch assembled_mini=$(find "$work/mini" -name '*.o' | wc -l)/${#units[@]} reasons=${failures[*]}" | \
    tee "$work/evidence/summary.txt" >&2
  exit 1
fi
sha256sum "$work/gnu/lua" "$work/mini/lua" >"$work/evidence/programs.sha256"
echo "MINIAS_LUA_RUNTIME=PASS arch=$arch units=${#units[@]}/${#units[@]} bfd_links=2 real_runs=2 output_equal=1 runner=$qemu" | \
  tee "$work/evidence/summary.txt"
