#!/usr/bin/env bash
# DISCOVERY, not a BusyBox acceptance certificate.
# Genuine native Linux musl 1.2.5 / RV32 ILP32 sysroot and pristine
# BusyBox 1.36.1 Kconfig/Kbuild, no GCC C fallback or simulated syscall libc.
# Only graduate after original GNU reference links AND MiniC all-owners
# replace through the existing BusyBox Kbuild/sha/QEMU oracle.
set -Eeuo pipefail
: "${MINIC_CC:?set MINIC_CC}"
: "${BARE_CC:=riscv64-unknown-elf-gcc}"
: "${GNU_LD:=riscv64-linux-gnu-ld}"
: "${QEMU_RV32:=qemu-riscv32}"
repo=$(git rev-parse --show-toplevel)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
for tool in "$BARE_CC" "$GNU_LD" "$QEMU_RV32" curl tar sha256sum make git; do
  command -v "$tool" >/dev/null
done
# Note: this is NOT glibc, and is NOT the older Picolibc Linux shim. It
# uses musl's genuine riscv32 Linux syscall and crt code from upstream.
# musl's lock/atomic runtime may legitimately use A while MiniC's C
# producer remains strict RV32I. Both share ILP32 soft-float ABI.
url=https://musl.libc.org/releases/musl-1.2.5.tar.gz
hash=a9a118bbe84d8764da0ea0d28b3ab3fae8477fc7e4085d90102b8596fc7c75e4
curl -fsSL --retry 3 --max-time 120 "$url" -o "$work/musl.tgz"
printf '%s  %s\n' "$hash" "$work/musl.tgz" | sha256sum -c --status
tar -xzf "$work/musl.tgz" -C "$work"
test -f "$work/musl-1.2.5/arch/riscv32/syscall_arch.h"
echo "RV32_MUSL_SOURCE=VERIFIED version=1.2.5 sha256=$hash target=riscv32-linux-musl"
sysroot="$work/sysroot"
mkdir -p "$work/build" "$sysroot/usr/include"
# Genuine Linux kernel UAPI headers only; do NOT mix host LP64 glibc's
# bits/, sys/, stdio.h or other runtime headers into RV32 musl.
uapi=/usr/riscv64-linux-gnu/include
for name in linux asm asm-generic mtd; do
  if [ -d "$uapi/$name" ]; then
    cp -a "$uapi/$name" "$sysroot/usr/include/"
  fi
done
if [ ! -f "$sysroot/usr/include/linux/types.h" ]; then
  echo "RV32_MUSL=BLOCKED stage=target_Linux_UAPI missing=$uapi/linux/types.h" >&2
  exit 1
fi
(
  cd "$work/build"
  CC="$BARE_CC" AR=riscv64-unknown-elf-ar RANLIB=riscv64-unknown-elf-ranlib \
    CFLAGS="-O2 -march=rv32ia -mabi=ilp32 -fno-pic -fno-pie" \
    "$work/musl-1.2.5/configure" \
      --target=riscv32-linux-musl --disable-shared --prefix=/usr \
    >"$work/musl-configure.log" 2>&1 || {
      echo "RV32_MUSL=BLOCKED stage=configure" >&2
      tail -n 35 "$work/musl-configure.log" >&2
      exit 1
    }
  make -j4 lib/libc.a obj/crt/crt1.o obj/crt/crti.o obj/crt/crtn.o \
    >"$work/musl-build.log" 2>&1 || {
      echo "RV32_MUSL=BLOCKED stage=build" >&2
      tail -n 35 "$work/musl-build.log" >&2
      exit 1
    }
  make DESTDIR="$sysroot" install-headers >"$work/musl-headers.log" 2>&1
)
test -s "$work/build/lib/libc.a"
test -s "$sysroot/usr/include/stdio.h"
target=(-march=rv32i -mabi=ilp32)
builtins=$("$BARE_CC" "${target[@]}" -print-file-name=include)
libgcc=$("$BARE_CC" "${target[@]}" -print-libgcc-file-name)
test -f "$libgcc"
headers=(-nostdinc -isystem "$sysroot/usr/include" -isystem "$builtins")
cat >"$work/abi.c" <<'SRC'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
    const char *text = "native-rv32-musl-1.2.5";
    if (sizeof(void*) != 4 || sizeof(long) != 4 ||
        strlen(text) != 22U) return 30;
    puts(text);
    return 17;
}
SRC
"$BARE_CC" "${target[@]}" "${headers[@]}" -O0 -ffreestanding \
    -fno-builtin -E -P "$work/abi.c" -o "$work/abi.i"
"$BARE_CC" "${target[@]}" "${headers[@]}" -O0 -ffreestanding \
    -fno-builtin -c "$work/abi.c" -o "$work/gcc.o"
"$MINIC_CC" --target=riscv32-linux "${target[@]}" -S \
    "$work/abi.i" -o "$work/mini.s"
"$BARE_CC" "${target[@]}" -c "$work/mini.s" -o "$work/mini.o"
for mode in gcc mini; do
  "$GNU_LD" -m elf32lriscv -static -e _start \
      "$work/build/obj/crt/crt1.o" "$work/build/obj/crt/crti.o" \
      "$work/$mode.o" --start-group "$work/build/lib/libc.a" \
      "$libgcc" --end-group "$work/build/obj/crt/crtn.o" \
      -o "$work/$mode.elf" \
      >"$work/$mode.link.log" 2>&1 || {
        echo "RV32_MUSL=BLOCKED stage=GNU_LD mode=$mode" >&2
        head -n 45 "$work/$mode.link.log" >&2
        exit 1
      }
  set +e
  timeout 45s "$QEMU_RV32" "$work/$mode.elf" \
      >"$work/$mode.out" 2>"$work/$mode.err"
  rc=$?
  set -e
  if [ "$rc" -ne 17 ]; then
    echo "RV32_MUSL=BLOCKED stage=QEMU mode=$mode rc=$rc" >&2
    head -n 25 "$work/$mode.err" >&2
    exit 1
  fi
done
cmp "$work/gcc.out" "$work/mini.out"
cmp "$work/gcc.err" "$work/mini.err"
echo "RV32_MUSL_ABI=PASS upstream=1.2.5 libc=original_syscalls CRT=musl ELF32=1 ABI=ilp32 GNU_MiniC_QEMU_byte_exact=1"

# The first BusyBox milestone is a real, immutable upstream Kbuild
# reference build. It must succeed with genuine musl before any claim
# about MiniC's percentage or an executable. Minimal applets are an
# explicit configuration frontier, NOT all-applets BusyBox acceptance.
commit=1a64f6a20aaf6ea4dbba68bbfa8cc1ab7e5c57c4
curl -fsSL --retry 3 --max-time 120 \
  "https://github.com/mirror/busybox/archive/$commit.tar.gz" \
  -o "$work/busybox.tgz"
mkdir -p "$work/busybox" "$work/busybox-out"
tar -xzf "$work/busybox.tgz" -C "$work/busybox" --strip-components=1
sh "$repo/tests/external/busybox/check-pinned-1361-source.sh" \
  "$work/busybox"
make -C "$work/busybox" O="$work/busybox-out" \
  HOSTCC=cc allnoconfig >"$work/kconfig.log" 2>&1 || {
    echo "RV32_BUSYBOX=BLOCKED stage=original_Kconfig" >&2
    tail -n 35 "$work/kconfig.log" >&2
    exit 1
  }
# BusyBox's Kconfig is source-owned. Enable a small coherent original
# utility cohort; do not modify any of the vendor's C source files.
python3 - "$work/busybox-out/.config" <<'PY'
import sys
p=sys.argv[1]
names={"STATIC","ECHO","TRUE","FALSE","CAT","HEAD","WC"}
lines=open(p).readlines()
for i,line in enumerate(lines):
    for name in names:
        if line.strip()==f"# CONFIG_{name} is not set":
            lines[i]=f"CONFIG_{name}=y\n"
        elif line.startswith(f"CONFIG_{name}="):
            lines[i]=f"CONFIG_{name}=y\n"
open(p,"w").writelines(lines)
PY
# BusyBox 1.36.1 Kconfig has 'oldconfig', not Linux's newer
# 'olddefconfig' target. Answer optional defaults without modifying C.
{ yes "" || :; } | timeout 90s make -C "$work/busybox" O="$work/busybox-out" \
  HOSTCC=cc oldconfig >"$work/olddefconfig.log" 2>&1 || {
    echo "RV32_BUSYBOX=BLOCKED stage=olddefconfig" >&2
    tail -n 32 "$work/olddefconfig.log" >&2
    exit 1
  }
# GNU reference Kbuild with the exact musl headers, never Picolibc or
# RV64 host glibc. The command-line driver is stable for all original
# Kbuild C owners. Link closure is explicitly verified.
cat >"$work/rv32-linux-musl-gcc" <<EOF
#!/usr/bin/env bash
exec "$BARE_CC" -march=rv32i -mabi=ilp32 \
  -nostdinc -isystem "$sysroot/usr/include" -isystem "$builtins" \
  -I"$repo/tests/linker/fixtures/busybox-uapi" -fno-builtin \
  "\$@"
EOF
chmod +x "$work/rv32-linux-musl-gcc"
# The original BusyBox linker defaults assume GCC drives a native GNU
# libc. For this initial build probe, use its own Kbuild owner inventory
# and fail explicitly if the GNU/musl baseline cannot link.
set +e
timeout 240s make -C "$work/busybox" O="$work/busybox-out" -j4 \
    ARCH=riscv CC="$work/rv32-linux-musl-gcc" HOSTCC=cc \
    EXTRA_LDFLAGS="-nostdlib -static -Wl,-e,_start \
    $work/build/obj/crt/crt1.o $work/build/obj/crt/crti.o \
    $work/build/lib/libc.a $libgcc $work/build/obj/crt/crtn.o" \
    busybox >"$work/busybox-build.log" 2>&1
rc=$?
set -e
owners=$(find "$work/busybox-out" -type f -name '*.o' \
    ! -path '*/scripts/*' | wc -l)
echo "RV32_BUSYBOX_KBUILD_DISCOVERY pinned=1.36.1 owners=$owners rc=$rc"
if [ "$rc" -ne 0 ]; then
    echo "RV32_BUSYBOX=BLOCKED stage=GNU_original_Kbuild rc=$rc" >&2
    grep -E 'error:|fatal error:|No rule|undefined reference|Error [0-9]+' \
      "$work/busybox-build.log" | head -n 28 >&2 || true
    tail -n 30 "$work/busybox-build.log" >&2
    exit 1
fi
test -s "$work/busybox-out/busybox"
echo "RV32_BUSYBOX_GNU_KBUILD=PASS original=1.36.1 owners=$owners musl=1.2.5 ELF32=1"
