#!/usr/bin/env bash
# Official Zstandard 1.5.7 portable static libzstd, original C TUs:
# common + compress + decompress; upstream-supported no-thread/no-legacy
# configuration. Real RV32I/ILP32 GNU C reference vs MiniC with GNU AS
# and MiniAS, authentic Picolibc and Linux QEMU execution.
# NO skipped owner, rewritten vendor C, GNU C fallback or fake libc.
set -Eeuo pipefail
: "${MINIC_CC:?set MINIC_CC}"
: "${MINIAS:=${MINIC_CC%/minic-cc}/minic-as}"
: "${BARE_CC:=riscv64-unknown-elf-gcc}"
: "${RISCV_CC:=riscv64-linux-gnu-gcc}"
root=$(git rev-parse --show-toplevel)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
release=1.5.7
expected=eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3
for tool in curl sha256sum tar "$BARE_CC" "$RISCV_CC" qemu-riscv32 riscv64-linux-gnu-ld; do
  command -v "$tool" >/dev/null
done
curl -fsSL --retry 3 --retry-delay 2 --max-time 120 \
  "https://github.com/facebook/zstd/releases/download/v$release/zstd-$release.tar.gz" \
  -o "$work/zstd.tar.gz"
printf '%s  %s\n' "$expected" "$work/zstd.tar.gz" | sha256sum -c - >/dev/null
tar -xzf "$work/zstd.tar.gz" -C "$work"
src="$work/zstd-$release"
test -s "$src/lib/zstd.h" && test -s "$src/lib/compress/zstd_compress.c"
echo "RV32_ZSTD_SOURCE=VERIFIED release=$release sha256=$expected"
target=(-march=rv32i -mabi=ilp32)
libc=(-specs=picolibc.specs)
# Both compilers receive the same official portable configuration and
# precisely the same GNU-preprocessed input. No vendor source modifications.
# Static libzstd uses absence of ZSTD_MULTITHREAD to disable pthread code.
# Some upstream sources test #ifdef, not the numeric macro value.
defs=(-DZSTD_LEGACY_SUPPORT=0 -DZSTD_DISABLE_ASM)
includes=(-I"$src/lib" -I"$src/lib/common" -I"$src/lib/compress" -I"$src/lib/decompress")
predefines=(-D__PICOLIBC_ERRNO_FUNCTION=__minic_picolibc_errno)
mapfile -d '' sources < <(find "$src/lib/common" "$src/lib/compress" \
  "$src/lib/decompress" -maxdepth 1 -type f -name '*.c' -print0 | LC_ALL=C sort -z)
if ((${#sources[@]} < 15)); then
  echo "RV32_ZSTD=FAIL stage=source_census TUs=${#sources[@]}" >&2
  exit 1
fi
mkdir -p "$work/obj" "$work/run"
blocked=0
for source in "${sources[@]}"; do
  name=${source#"$src/lib/"}
  stem=${name//\//_}
  stem=${stem%.c}
  "$BARE_CC" "${target[@]}" "${libc[@]}" "${defs[@]}" \
    "${predefines[@]}" "${includes[@]}" -E -P -std=gnu11 \
    "$source" -o "$work/obj/$stem.i"
  if ! "$BARE_CC" "${target[@]}" "${libc[@]}" -O0 -ffreestanding \
      -fno-builtin -x c -c "$work/obj/$stem.i" -o "$work/obj/$stem-reference.o" \
      >"$work/obj/$stem.gcc.out" 2>"$work/obj/$stem.gcc.err"; then
    echo "RV32_ZSTD=FAIL stage=GNU_C_reference source=$name" >&2
    head -n 25 "$work/obj/$stem.gcc.err" >&2
    exit 1
  fi
  # Keep noisy Core instrumentation scoped to the sole unresolved
  # original source owner, not the 25 already accepted C translation units.
  trace_env=()
  if [ "$name" = "compress/zstdmt_compress.c" ]; then
    trace_env=(CORE_FAST_TRACE=1 CORE_M138_ERROR_TRACE=1 CORE_M136_STATEMENT_TRACE=1 MINIC_CORE_VERIFY_TRACE=1)
  fi
  if ! env MINIC_RV32_FRAME_TRACE=1 "${trace_env[@]}" \
      "$MINIC_CC" --target=riscv32-linux "${target[@]}" -S \
      "$work/obj/$stem.i" -o "$work/obj/$stem.s" \
      >"$work/obj/$stem.minic.out" 2>"$work/obj/$stem.minic.err"; then
    echo "RV32_ZSTD_TU=BLOCKED stage=MiniC source=$name" >&2
    head -n 25 "$work/obj/$stem.minic.err" >&2
    if [ "${#trace_env[@]}" -gt 0 ]; then
      echo "RV32_ZSTD_CORE_FAILURE_OWNER source=$name"
      grep -E 'CORE_FAST_TRACE|CORE_M13[6789]_|CORE_EXPR_STMT|CORE_ASM_DETAIL|CORE_LOWER_|CORE_IR_VERIFY_' \
        "$work/obj/$stem.minic.err" | tail -n 100 || true
    fi
    blocked=$((blocked + 1))
    continue
  fi
  if ! "$RISCV_CC" "${target[@]}" -c "$work/obj/$stem.s" \
      -o "$work/obj/$stem-gnu-as.o" \
      >"$work/obj/$stem.as.out" 2>"$work/obj/$stem.as.err"; then
    echo "RV32_ZSTD_TU=BLOCKED stage=GNU_AS source=$name" >&2
    head -n 25 "$work/obj/$stem.as.err" >&2
    blocked=$((blocked + 1))
    continue
  fi
  if ! "$MINIAS" "${target[@]}" -o "$work/obj/$stem-minias.o" \
      "$work/obj/$stem.s" >"$work/obj/$stem.mini-as.out" \
      2>"$work/obj/$stem.mini-as.err"; then
    echo "RV32_ZSTD_TU=BLOCKED stage=MiniAS source=$name" >&2
    head -n 25 "$work/obj/$stem.mini-as.err" >&2
    blocked=$((blocked + 1))
    continue
  fi
  echo "RV32_ZSTD_TU=PASS source=$name GCC=1 MiniC=1 GNU_AS=1 MiniAS=1"
done
echo "RV32_ZSTD_CENSUS total=${#sources[@]} blocked=$blocked"
if ((blocked != 0)); then
  echo "RV32_ZSTD=BLOCKED genuine_original_TUs=${#sources[@]} blocked=$blocked" >&2
  exit 1
fi
echo "RV32_ZSTD_CENSUS=PASS originals=${#sources[@]} GNU_C_fallback=0"
# Harness uses only the genuine public libzstd API. Repeated inputs include
# empty/small/large, repeating and high-entropy patterns. Byte-for-byte
# stdout contains each complete compressed frame, not just a weak checksum.
cat >"$work/roundtrip.c" <<'SRC'
#include "zstd.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#define MAX_INPUT 131072U
#define MAX_PACKED 262144U
static unsigned char raw[MAX_INPUT];
static unsigned char unpacked[MAX_INPUT + 1U];
static unsigned char frame[MAX_PACKED];
static const unsigned sizes[] = {0U, 1U, 17U, 4096U, 32768U, 131072U};
static int emit_all(const unsigned char *bytes, size_t n) {
    while (n != 0U) {
        ssize_t wrote = write(1, bytes, n);
        if (wrote <= 0) return 0;
        bytes += (size_t)wrote;
        n -= (size_t)wrote;
    }
    return 1;
}
int main(void) {
    unsigned i, mode;
    unsigned cases = 0U;
    for (i = 0U; i < sizeof(sizes)/sizeof(sizes[0]); ++i) {
        size_t n = sizes[i];
        for (mode = 0U; mode < 3U; ++mode) {
            unsigned state = 0x10203040U + i * 17U;
            size_t j, packed_size, restored;
            int level = mode == 0U ? 1 : (mode == 1U ? 3 : 9);
            for (j = 0U; j < n; ++j) {
                state = state * 1664525U + 1013904223U;
                raw[j] = mode == 0U ? (unsigned char)('A' + j % 3U) :
                         mode == 1U ? (unsigned char)(j & 255U) :
                                      (unsigned char)(state >> 24);
            }
            packed_size = ZSTD_compress(frame, sizeof(frame), raw, n, level);
            if (ZSTD_isError(packed_size)) return 10;
            restored = ZSTD_decompress(unpacked, sizeof(unpacked), frame,
                                      packed_size);
            if (ZSTD_isError(restored) || restored != n ||
                memcmp(raw, unpacked, n) != 0) return 11;
            if (printf("RV32_ZSTD_CASE input=%lu packed=%lu level=%d\n",
                       (unsigned long)n, (unsigned long)packed_size, level) < 0 ||
                fflush(stdout) != 0) return 12;
            if (!emit_all(frame, packed_size)) return 13;
            ++cases;
        }
    }
    if (printf("RV32_ZSTD_ROUNDTRIP=PASS cases=%u version=%u\n",
               cases, ZSTD_versionNumber()) < 0 || fflush(stdout) != 0)
        return 14;
    return cases == 18U && ZSTD_versionNumber() == 10507U ? 0 : 15;
}
SRC
"$BARE_CC" "${target[@]}" "${libc[@]}" "${defs[@]}" \
  "${predefines[@]}" "${includes[@]}" -O0 -ffreestanding -fno-builtin \
  -c "$work/roundtrip.c" -o "$work/obj/roundtrip.o"
for name in rv32-linux-picolibc-start.S rv32-linux-picolibc-syscalls.c \
            rv32-linux-picolibc-posix.c rv32-linux-picolibc-errno.c; do
  "$BARE_CC" "${target[@]}" "${libc[@]}" -O0 -ffreestanding \
    -c "$root/tests/compiler/c0/$name" -o "$work/obj/${name%.*}.o"
done
picolibc=$(find /usr/lib/picolibc -type f -path '*/rv32i/ilp32/libc.a' -print -quit)
picomath=$(find /usr/lib/picolibc -type f -path '*/rv32i/ilp32/libm.a' -print -quit)
baregcc=$("$BARE_CC" "${target[@]}" -print-libgcc-file-name)
for path in "$picolibc" "$picomath" "$baregcc"; do test -f "$path"; done
for mode in reference gnu-as minias; do
  objects=()
  for source in "${sources[@]}"; do
    stem=${source#"$src/lib/"}
    stem=${stem//\//_}
    stem=${stem%.c}
    objects+=("$work/obj/$stem-$mode.o")
  done
  if ! riscv64-linux-gnu-ld -m elf32lriscv -static -e _start \
    -o "$work/run/zstd-$mode.elf" \
    "$work/obj/rv32-linux-picolibc-start.o" \
    "$work/obj/rv32-linux-picolibc-syscalls.o" \
    "$work/obj/rv32-linux-picolibc-posix.o" \
    "$work/obj/rv32-linux-picolibc-errno.o" \
    "$work/obj/roundtrip.o" "${objects[@]}" \
    --start-group "$picomath" "$picolibc" "$baregcc" --end-group \
    >"$work/run/$mode.link.out" 2>"$work/run/$mode.link.err"; then
    echo "RV32_ZSTD=FAIL stage=GNU_LD mode=$mode" >&2
    head -n 40 "$work/run/$mode.link.err" >&2
    exit 1
  fi
  riscv64-linux-gnu-readelf -h "$work/run/zstd-$mode.elf" |
    grep -q 'Class:.*ELF32'
  if ! timeout 120s qemu-riscv32 "$work/run/zstd-$mode.elf" \
      >"$work/run/$mode.out" 2>"$work/run/$mode.err"; then
    echo "RV32_ZSTD=FAIL stage=QEMU mode=$mode" >&2
    head -n 25 "$work/run/$mode.err" >&2
    exit 1
  fi
  grep -a -q 'RV32_ZSTD_ROUNDTRIP=PASS cases=18 version=10507' \
    "$work/run/$mode.out" || {
      echo "RV32_ZSTD=FAIL stage=roundtrip_receipt mode=$mode" >&2
      exit 1
    }
  echo "RV32_ZSTD_MODE=PASS mode=$mode original_C=1 QEMU=1 roundtrip=18"
done
for mode in gnu-as minias; do
  if ! cmp -s "$work/run/reference.out" "$work/run/$mode.out" ||
     ! cmp -s "$work/run/reference.err" "$work/run/$mode.err"; then
    echo "RV32_ZSTD=FAIL stage=GNU_MiniC_QEMU_binary_diff mode=$mode" >&2
    exit 1
  fi
done
echo "RV32_ZSTD=PASS original_1_5_7=1 TUs=${#sources[@]} MiniC_all_C=1 GNU_C_reference=1 GNU_AS=1 MiniAS=1 Picolibc=1 ELF32=1 QEMU=1 roundtrip_cases=18 full_frames_byte_exact=1"
