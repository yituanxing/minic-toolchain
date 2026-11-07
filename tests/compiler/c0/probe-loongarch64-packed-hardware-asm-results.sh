#!/usr/bin/env bash
set -Eeuo pipefail
mini=${1:?MiniC compiler}
test -x "$mini"
here=$(cd "$(dirname "$0")" && pwd)
src="$here/loongarch64_packed_hardware_asm_results.c"
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT

cc -std=gnu11 -Wall -Wextra -Werror -fsyntax-only "$src"
cc -E -P -x c "$src" -o "$d/source.i"
"$mini" --target=loongarch64-linux -S "$d/source.i" -o "$d/source.s"
for entry in packed_pte_result:t2:t4 packed_amo_fetch:t1:t2 \
             packed_xchg:t1:t2 packed_csr_xchg:t2:t1; do
  IFS=: read -r name base payload <<<"$entry"
  sed -n "/^${name}:/,/^\.size ${name},/p" "$d/source.s" > "$d/$name.s"
  test -s "$d/$name.s"
  # Real MachineIR control flow must choose one native width-8 store for an
  # aligned address and eight byte writes for the packed/unaligned path.
  # Both branches are proven by a physical T3 test and target-owned block.
  grep -Fq "andi \$t3, \$$base, 7" "$d/$name.s"
  grep -Eq "^[[:space:]]*beqz[[:space:]]+\\\$t3,[[:space:]]*\\.Lminic_mir_${name}_[0-9]+[[:space:]]*\$" "$d/$name.s"
  grep -Fq "st.b \$t3, \$$base, 7" "$d/$name.s"
  grep -Fq "st.d \$$payload, \$$base, 0" "$d/$name.s"
done
grep -Fq 'll.d $t4, $t1, 0' "$d/packed_pte_result.s"
grep -Fq 'sc.d $t4, $t1, 0' "$d/packed_pte_result.s"
grep -Fq 'dbar 0x700' "$d/packed_pte_result.s"
grep -Fq 'amadd_db.d $t2, $t0, $t1' "$d/packed_amo_fetch.s"
grep -Fq 'amswap_db.d $t2, $t0, $t1' "$d/packed_xchg.s"
grep -Fq 'csrxchg $t1, $t0, 0' "$d/packed_csr_xchg.s"
test "$(grep -Fc 'andi $t3, $t2, 7' "$d/packed_csr_xchg.s")" = 2
grep -Fq 'ld.bu $t0, $t2, 0' "$d/packed_csr_xchg.s"
grep -Fq 'ld.d $t1, $t2, 0' "$d/packed_csr_xchg.s"

# Source-owned ISA forms stay exact: modified instruction text cannot quietly
# turn into a target assembler escape, including privileged CSR operations.
for bad in pte amo xchg csr; do
  case "$bad" in
    pte) sed 's/ll.d %1, %0/ll.d %1, %0; nop/' "$d/source.i" > "$d/bad.i" ;;
    amo) sed 's/amadd_db.d %1/amadd_db.d %1; nop/' "$d/source.i" > "$d/bad.i" ;;
    xchg) sed 's/amswap_db.d %1/amswap_db.d %1; nop/' "$d/source.i" > "$d/bad.i" ;;
    csr) sed 's/csrxchg %0, %1, %2/csrxchg %0, %1, 1/' "$d/source.i" > "$d/bad.i" ;;
  esac
  if "$mini" --target=loongarch64-linux -S "$d/bad.i" -o "$d/bad.s" >/dev/null 2>&1; then
    echo "LOONG_PACKED_HARDWARE=FAIL accepted $bad" >&2
    exit 1
  fi
  test ! -e "$d/bad.s"
done
echo 'LOONG_PACKED_HARDWARE_RESULT=PASS GNU_C=PASS PTE_AMO_XCHG_CSR=4 guarded_native_and_byte_scatter=4 CSR_guarded_ingress=1 negatives=4 QEMU=NOT_RUN'
