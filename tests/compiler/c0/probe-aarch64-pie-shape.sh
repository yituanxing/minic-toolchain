#!/bin/sh
# Fast GNU-independent AAPCS64 PIC/PIE address-model gate. The separate
# probe-aarch64-pie-extern-got.sh validates GNU AS/LD and QEMU behavior.
set -eu
: "${MINIC_CC:?point MINIC_CC at the built compiler}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
cat >"$work/pic.i" <<'SRC'
extern int external_value;
extern int external_add(int);
static int local_value = 17;
static int local_add(int x) { return x + 3; }
int *get_local_object(void) { return &local_value; }
int *get_external_object(void) { return &external_value; }
int (*get_local_function(void))(int) { return &local_add; }
int (*get_external_function(void))(int) { return &external_add; }
SRC
for flag in -fPIE -fPIC; do
  "$MINIC_CC" --target=aarch64-linux "$flag" -S \
    "$work/pic.i" -o "$work/minic.s"
  grep -Fq 'adrp x9, local_value' "$work/minic.s"
  grep -Fq 'adrp x9, local_add' "$work/minic.s"
  grep -Fq 'adrp x9, :got:external_value' "$work/minic.s"
  grep -Fq 'adrp x9, :got:external_add' "$work/minic.s"
  cp "$work/minic.s" "$work/${flag#-}.s"
done
cmp "$work/fPIE.s" "$work/fPIC.s"
echo 'MINIC_AARCH64_PIE_SHAPE=PASS local_pcrel=2 external_got=2 PIE=1 PIC=1'
