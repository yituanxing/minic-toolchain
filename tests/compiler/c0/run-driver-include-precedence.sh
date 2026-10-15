#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-driver-include-precedence
sysroot="$work/sysroot"
user="$work/user"
object="$work/driver_include_precedence.o"

rm -rf "$work"
mkdir -p "$sysroot/include" "$user"
cat >"$sysroot/include/minic_driver_precedence.h" <<'EOF'
#define MINIC_DRIVER_INCLUDE_SOURCE 1
EOF
cat >"$user/minic_driver_precedence.h" <<'EOF'
#define MINIC_DRIVER_INCLUDE_SOURCE 2
EOF
cat >"$work/input.c" <<'EOF'
#include <minic_driver_precedence.h>
_Static_assert(MINIC_DRIVER_INCLUDE_SOURCE == 2, "user -I must shadow default sysroot headers");
int driver_include_precedence_probe(void) { return MINIC_DRIVER_INCLUDE_SOURCE; }
EOF

"$minic" --sysroot "$sysroot" -I"$user" -c -o "$object" "$work/input.c"
test -s "$object"
readelf -Ws "$object" | grep -Eq "GLOBAL.*driver_include_precedence_probe$"
printf '%s\n' "PASS compiler/c0/driver_include_precedence user_I=before-default-sysroot"
