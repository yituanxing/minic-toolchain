#!/usr/bin/env bash
# Original official Lua 5.5.0: complete RV32I/ILP32 source census.
# No generated Lua source, skipped functions, host headers, synthetic
# standard library, or GCC fallback in any MiniC-built Lua object.
set -Eeuo pipefail
: "${MINIC_CC:?set MINIC_CC to MiniC}"
: "${MINIAS:=${MINIC_CC%/minic-cc}/minic-as}"
: "${BARE_CC:=riscv64-unknown-elf-gcc}"
: "${RISCV_CC:=riscv64-linux-gnu-gcc}"
root=$(git rev-parse --show-toplevel)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
target=(-march=rv32i -mabi=ilp32)
libc=(-specs=picolibc.specs)
# Same real Picolibc errno function ABI as qualified zlib 1.3.1,
# never erase its TLS qualifier in the MiniC parser.
predefines=(-D__PICOLIBC_ERRNO_FUNCTION=__minic_picolibc_errno)
sources=(lapi.c lcode.c lctype.c ldebug.c ldo.c ldump.c lfunc.c lgc.c
 llex.c lmem.c lobject.c lopcodes.c lparser.c lstate.c lstring.c ltable.c
 ltm.c lundump.c lvm.c lzio.c lauxlib.c lbaselib.c lcorolib.c ldblib.c
 liolib.c lmathlib.c loadlib.c loslib.c lstrlib.c ltablib.c lutf8lib.c
 linit.c lua.c luac.c)
expected=57ccc32bbbd005cab75bcc52444052535af691789dba2b9016d5c50640d68b3d
for cmd in curl sha256sum "$BARE_CC" "$RISCV_CC" qemu-riscv32; do
  command -v "$cmd" >/dev/null
done
curl -fsSL --retry 3 --retry-delay 2 --max-time 120 \
  "https://www.lua.org/ftp/lua-5.5.0.tar.gz" -o "$work/lua.tar.gz"
printf '%s  %s\n' "$expected" "$work/lua.tar.gz" | sha256sum -c - >/dev/null
tar -xzf "$work/lua.tar.gz" -C "$work"
src="$work/lua-5.5.0/src"
test -f "$src/lua.c" && test -f "$src/luac.c"
mkdir "$work/obj" "$work/run"
echo "RV32_LUA_SOURCE=VERIFIED release=5.5.0 sha256=$expected count=34"
compiled=0
blocked=0
# One GNU-preprocessed RV32I/ILP32 source is the exact input to both
# compilers. The upstream default portable configuration is deliberate
# and identical for reference + MiniC; no GNU/glibc/Linux shims.
for source in "${sources[@]}"; do
  stem="${source%.c}"
  "$BARE_CC" "${target[@]}" "${libc[@]}" "${predefines[@]}" \
    -E -P -std=c99 -I"$src" "$src/$source" \
    -o "$work/obj/$stem.i"
  if ! "$BARE_CC" "${target[@]}" "${libc[@]}" -O0 -ffreestanding -fno-builtin \
    -x c -c "$work/obj/$stem.i" -o "$work/obj/$stem-reference.o" \
    >"$work/obj/$stem.gcc.out" 2>"$work/obj/$stem.gcc.err"; then
    echo "RV32_LUA=FAIL source=$source stage=GNU_C_reference" >&2
    head -n 22 "$work/obj/$stem.gcc.err" >&2
    exit 1
  fi
  if ! "$MINIC_CC" --target=riscv32-linux "${target[@]}" -S \
    "$work/obj/$stem.i" -o "$work/obj/$stem.s" \
    >"$work/obj/$stem.minic.out" 2>"$work/obj/$stem.minic.err"; then
    blocked=$((blocked+1))
    echo "RV32_LUA_TU=BLOCKED source=$source stage=MiniC" >&2
    head -n 14 "$work/obj/$stem.minic.err" >&2
    line=$(sed -n "s/.*$stem\\.i:\\([0-9][0-9]*\\):.*/\\1/p" \
       "$work/obj/$stem.minic.err" | head -n 1)
    if [[ -n "$line" ]]; then
      first=$((line > 4 ? line-4 : 1))
      end=$((line+4))
      nl -ba "$work/obj/$stem.i" | sed -n "${first},${end}p" >&2
    fi
    continue
  fi
  if ! "$RISCV_CC" "${target[@]}" -c "$work/obj/$stem.s" \
    -o "$work/obj/$stem-gnu-as.o" \
    >"$work/obj/$stem.gnu-as.out" 2>"$work/obj/$stem.gnu-as.err"; then
    blocked=$((blocked+1))
    echo "RV32_LUA_TU=BLOCKED source=$source stage=GNU_AS" >&2
    head -n 14 "$work/obj/$stem.gnu-as.err" >&2
    continue
  fi
  if ! "$MINIAS" "${target[@]}" -o "$work/obj/$stem-minias.o" \
    "$work/obj/$stem.s" >"$work/obj/$stem.mini-as.out" \
    2>"$work/obj/$stem.mini-as.err"; then
    blocked=$((blocked+1))
    echo "RV32_LUA_TU=BLOCKED source=$source stage=MiniAS" >&2
    head -n 14 "$work/obj/$stem.mini-as.err" >&2
    continue
  fi
  compiled=$((compiled+1))
  echo "RV32_LUA_TU=PASS source=$source compiler=MiniC GNU_AS=1 MiniAS=1"
done
echo "RV32_LUA_TU_SUMMARY total=34 compiled=$compiled blocked=$blocked"
if ((blocked != 0)); then
  echo "RV32_LUA=BLOCKED compiler_frontier=$blocked of=34" >&2
  exit 1
fi
# Linux RV32 process entry/Picolibc bridge is separately qualified
# with real cJSON, zlib and original minigzip on identical ELF32/QEMU.
for name in rv32-linux-picolibc-start.S rv32-linux-picolibc-syscalls.c \
            rv32-linux-picolibc-posix.c rv32-linux-picolibc-errno.c; do
  stem="${name%.*}"
  "$BARE_CC" "${target[@]}" "${libc[@]}" -O0 -ffreestanding \
    -c "$root/tests/compiler/c0/$name" -o "$work/obj/$stem.o"
done
picolibc=$(find /usr/lib/picolibc -type f -path '*/rv32i/ilp32/libc.a' -print -quit)
picomath=$(find /usr/lib/picolibc -type f -path '*/rv32i/ilp32/libm.a' -print -quit)
baregcc=$("$BARE_CC" "${target[@]}" -print-libgcc-file-name)
for f in "$picolibc" "$picomath" "$baregcc"; do test -f "$f"; done
# The original lua.c is selected as the sole main(), excluding luac.c.
# No Lua function is delegated to GNU C in MiniC modes.
pressure='local t={3,1,2}; table.sort(t); assert(table.concat(t,",")=="1,2,3"); local function f(n) return n*7 end; assert(f(6)==42); local co=coroutine.create(function() coroutine.yield(21); return 42 end); local ok,x=coroutine.resume(co); assert(ok and x==21); ok,x=coroutine.resume(co); assert(ok and x==42); assert(math.floor(42.9)==42); print("LUA_RV32",string.reverse("CiniM"),string.format("%d",f(6)))'
for mode in reference gnu-as minias; do
  objects=()
  for source in "${sources[@]}"; do
    [[ "$source" == luac.c ]] && continue
    stem="${source%.c}"
    case "$mode" in
      reference) kind=reference ;;
      gnu-as) kind=gnu-as ;;
      minias) kind=minias ;;
    esac
    objects+=("$work/obj/$stem-$kind.o")
  done
  if ! riscv64-linux-gnu-ld -m elf32lriscv -static -e _start \
    -o "$work/run/lua-$mode.elf" \
    "$work/obj/rv32-linux-picolibc-start.o" \
    "$work/obj/rv32-linux-picolibc-syscalls.o" \
    "$work/obj/rv32-linux-picolibc-posix.o" \
    "$work/obj/rv32-linux-picolibc-errno.o" \
    "${objects[@]}" \
    --start-group "$picomath" "$picolibc" "$baregcc" --end-group \
    >"$work/run/$mode.link.out" 2>"$work/run/$mode.link.err"; then
    echo "RV32_LUA=FAIL stage=ELF32_link mode=$mode" >&2
    head -n 35 "$work/run/$mode.link.err" >&2
    exit 1
  fi
  riscv64-linux-gnu-readelf -h "$work/run/lua-$mode.elf" |
    grep -q 'Class:.*ELF32'
  set +e
  timeout 75s qemu-riscv32 "$work/run/lua-$mode.elf" -e "$pressure" \
    >"$work/run/$mode.out" 2>"$work/run/$mode.err"
  rc=$?
  set -e
  if ((rc != 0)); then
    echo "RV32_LUA=FAIL stage=QEMU mode=$mode exit=$rc" >&2
    head -n 28 "$work/run/$mode.err" >&2
    timeout 8s qemu-riscv32 -strace "$work/run/lua-$mode.elf" \
      -e "$pressure" 2>&1 | tail -n 28 >&2 || :
    exit 1
  fi
  echo "RV32_LUA_INTERPRETER=PASS mode=$mode original_lua=1 QEMU=1"
done
test -s "$work/run/reference.out"
for mode in gnu-as minias; do
  cmp -s "$work/run/reference.out" "$work/run/$mode.out" || {
    echo "RV32_LUA=FAIL stage=stdout_differential mode=$mode" >&2
    diff -u "$work/run/reference.out" "$work/run/$mode.out" >&2 || :
    exit 1
  }
  cmp -s "$work/run/reference.err" "$work/run/$mode.err" || {
    echo "RV32_LUA=FAIL stage=stderr_differential mode=$mode" >&2
    exit 1
  }
done
echo 'RV32_LUA=PASS original_5_5_0=1 TUs=34 MiniC_only=1 Picolibc=1 GCC_reference=1 GNU_AS=1 MiniAS=1 ELF32=1 QEMU=1 interpreter=byte_exact'

# The second original Lua 5.5.0 executable: unchanged luac.c.
# Link exactly the 32 library TUs and luac.c with each compiler mode,
# excluding lua.c so no fake main() or partial GNU object substitution.
cat >"$work/bytecode_pressure.lua" <<'LUA'
local record = setmetatable({x=13}, {
  __index = function(_, k) if k == "y" then return 29 end end
})
local function f(n) return n + record.y end
assert(f(record.x) == 42)
local t = {10,3,2,6}; table.sort(t)
assert(table.concat(t,",") == "2,3,6,10")
local co = coroutine.create(function()
  coroutine.yield(21)
  return 42
end)
local ok, value = coroutine.resume(co)
assert(ok and value == 21)
ok,value = coroutine.resume(co)
assert(ok and value == 42)
print("LUAC_RV32", f(13), table.concat(t,","))
LUA
for mode in reference gnu-as minias; do
  objects=()
  for source in "${sources[@]}"; do
    [[ "$source" == lua.c ]] && continue
    stem="${source%.c}"
    objects+=("$work/obj/$stem-$mode.o")
  done
  if ! riscv64-linux-gnu-ld -m elf32lriscv -static -e _start \
      -o "$work/run/luac-$mode.elf" \
      "$work/obj/rv32-linux-picolibc-start.o" \
      "$work/obj/rv32-linux-picolibc-syscalls.o" \
      "$work/obj/rv32-linux-picolibc-posix.o" \
      "$work/obj/rv32-linux-picolibc-errno.o" \
      "${objects[@]}" \
      --start-group "$picomath" "$picolibc" "$baregcc" --end-group \
      >"$work/run/luac-$mode.link.out" 2>"$work/run/luac-$mode.link.err"; then
    echo "RV32_LUAC=FAIL stage=ELF32_link mode=$mode" >&2
    head -n 40 "$work/run/luac-$mode.link.err" >&2
    exit 1
  fi
  riscv64-linux-gnu-readelf -h "$work/run/luac-$mode.elf" |
    grep -q 'Class:.*ELF32'
  set +e
  timeout 75s qemu-riscv32 "$work/run/luac-$mode.elf" \
    -o "$work/run/$mode.luac" "$work/bytecode_pressure.lua" \
    >"$work/run/luac-$mode.out" 2>"$work/run/luac-$mode.err"
  rc=$?
  set -e
  if ((rc != 0)); then
    echo "RV32_LUAC=FAIL stage=QEMU mode=$mode exit=$rc" >&2
    head -n 24 "$work/run/luac-$mode.err" >&2
    exit 1
  fi
  test -s "$work/run/$mode.luac"
  echo "RV32_LUAC_BINARY=PASS mode=$mode bytes=$(wc -c < "$work/run/$mode.luac")"
done
for mode in gnu-as minias; do
  if ! cmp -s "$work/run/reference.luac" "$work/run/$mode.luac"; then
    echo "RV32_LUAC=FAIL stage=bytecode_differential mode=$mode" >&2
    cmp -l "$work/run/reference.luac" "$work/run/$mode.luac" | head -n 12 >&2 || :
    exit 1
  fi
done
# Execute original source AND generated binary bytecode with the genuine
# interpreter for each ABI/assembler route. No same-process injected Lua C API.
for mode in reference gnu-as minias; do
  for form in source bytecode; do
    case "$form" in
      source) input="$work/bytecode_pressure.lua" ;;
      bytecode) input="$work/run/$mode.luac" ;;
    esac
    if ! timeout 75s qemu-riscv32 "$work/run/lua-$mode.elf" "$input" \
        >"$work/run/$mode-$form.out" \
        2>"$work/run/$mode-$form.err"; then
      echo "RV32_LUAC=FAIL stage=bytecode_execution mode=$mode form=$form" >&2
      head -n 20 "$work/run/$mode-$form.err" >&2
      exit 1
    fi
    cmp -s "$work/run/reference-source.out" "$work/run/$mode-$form.out" ||
      [[ "$mode" == reference && "$form" == source ]] || {
        echo "RV32_LUAC=FAIL stage=output_differential mode=$mode form=$form" >&2
        exit 1
      }
  done
done
test -s "$work/run/reference-source.out"
echo 'RV32_LUAC=PASS original_upstream=1 MiniC_all_33_objects=1 GCC_oracle=1 GNU_AS=1 MiniAS=1 ELF32=1 QEMU=1 bytecode=byte_exact source_binary_execution=1'

