#!/usr/bin/env bash
# Linux AMD IOMMU ivrs_ioapic_quirks[][3] has a designated, inferred outer
# bound and statically initialized nested record arrays.  Check the x86 data
# emitter as well as frontend acceptance with the original shape.
set -Eeuo pipefail

minic=$1
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
src="$root/tests/compiler/c0/static_inferred_record_multidimensional.c"
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT

cc -std=gnu11 -E -P -x c "$src" -o "$d/quirks.i"
"$minic" --target=x86_64-linux -S "$d/quirks.i" -o "$d/quirks.s"
cc -no-pie -x assembler "$d/quirks.s" -o "$d/minic"
cc -std=gnu11 -no-pie "$src" -o "$d/gnu"

"$d/minic"
"$d/gnu"
echo 'MINIC_X86_LINUX_2D_RECORD_ARRAY=PASS extent=4x3 designated=1 hole_zero=1 native=1'
