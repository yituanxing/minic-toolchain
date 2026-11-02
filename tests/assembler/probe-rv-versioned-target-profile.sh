#!/usr/bin/env bash
# Explicit standardized versions are semantic Target-owned ISA values.
# They must resolve to the same code, attributes and mapping as their
# verified implicit-default counterparts, without admitting unknown modes.
set -Eeuo pipefail
: "${MINIAS:?set MINIAS to exact-head minic-as}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
printf '.text\n.globl versioned_entry\nversioned_entry:\n  nop\n' >"$work/base.s"

profiles=(
  'rv32i2p1|rv32i|ilp32'
  'rv32e2p0|rv32e|ilp32e'
  'rv64i2p1|rv64i|lp64'
  'rv64i2p1m2p0a2p1|rv64ima|lp64'
  'rv64i2p1f2p2d2p2|rv64ifd|lp64d'
  'rv64i2p1c2p0|rv64ic|lp64'
  'rv64i2p1v1p0|rv64iv|lp64'
  'rv64i2p1_zicsr2p0_zifencei2p0|rv64i_zicsr_zifencei|lp64'
  'rv64i2p1_zbb1p0_zihintpause2p0|rv64i_zbb_zihintpause|lp64'
)
index=0
for row in "${profiles[@]}"; do
  IFS='|' read -r explicit shorthand abi <<<"$row"
  "$MINIAS" --target=riscv "-march=$explicit" "-mabi=$abi" \
    -o "$work/$index-explicit.o" "$work/base.s"
  "$MINIAS" --target=riscv "-march=$shorthand" "-mabi=$abi" \
    -o "$work/$index-implicit.o" "$work/base.s"
  cmp "$work/$index-explicit.o" "$work/$index-implicit.o"
  # GNU is an external oracle, not a replacement for the local identical
  # object check. Exact source and ABI are passed to both assemblers.
  if command -v riscv64-linux-gnu-as >/dev/null; then
    riscv64-linux-gnu-as "-march=$explicit" "-mabi=$abi" \
      -o "$work/$index-gnu.o" "$work/base.s"
    actual=$(readelf -A "$work/$index-explicit.o" |
             sed -n '/Tag_RISCV_arch:/p')
    expected=$(readelf -A "$work/$index-gnu.o" |
               sed -n '/Tag_RISCV_arch:/p')
    test "$actual" = "$expected"
  fi
  index=$((index+1))
done

# Version spelling is resolved by the same inventory in -march and
# source-local +extension; it cannot upgrade file Tag_RISCV_arch.
for suffix in implicit explicit; do
  if [[ "$suffix" == explicit ]]; then zbb=zbb1p0; vector=v1p0
  else zbb=zbb; vector=v; fi
  printf '.option norvc\n.text\n.globl entry\nentry:\n.option push\n.option arch,+%s\nctz a0,a0\n.option arch,+%s\nvsetvli a1,a0,e8,m1,ta,ma\n.option pop\naddi a2,a2,1\n' \
    "$zbb" "$vector" >"$work/site-$suffix.s"
  "$MINIAS" --target=riscv -march=rv64i2p1 -mabi=lp64 \
    -o "$work/site-$suffix.o" "$work/site-$suffix.s"
done
cmp "$work/site-explicit.o" "$work/site-implicit.o"
readelf -A "$work/site-explicit.o" | grep -q 'rv64i2p1'
if command -v riscv64-linux-gnu-as >/dev/null &&
   command -v riscv64-linux-gnu-objcopy >/dev/null; then
  riscv64-linux-gnu-as -march=rv64i2p1 -mabi=lp64 -mno-relax \
    -o "$work/site-gnu.o" "$work/site-explicit.s"
  for section in .text .riscv.attributes; do
    riscv64-linux-gnu-objcopy -O binary -j "$section" \
      "$work/site-explicit.o" "$work/site-mini-${section#.}.bin"
    riscv64-linux-gnu-objcopy -O binary -j "$section" \
      "$work/site-gnu.o" "$work/site-gnu-${section#.}.bin"
    cmp "$work/site-mini-${section#.}.bin" "$work/site-gnu-${section#.}.bin"
  done
fi

# Reject unsupported versions rather than silently projecting fixed
# features into a different version's ELF architecture attribute.
bad_profiles=(
  'rv64i2p2|lp64'
  'rv32e2p1|ilp32e'
  'rv64i2p1m2p1|lp64'
  'rv64i2p1v1p1|lp64'
  'rv64i2p1_zbb1p1|lp64'
  'rv64i2p1_zicsr2p1|lp64'
  'rv64i2p1m2p|lp64'
  'rv64i2p1_zbb1p0_zbb1p0|lp64'
  'rv64ix|lp64'
  'rv64i2p1z|lp64'
)
for row in "${bad_profiles[@]}"; do
  IFS='|' read -r bad bad_abi <<<"$row"
  if "$MINIAS" --target=riscv "-march=$bad" "-mabi=$bad_abi" \
      -o "$work/bad.o" "$work/base.s" 2>"$work/bad.err"; then
    echo "MiniAS admitted unsupported versioned ISA: $bad" >&2
    exit 1
  fi
  test ! -e "$work/bad.o"
  grep -Eq 'unsupported|unqualified|duplicate|incompatible' "$work/bad.err"
done
printf '.text\n.option arch,+zbb1p1\nctz a0,a0\n' >"$work/bad-site.s"
if "$MINIAS" --target=riscv -march=rv64i -mabi=lp64 \
    -o "$work/bad-site.o" "$work/bad-site.s" 2>"$work/bad-site.err"; then
  echo 'MiniAS accepted unsupported local version' >&2; exit 1
fi
test ! -e "$work/bad-site.o"

if command -v riscv64-linux-gnu-as >/dev/null; then oracle=GNU
else oracle=SKIP_toolchain_unavailable; fi
echo "MINIAS_RV_VERSIONED_PROFILE=PASS profiles=$index site_alias=1 invalid=11 GNU=$oracle"
