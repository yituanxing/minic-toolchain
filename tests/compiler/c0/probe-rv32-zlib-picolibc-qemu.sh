#!/usr/bin/env bash
# Real upstream zlib v1.3.1 15/15 unchanged TUs, RV32I/ILP32.
# Original test/example.c is the actual QEMU program. No compiler
# fallback: all zlib library objects must come from MiniC for either
# MiniAS or GNU AS. GNU C compiles the same source as independent oracle.
set -Eeuo pipefail
: "${MINIC_CC:?}"
: "${MINIAS:=${MINIC_CC%/minic-cc}/minic-as}"
: "${BARE_CC:=riscv64-unknown-elf-gcc}"
: "${RISCV_CC:=riscv64-linux-gnu-gcc}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
root=$(git rev-parse --show-toplevel)
target=(-march=rv32i -mabi=ilp32)
libc=(-specs=picolibc.specs)
# Picolibc's documented errno-function header mode preserves the
# library's genuine thread-local errno storage. The function itself is
# compiled by GCC to the correct ILP32 TLS access, never by MiniC.
# Both MiniC and GCC see the EXACT same preprocessed upstream zlib.
errno_header=(-D__PICOLIBC_ERRNO_FUNCTION=__minic_picolibc_errno)
commit=51b7f2abdade71cd9bb0e7a373ef2610ec6f9daf
sources=(adler32.c compress.c crc32.c deflate.c gzclose.c gzlib.c gzread.c
         gzwrite.c infback.c inffast.c inflate.c inftrees.c trees.c
         uncompr.c zutil.c)
for command in "$BARE_CC" "$RISCV_CC" qemu-riscv32 curl; do
  command -v "$command" >/dev/null
done
curl -fsSL --retry 3 --retry-delay 2 --max-time 90 \
  "https://codeload.github.com/madler/zlib/tar.gz/$commit" -o "$work/zlib.tar.gz"
mkdir "$work/upstream"
tar -xzf "$work/zlib.tar.gz" -C "$work/upstream" --strip-components=1
src="$work/upstream"
for pair in \
  'zlib.h 8d4b932eaf6a0fbb8133b3ab49ba5ef587059fa0' \
  'deflate.c 012ea8148e8d21cf515a9ff47d4af719d049b276' \
  'zconf.h 62adc8d8431f2f9149ae0b1583915e21a28dd8b5' \
  'test/example.c c3521dd5934f716bc7c33fe1c5fd4a361c985e76'; do
  read -r name blob <<<"$pair"
  if [[ $(git hash-object "$src/$name") != "$blob" ]]; then
    echo "RV32_ZLIB=FAIL stage=upstream_integrity file=$name" >&2
    exit 1
  fi
done
# Existing zlib tests already qualify the original ./configure path.
# Configure on the host for feature probes, then preprocess and compile
# every resulting TU using the actual RV32I/ILP32 headers and ABI.
(cd "$src" && CC=cc CFLAGS="-O0 -std=c11" ./configure) >"$work/configure.log" 2>&1
grep -qF "Checking for unistd.h... Yes." "$work/configure.log"
mkdir "$work/obj"
MINIC_CC="$MINIC_CC" bash tests/compiler/c0/check-rv32-opaque-tls-declarations.sh
MINIC_CC="$MINIC_CC" MINIAS="$MINIAS" \
  bash tests/compiler/c0/probe-rv32-variadic-gcc-qemu.sh
MINIC_PASS=0
REJECTS=0
for source in "${sources[@]}"; do
  stem="${source%.c}"
  "$BARE_CC" "${target[@]}" "${libc[@]}" "${errno_header[@]}" \
    -E -P -std=c11 -I"$src" "$src/$source" -o "$work/obj/$stem.i"
  "$BARE_CC" "${target[@]}" "${libc[@]}" -O0 -ffreestanding -fno-builtin \
    -x c -c "$work/obj/$stem.i" -o "$work/obj/$stem-reference.o"
  if ! "$MINIC_CC" --target=riscv32-linux "${target[@]}" -S \
      "$work/obj/$stem.i" -o "$work/obj/$stem.s" \
      >"$work/obj/$stem.minic.out" 2>"$work/obj/$stem.minic.err"; then
    REJECTS=$((REJECTS+1))
    echo "RV32_ZLIB_TU=BLOCKED source=$source stage=MiniC" >&2
    head -n 8 "$work/obj/$stem.minic.err" >&2
    loc=$(sed -n "s/.*$stem\\.i:\\([0-9][0-9]*\\):.*/\\1/p" \
            "$work/obj/$stem.minic.err" | head -n 1)
    if [[ -n "$loc" ]]; then
      first=$((loc > 5 ? loc-5 : 1))
      last=$((loc+5))
      nl -ba "$work/obj/$stem.i" | sed -n "${first},${last}p" >&2
    fi
    continue
  fi
  if ! "$RISCV_CC" "${target[@]}" -x assembler -c \
      "$work/obj/$stem.s" -o "$work/obj/$stem-gnu-as.o" \
      >"$work/obj/$stem.gas.out" 2>"$work/obj/$stem.gas.err"; then
    echo "RV32_ZLIB_TU=FAIL source=$source stage=GNU_AS" >&2
    head -n 12 "$work/obj/$stem.gas.err" >&2
    exit 1
  fi
  if ! "$MINIAS" "${target[@]}" -o "$work/obj/$stem-minias.o" "$work/obj/$stem.s" \
      >"$work/obj/$stem.as.out" 2>"$work/obj/$stem.as.err"; then
    echo "RV32_ZLIB_TU=FAIL source=$source stage=MiniAS" >&2
    head -n 12 "$work/obj/$stem.as.err" >&2
    exit 1
  fi
  MINIC_PASS=$((MINIC_PASS+1))
  echo "RV32_ZLIB_TU=PASS source=$source compiler=MiniC assemblers=GNU_AS,MiniAS"
done
echo "RV32_ZLIB_TU_SUMMARY total=15 compiled=$MINIC_PASS blocked=$REJECTS"
if (( REJECTS != 0 )); then
  echo "RV32_ZLIB=BLOCKED stage=compiler compiled=$MINIC_PASS of=15" >&2
  exit 1
fi
# Original full zlib example, without modifying zlib's own source or tests.
"$BARE_CC" "${target[@]}" "${libc[@]}" -O0 -ffreestanding -fno-builtin \
    -I"$src" -c "$src/test/example.c" -o "$work/obj/example.o"
"$BARE_CC" "${target[@]}" "${libc[@]}" -O0 -ffreestanding -fno-builtin \
    -c "$root/tests/compiler/c0/rv32-linux-picolibc-syscalls.c" \
    -o "$work/obj/linux-syscalls.o"
"$BARE_CC" "${target[@]}" "${libc[@]}" -O0 -ffreestanding -fno-builtin \
    -c "$root/tests/compiler/c0/rv32-linux-picolibc-posix.c" \
    -o "$work/obj/linux-posix.o"
"$BARE_CC" "${target[@]}" "${libc[@]}" -O0 -ffreestanding -fno-builtin \
    -c "$root/tests/compiler/c0/rv32-linux-picolibc-errno.c" \
    -o "$work/obj/linux-errno.o"
"$BARE_CC" "${target[@]}" "${libc[@]}" \
    -c "$root/tests/compiler/c0/rv32-linux-picolibc-start.S" -o "$work/obj/start.o"
picolibc=$(find /usr/lib/picolibc -type f -path '*/rv32i/ilp32/libc.a' -print -quit)
picomath=$(find /usr/lib/picolibc -type f -path '*/rv32i/ilp32/libm.a' -print -quit)
baregcc=$("$BARE_CC" "${target[@]}" -print-libgcc-file-name)
for f in "$picolibc" "$picomath" "$baregcc"; do test -f "$f"; done
mkdir "$work/run"
for mode in reference gnu-as minias; do
  libs=()
  for source in "${sources[@]}"; do
    stem="${source%.c}"
    case "$mode" in
      reference) name="$stem-reference.o" ;;
      gnu-as) name="$stem-gnu-as.o" ;;
      minias) name="$stem-minias.o" ;;
    esac
    libs+=("$work/obj/$name")
  done
  riscv64-linux-gnu-ld -m elf32lriscv -static -e _start \
    -o "$work/run/zlib-$mode.elf" \
    "$work/obj/start.o" "$work/obj/linux-syscalls.o" \
    "$work/obj/linux-posix.o" "$work/obj/linux-errno.o" \
    "$work/obj/example.o" "${libs[@]}" \
    --start-group "$picomath" "$picolibc" "$baregcc" --end-group
  riscv64-linux-gnu-readelf -h "$work/run/zlib-$mode.elf" | grep -q 'Class:.*ELF32'
  set +e
  (cd "$work/run" && timeout 75s qemu-riscv32 "./zlib-$mode.elf") \
    >"$work/run/$mode.out" 2>"$work/run/$mode.err"
  rc=$?
  set -e
  if (( rc != 0 )); then
    echo "RV32_ZLIB=FAIL stage=QEMU mode=$mode exit=$rc" >&2
    head -n 30 "$work/run/$mode.err" >&2 || :
    echo "RV32_ZLIB=DIAG mode=$mode original_upstream_elf32=1" >&2
    (cd "$work/run" && timeout 10s qemu-riscv32 -strace "./zlib-$mode.elf") \
      2>&1 | tail -n 45 >&2 || :
    echo "--- GNU symbols near entry and TLS" >&2
    riscv64-linux-gnu-nm "$work/run/zlib-$mode.elf" |
      grep -E ' (main|_start|errno|__minic_picolibc_errno|__libc_init_array)$' >&2 || :
    exit 1
  fi
  echo "RV32_ZLIB_EXAMPLE=PASS mode=$mode original_upstream=1 QEMU=1"
done
test -s "$work/run/reference.out"
for mode in gnu-as minias; do
  cmp -s "$work/run/reference.out" "$work/run/$mode.out" || {
    echo "RV32_ZLIB=FAIL stage=stdout_diff mode=$mode" >&2
    diff -u "$work/run/reference.out" "$work/run/$mode.out" | head -n 40 >&2 || :
    exit 1
  }
  cmp -s "$work/run/reference.err" "$work/run/$mode.err" || {
    echo "RV32_ZLIB=FAIL stage=stderr_diff mode=$mode" >&2
    exit 1
  }
done
echo 'RV32_ZLIB=PASS original_upstream=1 TUs=15 MiniC_only=1 GNU_C_oracle=1 GNU_AS=1 MiniAS=1 Picolibc=1 ELF32=1 QEMU=1 example=byte_exact'

# Second untouched upstream executable: 15 MiniC-built zlib TUs are reused
# with original test/minigzip.c, compiled independently by GCC and MiniC.
minigzip="$src/test/minigzip.c"
test "$(git hash-object "$minigzip")" = 134e10e6c3e947a8e54501a1a256fb62c537cb87
"$BARE_CC" "${target[@]}" "${libc[@]}" -E -P -std=c11 \
    -I"$src" "$minigzip" -o "$work/obj/minigzip.i"
"$BARE_CC" "${target[@]}" "${libc[@]}" -O0 -ffreestanding -fno-builtin \
    -x c -c "$work/obj/minigzip.i" -o "$work/obj/minigzip-reference.o"
if ! "$MINIC_CC" --target=riscv32-linux "${target[@]}" -S \
    "$work/obj/minigzip.i" -o "$work/obj/minigzip.s" \
    >"$work/obj/minigzip.minic.out" 2>"$work/obj/minigzip.minic.err"; then
  echo 'RV32_MINIGZIP=BLOCKED stage=MiniC' >&2
  head -n 24 "$work/obj/minigzip.minic.err" >&2
  exit 1
fi
"$RISCV_CC" "${target[@]}" -c "$work/obj/minigzip.s" \
    -o "$work/obj/minigzip-gnu-as.o"
"$MINIAS" "${target[@]}" -o "$work/obj/minigzip-minias.o" \
    "$work/obj/minigzip.s"
echo 'RV32_MINIGZIP_COMPILE=PASS upstream_original=1 GCC=1 MiniC=1 GNU_AS=1 MiniAS=1'
# A deterministic binary payload forces real gzip file operations,
# not just help/version output or a fake compression test.
python3 - "$work/minigzip-input.bin" <<'PY'
import pathlib, sys
pathlib.Path(sys.argv[1]).write_bytes(
    bytes((i * 73 + (i >> 5) * 31) & 255 for i in range(32768))
)
PY
for mode in reference gnu-as minias; do
  libs=()
  for source in "${sources[@]}"; do
    stem="${source%.c}"
    case "$mode" in
      reference) name="$stem-reference.o" ;;
      gnu-as) name="$stem-gnu-as.o" ;;
      minias) name="$stem-minias.o" ;;
    esac
    libs+=("$work/obj/$name")
  done
  riscv64-linux-gnu-ld -m elf32lriscv -static -e _start \
    -o "$work/run/minigzip-$mode.elf" \
    "$work/obj/start.o" "$work/obj/linux-syscalls.o" \
    "$work/obj/linux-posix.o" "$work/obj/linux-errno.o" \
    "$work/obj/minigzip-$mode.o" "${libs[@]}" \
    --start-group "$picomath" "$picolibc" "$baregcc" --end-group
  mkdir -p "$work/run/$mode"
  cp "$work/minigzip-input.bin" "$work/run/$mode/input.bin"
  (cd "$work/run/$mode" &&
    timeout 60s qemu-riscv32 "../minigzip-$mode.elf" input.bin)
  test ! -e "$work/run/$mode/input.bin"
  test -s "$work/run/$mode/input.bin.gz"
  cp "$work/run/$mode/input.bin.gz" "$work/minigzip-$mode.gz"
  (cd "$work/run/$mode" &&
    timeout 60s qemu-riscv32 "../minigzip-$mode.elf" -d input.bin.gz)
  cmp -s "$work/minigzip-input.bin" "$work/run/$mode/input.bin"
  test ! -e "$work/run/$mode/input.bin.gz"
  echo "RV32_MINIGZIP_MODE=PASS mode=$mode encode=1 decode=1 sha256=$(sha256sum "$work/run/$mode/input.bin" | cut -c1-20)"
done
cmp -s "$work/minigzip-reference.gz" "$work/minigzip-gnu-as.gz"
cmp -s "$work/minigzip-reference.gz" "$work/minigzip-minias.gz"
echo 'RV32_MINIGZIP=PASS original_upstream=1 unchanged_zlib_TUs=15 full_MiniC=1 GCC_oracle=1 GNU_AS=1 MiniAS=1 ELF32=1 QEMU=1 gzip_compress=1 gzip_decompress=1 gzip_bytes_equal=1 original_bytes_restored=1'

