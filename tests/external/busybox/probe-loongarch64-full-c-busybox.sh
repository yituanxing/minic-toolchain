#!/bin/sh
# Original BusyBox 1.36.1 LoongArch64 all-581 MiniC C-owner ELF,
# linked by GNU Kbuild and differential-tested under QEMU in 38 cases.
# GNU assembler/linker remain intentional; no all-MiniC toolchain claim.
set -eu
test "$#" -eq 5 || {
  echo 'usage: probe-loongarch64-full-c-busybox.sh SOURCE GCC CROSS_PREFIX QEMU SYSROOT' >&2
  exit 2
}
repo=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
: "${MINIC_CC:?set MINIC_CC}"
exec env FOUR_BACKEND_BUSYBOX_FULL_C_BINARY=1 \
  sh "$repo/probe-four-backend-all-kbuild.sh" \
    "$1" loongarch64 "$2" "$3" "$4" "$5"
