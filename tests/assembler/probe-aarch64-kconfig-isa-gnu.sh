#!/usr/bin/env bash
# Linux 6.6.143 arch/arm64/Kconfig ISA-feature probes and the machine code
# they enable.  GNU GAS is the independent byte oracle; no stubbed success.
set -Eeuo pipefail

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
mini=${MINIAS:-"$root/build/release/bin/minic-as"}
gas=${AARCH64_AS:-aarch64-linux-gnu-as}
objcopy=${AARCH64_OBJCOPY:-aarch64-linux-gnu-objcopy}
test -x "$mini"
command -v "$gas" >/dev/null
command -v "$objcopy" >/dev/null

work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
passed=0

positive() {
  local name=$1 march=$2
  "$mini" --target=aarch64 "-march=$march" -mabi=lp64 \
    -o "$work/$name-mini.o" "$work/$name.s"
  "$gas" "-march=$march" -o "$work/$name-gnu.o" "$work/$name.s"
  "$objcopy" -j .text -O binary "$work/$name-mini.o" "$work/$name-mini.bin"
  "$objcopy" -j .text -O binary "$work/$name-gnu.o" "$work/$name-gnu.bin"
  cmp "$work/$name-mini.bin" "$work/$name-gnu.bin"
  test "$(wc -c < "$work/$name-mini.bin")" -gt 0
  passed=$((passed+1))
}

negative() {
  local name=$1 march=$2
  if "$mini" --target=aarch64 "-march=$march" -mabi=lp64 \
       -o "$work/$name-mini.o" "$work/$name.s" >"$work/$name-mini.log" 2>&1; then
    echo "MiniAS silently accepted invalid AArch64 ISA: $name" >&2; exit 1
  fi
  if "$gas" "-march=$march" -o "$work/$name-gnu.o" \
       "$work/$name.s" >"$work/$name-gnu.log" 2>&1; then
    echo "GNU accepted negative ISA control: $name" >&2; exit 1
  fi
}

# Exact Linux arch/arm64/Kconfig cc-option probes (-Wa,-march=...).
for arch in armv8.2-a armv8.3-a armv8.4-a; do
  printf '.text\nnop\n' > "$work/cc-${arch}.s"
  positive "cc-${arch}" "$arch"
done

# Linux as-instr: `.arch armv8.2-a+sha3`; verify all eight SHA3/SHA512
# machine encodings so a Kconfig YES cannot mask an absent instruction set.
cat > "$work/as-sha3.s" <<'ASM'
.arch armv8.2-a+sha3
.text
eor3 v0.16b, v1.16b, v2.16b, v3.16b
bcax v4.16b, v5.16b, v6.16b, v7.16b
rax1 v8.2d, v9.2d, v10.2d
xar v11.2d, v12.2d, v13.2d, #42
sha512h q0, q1, v2.2d
sha512h2 q3, q4, v5.2d
sha512su0 v6.2d, v7.2d
sha512su1 v8.2d, v9.2d, v10.2d
ASM
positive as-sha3 armv8-a

# Linux as-instr: `.arch_extension rcpc`; compare LDAPR instruction widths.
cat > "$work/as-rcpc.s" <<'ASM'
.arch_extension rcpc
.text
ldapr w0, [x1]
ldapr x2, [x3]
ldaprb w4, [x5]
ldaprh w6, [x7]
ASM
positive as-rcpc armv8-a

# AS_HAS_ARMV8_3 gates kernel pointer authentication.  Check actual fixed
# PAC HINT, authenticated returns and BTI encodings against GAS.
cat > "$work/ptrauth.s" <<'ASM'
.text
paciasp
autiasp
pacibsp
autibsp
bti c
bti j
bti jc
retaa
retab
ASM
positive ptrauth armv8.3-a

# AS_HAS_ARMV8_4 gates ARM64_TLB_RANGE; real TLBI range instruction forms.
cat > "$work/tlb-range.s" <<'ASM'
.text
tlbi rvae1is,x0
tlbi rvale1is,x1
tlbi rvae1,x2
tlbi rvale1,x3
ASM
positive tlb-range armv8.4-a

# ISA feature selection has to affect accepted mnemonics, not only
# Kconfig's empty input probes; unsupported selected forms fail closed.
printf '.text\neor3 v0.16b,v1.16b,v2.16b,v3.16b\n' > "$work/no-sha3.s"
negative no-sha3 armv8.2-a
printf '.text\nldapr x0,[x1]\n' > "$work/no-rcpc.s"
negative no-rcpc armv8.2-a
printf '.text\nretab\n' > "$work/no-pauth.s"
negative no-pauth armv8-a
printf '.text\ntlbi rvae1is,x0\n' > "$work/no-range.s"
negative no-range armv8.3-a
printf '.text\nnop\n' > "$work/bad-revision.s"
negative bad-revision armv8.6-a+unknownfeature

echo "MINIAS_A64_LINUX_KCONFIG_ISA_GNU=PASS GNU_bytes=exact positive=$passed negatives=5 AS_HAS_ARMV8_2_SHA3_ARMV8_3_LDAPR_ARMV8_4=5"
