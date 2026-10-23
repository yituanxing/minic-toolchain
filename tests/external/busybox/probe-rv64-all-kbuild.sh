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
# These upstream git object IDs are shared with the two other cross targets.
# Checking actual source content is independent from trusting a download URL.
for item in \
  Makefile:fa8cb761443388730372eaf260e7ca1a0235bc43 \
  coreutils/cat.c:4b3414941e87a47bd639bc481ab9f3fa0045ea80 \
  coreutils/echo.c:44b2eb5d05fac12374b5f8692990014087cfda01 \
  coreutils/head.c:c7537a20e4e01483f3a6ffaf89eb2da25d7acf5f \
  coreutils/wc.c:d5238d3fcfba2f62f6bb292a3af55c5107c3ec93 \
  findutils/grep.c:0b72812f18b9177855e226c78f3625146b8daee2 \
  coreutils/cut.c:55bdd938611feb51e7bf1b7977b87015b2819cf9; do
  file=${item%%:*}; expected=${item#*:}
  actual=$(git hash-object "$work/source/$file")
  test "$actual" = "$expected" || {
    echo "RV64_BUSYBOX_SOURCE=FAIL file=$file" >&2; exit 1;
  }
done
echo "RV64_BUSYBOX_SOURCE=PASS version=1.36.1 commit=$commit"
sh "$repo/tests/external/busybox/probe-four-backend-all-kbuild.sh" \
  "$work/source" riscv64 riscv64-linux-gnu-gcc riscv64-linux-gnu- \
  qemu-riscv64 /usr/riscv64-linux-gnu
