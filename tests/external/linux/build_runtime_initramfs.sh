#!/usr/bin/env bash
set -Eeuo pipefail

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cc=${RISCV_CC:-riscv64-linux-gnu-gcc}
work=${BUILD_DIR:-"$root/build/linux-runtime-initramfs"}
out=${OUTPUT_INITRAMFS:-"$work/runtime-initramfs.cpio.gz"}
tree="$work/root"

rm -rf "$tree"
mkdir -p "$tree/bin" "$tree/proc" "$tree/dev" "$tree/sys" "$tree/tmp"

"$cc" -static -O2 -Wall -Wextra -Werror \
  "$root/tests/external/linux/runtime_initramfs/init.c" -o "$tree/init"
"$cc" -static -O2 -Wall -Wextra -Werror \
  "$root/tests/external/linux/runtime_initramfs/minish.c" -o "$tree/bin/sh"
"$cc" -static -O2 -Wall -Wextra -Werror \
  "$root/tests/external/linux/runtime_initramfs/probe.c" -o "$tree/bin/runtime-probe"

file "$tree/init" "$tree/bin/sh" "$tree/bin/runtime-probe"
mkdir -p "$(dirname "$out")"
(
  cd "$tree"
  find . -print0 | LC_ALL=C sort -z | cpio --null -o --format=newc --quiet
) | gzip -9n >"$out"

test -s "$out"
printf 'LINUX_RUNTIME_INITRAMFS=PASS path=%s bytes=%s\n' "$out" "$(stat -c %s "$out")"
