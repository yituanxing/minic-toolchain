#!/usr/bin/env bash
# Original pinned BusyBox source and independent four-ISA musl static ABI gate.
set -Eeuo pipefail
target="$1"; cc="$2"; prefix="$3"; qemu="$4"
: "${MINIC_CC:?set MINIC_CC}"
repo=$(cd "$(dirname "$0")/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
case "$target" in
 x86_64) incl=/usr/include/x86_64-linux-gnu; triple=x86_64-linux-musl ;;
 riscv64) incl=/usr/riscv64-linux-gnu/include; triple=riscv64-linux-musl ;;
 aarch64) incl=/usr/aarch64-linux-gnu/include; triple=aarch64-linux-musl ;;
 loongarch64) incl=/usr/loongarch64-linux-gnu/include; triple=loongarch64-linux-musl ;;
 *) echo "MUSL_MATRIX=FAIL invalid_target=$target" >&2; exit 2 ;;
esac
mkdir -p "$work/build" "$work/sysroot/usr/include" "$work/busybox"
curl -fsSL --retry 4 --retry-delay 2 --connect-timeout 15 \
  https://musl.libc.org/releases/musl-1.2.5.tar.gz -o "$work/musl.tgz"
printf '%s  %s\n' \
  a9a118bbe84d8764da0ea0d28b3ab3fae8477fc7e4085d90102b8596fc7c75e4 \
  "$work/musl.tgz" | sha256sum -c --status
tar -xzf "$work/musl.tgz" -C "$work"
for d in linux asm asm-generic mtd; do
  if test -d "$incl/$d"; then cp -a "$incl/$d" "$work/sysroot/usr/include/"; fi
done
if test "$target" = x86_64; then
  for d in linux asm-generic mtd; do cp -a "/usr/include/$d" "$work/sysroot/usr/include/"; done
fi
(
  cd "$work/build"
  CC="$cc" AR="${prefix}ar" RANLIB="${prefix}ranlib" CFLAGS="-O2 -fno-pic -fno-pie" \
    "$work/musl-1.2.5/configure" --target="$triple" --disable-shared --prefix=/usr \
    >"$work/configure.log" 2>&1 || {
      echo "MUSL_MATRIX=FAIL target=$target stage=configure" >&2
      tail -n 35 "$work/configure.log" >&2
      exit 1
    }
  make -j4 lib/libc.a obj/crt/crt1.o obj/crt/crti.o obj/crt/crtn.o \
    >"$work/build.log" 2>&1 || {
      echo "MUSL_MATRIX=FAIL target=$target stage=build" >&2
      tail -n 35 "$work/build.log" >&2
      exit 1
    }
  make DESTDIR="$work/sysroot" install-headers >"$work/headers.log" 2>&1
)
test -s "$work/build/lib/libc.a"
test -s "$work/sysroot/usr/include/stdio.h"
builtin=$("$cc" -print-file-name=include)
libgcc=$("$cc" -print-libgcc-file-name)
headers=(-nostdinc -isystem "$work/sysroot/usr/include" -isystem "$builtin" \
  -I"$repo/tests/linker/fixtures/busybox-uapi")
cat >"$work/consumer.c" <<'SRC'
#include <stdio.h>
#include <stdlib.h>
int main(void) {
  puts("pinned-musl-1.2.5");
  return sizeof(size_t) == 8 ? 42 : 1;
}
SRC
"$cc" "${headers[@]}" -O0 -E -P "$work/consumer.c" -o "$work/consumer.i"
"$cc" "${headers[@]}" -O0 -fno-pic -fno-pie -c "$work/consumer.c" -o "$work/gcc.o"
"$MINIC_CC" "--target=$target-linux" -S "$work/consumer.i" -o "$work/mini.s"
"$cc" -c -x assembler "$work/mini.s" -o "$work/mini.o"
for impl in gcc mini; do
  "$cc" -nostdlib -static -no-pie -Wl,-e,_start \
    "$work/build/obj/crt/crt1.o" "$work/build/obj/crt/crti.o" "$work/$impl.o" \
    -Wl,--start-group "$work/build/lib/libc.a" "$libgcc" -Wl,--end-group \
    "$work/build/obj/crt/crtn.o" -o "$work/$impl"
done
set +e
"$qemu" "$work/gcc" >"$work/gcc.out" 2>"$work/gcc.err"; gcc_rc=$?
"$qemu" "$work/mini" >"$work/mini.out" 2>"$work/mini.err"; mini_rc=$?
set -e
if test "$gcc_rc" != 42 || test "$mini_rc" != 42 ||
   ! cmp -s "$work/gcc.out" "$work/mini.out" ||
   ! cmp -s "$work/gcc.err" "$work/mini.err"; then
  echo "MUSL_MATRIX=FAIL target=$target stage=GNU_MiniC_QEMU gcc=$gcc_rc minic=$mini_rc" >&2
  head -c 1200 "$work/gcc.err" "$work/mini.err" >&2 || true
  exit 1
fi
echo "MUSL_MATRIX_ABI=PASS target=$target libc=musl-1.2.5 CRT=musl static=1 GCC_MiniC_QEMU_byte_exact=1"

# The GNU returns_twice attribute in musl's setjmp.h must not silently
# collapse a longjmp re-entry into a single-return CFG optimization.
cat >"$work/returns-twice.c" <<'SRC'
#include <setjmp.h>
#include <stdio.h>
static jmp_buf checkpoint;
static volatile int entered;
int main(void) {
  int rc = setjmp(checkpoint);
  if (rc == 0) {
    entered = 1;
    longjmp(checkpoint, 42);
  }
  printf("musl-longjmp:%d:%d\n", rc, entered);
  return rc == 42 && entered == 1 ? 42 : 1;
}
SRC
"$cc" "${headers[@]}" -O0 -E -P "$work/returns-twice.c" -o "$work/setjmp.i"
"$cc" "${headers[@]}" -O0 -fno-pic -fno-pie -c "$work/returns-twice.c" -o "$work/setjmp-gnu.o"
"$MINIC_CC" "--target=$target-linux" -S "$work/setjmp.i" -o "$work/setjmp-mini.s"
"$cc" -c -x assembler "$work/setjmp-mini.s" -o "$work/setjmp-mini.o"
for impl in gnu mini; do
  "$cc" -nostdlib -static -no-pie -Wl,-e,_start \
    "$work/build/obj/crt/crt1.o" "$work/build/obj/crt/crti.o" "$work/setjmp-$impl.o" \
    -Wl,--start-group "$work/build/lib/libc.a" "$libgcc" -Wl,--end-group \
    "$work/build/obj/crt/crtn.o" -o "$work/setjmp-$impl"
done
set +e
"$qemu" "$work/setjmp-gnu" >"$work/setjmp-gnu.out" 2>"$work/setjmp-gnu.err"; gnu_rc=$?
"$qemu" "$work/setjmp-mini" >"$work/setjmp-mini.out" 2>"$work/setjmp-mini.err"; mini_rc=$?
set -e
if test "$gnu_rc" != 42 || test "$mini_rc" != 42 ||
   ! cmp -s "$work/setjmp-gnu.out" "$work/setjmp-mini.out" ||
   ! cmp -s "$work/setjmp-gnu.err" "$work/setjmp-mini.err"; then
  echo "MUSL_RETURNS_TWICE=FAIL target=$target gcc=$gnu_rc minic=$mini_rc" >&2
  head -c 800 "$work/setjmp-gnu.err" "$work/setjmp-mini.err" >&2 || true
  exit 1
fi
echo "MUSL_RETURNS_TWICE=PASS target=$target original_musl_setjmp_header=1 GNU_MiniC_QEMU_byte_exact=1"
# Original musl time.h has unnamed zero-width bitfields in struct timespec.
# Test this real ABI/layout class before all 581 original BusyBox C owners.
cat >"$work/bitfield.c" <<'SRC'
#include <time.h>
#include <stdio.h>
static int value(void) {
    static const struct timespec ts = { 0, 32 * 1000 * 1000 };
    return ts.tv_sec == 0 && ts.tv_nsec == 32000000;
}
int main(void) {
    puts(value() ? "timespec-ok" : "timespec-fail");
    return value() ? 42 : 1;
}
SRC
"$cc" "${headers[@]}" -O0 -E -P "$work/bitfield.c" -o "$work/bitfield.i"
"$cc" "${headers[@]}" -O0 -fno-pic -fno-pie -c "$work/bitfield.c" -o "$work/bitfield-gnu.o"
"$MINIC_CC" "--target=$target-linux" -S "$work/bitfield.i" -o "$work/bitfield-mini.s"
"$cc" -c -x assembler "$work/bitfield-mini.s" -o "$work/bitfield-mini.o"
for impl in gnu mini; do
  "$cc" -nostdlib -static -no-pie -Wl,-e,_start \
    "$work/build/obj/crt/crt1.o" "$work/build/obj/crt/crti.o" "$work/bitfield-$impl.o" \
    -Wl,--start-group "$work/build/lib/libc.a" "$libgcc" -Wl,--end-group \
    "$work/build/obj/crt/crtn.o" -o "$work/bitfield-$impl"
done
set +e
"$qemu" "$work/bitfield-gnu" >"$work/bitfield-gnu.out" 2>"$work/bitfield-gnu.err"; gnu_rc=$?
"$qemu" "$work/bitfield-mini" >"$work/bitfield-mini.out" 2>"$work/bitfield-mini.err"; mini_rc=$?
set -e
if test "$gnu_rc" != 42 || test "$mini_rc" != 42 ||
   ! cmp -s "$work/bitfield-gnu.out" "$work/bitfield-mini.out" ||
   ! cmp -s "$work/bitfield-gnu.err" "$work/bitfield-mini.err"; then
  echo "MUSL_TIMESPEC_BITFIELD=FAIL target=$target gcc=$gnu_rc minic=$mini_rc" >&2
  exit 1
fi
echo "MUSL_TIMESPEC_BITFIELD=PASS target=$target original_musl_timeh=1 GNU_MiniC_QEMU_byte_exact=1"


commit=1a64f6a20aaf6ea4dbba68bbfa8cc1ab7e5c57c4
curl -fsSL --retry 4 --retry-delay 2 --connect-timeout 15 \
  "https://github.com/mirror/busybox/archive/$commit.tar.gz" \
  -o "$work/busybox.tgz"
tar -xzf "$work/busybox.tgz" --strip-components=1 -C "$work/busybox"
sh "$repo/tests/external/busybox/check-pinned-1361-source.sh" "$work/busybox"
echo "MUSL_MATRIX_SOURCE=PASS target=$target version=BusyBox-1.36.1 commit=$commit"

# GNU full Kbuild and MiniC all-owner substitution use this SINGLE prepared
# musl toolchain and pristine pinned 1.36.1 sources. Counts are target-specific
# and must not be borrowed from the known glibc 593/581 successes.
export MINIC_MUSL_GCC="$cc" MINIC_MUSL_SYSROOT="$work/sysroot"
export MINIC_MUSL_BUILTIN="$builtin" MINIC_MUSL_BUILD="$work/build"
export MINIC_MUSL_UAPI="$repo/tests/linker/fixtures/busybox-uapi"
export MINIC_MUSL_LIBGCC="$libgcc"
report="$work/report"; mkdir -p "$report"
set +e
timeout 1200s env CROSS_COMPILE="$prefix" PKG_CONFIG=pkg-config \
  BUSYBOX_ALLYES_LINK_COMPATIBLE=1 BUSYBOX_ALLYES_MIXED_BULK=1 \
  BUSYBOX_ALLYES_MUSL_MATRIX=1 BUSYBOX_ALLYES_MINIC_PIC=0 \
  BUSYBOX_ALLYES_MINIC_TARGET="$target-linux" \
  BUSYBOX_ALLYES_CROSS_PREFIX="$prefix" BUSYBOX_ALLYES_QEMU="$qemu" \
  BUSYBOX_ALLYES_SYSROOT="$work/sysroot" \
  python3 "$repo/tests/external/busybox/allyes_sweep.py" \
    --source "$work/busybox" --report "$report" --minic "$MINIC_CC" \
    --host-cc "python3 $repo/tests/external/busybox/musl-gcc-oracle.py" \
  >"$work/kbuild.out" 2>"$work/kbuild.err"
rc=$?
set -e
grep -E '^(BUSYBOX_ALLYES_(CONFIG=|HOST_EXCLUSIONS=|GCC_KBUILD=|GCC_REAL_ERRORS |FRONTIER=|BLOCKER )|BUSYBOX_MUSL_FULL_ACCEPT=|BUSYBOX_MIXED_BULK(=|_CROSS_RUNTIME=|_CROSS_COHORT=|_REAL_OWNERS=|_RUNTIME=))' "$work/kbuild.out" | head -n 110 || true
if test "$rc" != 0; then
  echo "BUSYBOX_MUSL_ACCEPT=FAIL target=$target source=BusyBox1.36.1 stage=real_Kbuild_and_MiniC_cohort rc=$rc" >&2
  tail -n 28 "$work/kbuild.out" "$work/kbuild.err" >&2
  exit 1
fi
grep -q '^BUSYBOX_MUSL_FULL_ACCEPT=PASS .*GCC_fallbacks=0 .*QEMU_differentials=38' "$work/kbuild.out" || {
  echo "BUSYBOX_MUSL_ACCEPT=FAIL target=$target stage=missing_real_all_C_owner_SHA_QEMU_receipt" >&2
  exit 1
}
echo "BUSYBOX_MUSL_ACCEPT=PASS target=$target BusyBox=1.36.1 libc=musl1.2.5 all_real_C_owners_MiniC=YES GNU_C_fallback=0 GNU_asm_link=1 QEMU_differential=38"
