#!/bin/sh
# Original BusyBox 1.36.1 allyesconfig: all 581 C owners are MiniC-produced
# GNU-assembled objects, GNU-linked through unchanged Kbuild, and the entire
# resulting BusyBox ELF executes 38 exact GNU/QEMU differential fixtures.
# This does NOT claim MiniC assembler/linker ownership or every BusyBox applet.
set -eu
test "$#" -eq 5 || {
  echo 'usage: probe-aarch64-full-c-busybox.sh SOURCE GCC CROSS_PREFIX QEMU SYSROOT' >&2
  exit 2
}
repo=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
: "${MINIC_CC:?set MINIC_CC}"
exec env FOUR_BACKEND_BUSYBOX_AARCH64_PIE=1 \
  FOUR_BACKEND_BUSYBOX_FULL_C_BINARY=1 \
  sh "$repo/probe-four-backend-all-kbuild.sh" \
    "$1" aarch64 "$2" "$3" "$4" "$5"
