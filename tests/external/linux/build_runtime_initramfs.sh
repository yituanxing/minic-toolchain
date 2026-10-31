#!/usr/bin/env bash
set -Eeuo pipefail

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cc=${RISCV_CC:-riscv64-linux-gnu-gcc}
work=${BUILD_DIR:-"$root/build/linux-runtime-initramfs"}
out=${OUTPUT_INITRAMFS:-"$work/runtime-initramfs.cpio.gz"}
reproducible=${LINUX_RUNTIME_REPRODUCIBLE:-0}

# A caller may select the temporary build area, but this script owns only its
# dedicated root/ subtree. Never clean a filesystem root or the source tree.
if [[ -z $work || $work == / || $work == "$root" ]]; then
  echo "LINUX_RUNTIME_INITRAMFS=ERROR unsafe_build_dir=$work" >&2
  exit 2
fi
mkdir -p "$work"
work=$(cd "$work" && pwd -P)
if [[ $work == / || $work == "$root" ]]; then
  echo "LINUX_RUNTIME_INITRAMFS=ERROR unsafe_resolved_build_dir=$work" >&2
  exit 2
fi
tree="$work/root"

case "$reproducible" in
  0) ;;
  1)
    # Independent GNU/MiniAS QEMU jobs must embed identical initramfs bytes.
    # SOURCE_DATE_EPOCH also controls future C __DATE__/__TIME__ expansions.
    export SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-0}
    if [[ ! $SOURCE_DATE_EPOCH =~ ^[0-9]+$ ]]; then
      echo "LINUX_RUNTIME_INITRAMFS=ERROR invalid_source_date_epoch=$SOURCE_DATE_EPOCH" >&2
      exit 2
    fi
    ;;
  *)
    echo "LINUX_RUNTIME_INITRAMFS=ERROR invalid_reproducible_mode=$reproducible" >&2
    exit 2
    ;;
esac

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
cpio_args=(--null -o --format=newc --quiet)
if [[ $reproducible == 1 ]]; then
  # newc contains uid/gid, inode, mode and mtime, even with gzip -n. Normalize
  # those fields before archiving; --reproducible canonicalizes GNU cpio inode
  # assignment and --owner removes runner-specific uid/gid from the archive.
  find "$tree" -type d -exec chmod 0755 -- {} +
  chmod 0755 -- "$tree/init" "$tree/bin/sh" "$tree/bin/runtime-probe"
  find "$tree" -exec touch -h -d "@$SOURCE_DATE_EPOCH" -- {} +
  cpio_args+=(--reproducible --owner=0:0)
fi
(
  cd "$tree"
  find . -print0 | LC_ALL=C sort -z | cpio "${cpio_args[@]}"
) | gzip -9n >"$out"

test -s "$out"
printf 'LINUX_RUNTIME_INITRAMFS=PASS path=%s bytes=%s\n' "$out" "$(stat -c %s "$out")"
printf 'LINUX_RUNTIME_INITRAMFS_SHA256=%s reproducible=%s\n' \
  "$(sha256sum "$out" | cut -d' ' -f1)" "$reproducible"
