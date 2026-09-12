#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-driver-gnu-predefines
object="$work/driver_gnu_predefines.o"

rm -rf "$work"
mkdir -p "$work"
"$minic" -nostdinc -c -o "$object" "$root/tests/compiler/c0/driver_gnu_predefines.c"
test -s "$object"
readelf -h "$object" | grep -q "REL (Relocatable file)"
readelf -Ws "$object" | grep -Eq "GLOBAL.*driver_gnu_predefine_probe$"
printf '%s\n' "PASS compiler/c0/driver_gnu_predefines gnu_surface=2.7 packed_size=5 pipeline=driver"
