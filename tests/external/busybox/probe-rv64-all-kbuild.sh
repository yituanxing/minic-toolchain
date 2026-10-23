#!/bin/sh
# RV64 current-source counterpart of the certified AArch64/LoongArch64
# original BusyBox 1.36.1 all-Kbuild-C-owner GNU/QEMU acceptance.
# Never reuse the old BusyBox 1.38/408-applet receipt or guess 581 owners.
set -eu
: "${MINIC_CC:?set MINIC_CC to a current-source compiler}"
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
commit=1a64f6a20aaf6ea4dbba68bbfa8cc1ab7e5c57c4
mkdir -p "$work/source"
timeout 120s curl -fsSL --retry 2 \
  "https://github.com/mirror/busybox/archive/$commit.tar.gz" \
  -o "$work/source.tar.gz"
tar -xzf "$work/source.tar.gz" -C "$work/source" --strip-components=1
sh "$repo/tests/external/busybox/check-pinned-1361-source.sh" "$work/source"
echo "RV64_BUSYBOX_SOURCE=PASS version=1.36.1 commit=$commit"
sh "$repo/tests/external/busybox/probe-four-backend-all-kbuild.sh" \
  "$work/source" riscv64 riscv64-linux-gnu-gcc riscv64-linux-gnu- \
  qemu-riscv64 /usr/riscv64-linux-gnu
