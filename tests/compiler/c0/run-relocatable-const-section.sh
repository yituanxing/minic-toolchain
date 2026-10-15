#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
work=${BUILD_DIR:-"$root/build/debug"}/tests/relocatable-const-section
rm -rf "$work"
mkdir -p "$work"

cat >"$work/input.c" <<'SRC'
static const char alpha[] = "alpha";
static const char beta[] = "beta";

static const char * const names[] = {
    alpha,
    beta
};

const char *pick_name(int index)
{
    return names[index];
}
SRC

"$minic" -S "$work/input.c" -o "$work/output.s"
test -s "$work/output.s"

awk '
  /^\.section \.data\.rel\.ro$/ { in_relro=1; next }
  /^\.section / { in_relro=0 }
  in_relro && /^names:$/ { found=1 }
  END { exit found ? 0 : 1 }
' "$work/output.s"

awk '
  /^\.section \.rodata$/ { in_ro=1; next }
  /^\.section / { in_ro=0 }
  in_ro && /^alpha:$/ { alpha=1 }
  in_ro && /^beta:$/ { beta=1 }
  END { exit alpha && beta ? 0 : 1 }
' "$work/output.s"

printf '%s\n' 'PASS compiler/c0/relocatable-const-section pointer-table=data.rel.ro pure-const=rodata'
