#!/bin/sh
set -eu

: "${MINIPP:?MINIPP must point to minic-cpp}"
: "${BUILD_DIR:?BUILD_DIR must be set}"
host_cpp=${HOST_CPP:-cc}
work="$BUILD_DIR/tests/preprocessor/include-next"
overlay="$work/overlay"
system="$work/system"

rm -rf "$work"
mkdir -p "$overlay" "$system"

cat >"$overlay/demo.h" <<'EOF'
#define OVERLAY_VALUE 1
#include "nested.h"
EOF
cat >"$overlay/nested.h" <<'EOF'
#define OVERLAY_NESTED 4
#include_next <nested.h>
EOF
cat >"$system/nested.h" <<'EOF'
#define SYSTEM_NESTED 8
EOF
cat >"$work/input.c" <<'EOF'
#include <demo.h>
int include_next_probe = OVERLAY_VALUE + OVERLAY_NESTED + SYSTEM_NESTED;
EOF

"$host_cpp" -E -P -undef -nostdinc -I"$overlay" -I"$system" \
  "$work/input.c" -o "$work/gcc.i"
"$MINIPP" -E -P -undef -nostdinc -I"$overlay" -I"$system" \
  "$work/input.c" -o "$work/mini.i"
cmp "$work/gcc.i" "$work/mini.i"
grep -F "int include_next_probe = 1 + 4 + 8;" "$work/mini.i" >/dev/null
printf '%s\n' "MINIPP_INCLUDE_NEXT=PASS provenance=quoted-inherit search=next-path exact=GCC"
