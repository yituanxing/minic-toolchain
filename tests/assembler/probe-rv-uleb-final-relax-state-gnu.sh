#!/usr/bin/env bash
# GNU GAS riscv_md_finish makes the ULEB relocation decision from the final
# global `.option [no]relax` state, even for a previously parsed fragment.
# This is distinct from the presence of instruction R_RISCV_RELAX markers.
set -Eeuo pipefail
test -n "${MINIAS:-}"
gas=${RISCV_GAS:-riscv64-linux-gnu-as}
command -v "$gas" >/dev/null || {
  echo "GNU RISC-V assembler missing: $gas" >&2
  exit 2
}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
for scenario in default terminal_norelax restored_pop late_norelax; do
  {
    case "$scenario" in
      terminal_norelax) printf '.option norelax\n';;
      restored_pop) printf '.option push\n.option norelax\n';;
    esac
    cat <<'ASM'
.text
.Lstart:
  la a0,external_data
.Lend:
.section .debug_rnglists,"",@progbits
  .uleb128 .Lend-.Lstart
ASM
    case "$scenario" in
      restored_pop) printf '.option pop\n';;
      late_norelax) printf '.option norelax\n';;
    esac
  } >"$work/$scenario.s"
  "$gas" -mrelax -o "$work/$scenario.gnu.o" "$work/$scenario.s"
  "$MINIAS" --target=riscv -o "$work/$scenario.mini.o" "$work/$scenario.s"
  readelf -rW "$work/$scenario.gnu.o" >"$work/$scenario.gnu.reloc"
  readelf -rW "$work/$scenario.mini.o" >"$work/$scenario.mini.reloc"
  for who in gnu mini; do
    awk '/R_RISCV_(SET|SUB)_ULEB128/ { print $1, $3, $5, $6, $7 }' \
      "$work/$scenario.$who.reloc" >"$work/$scenario.$who.uleb"
  done
  cmp "$work/$scenario.gnu.uleb" "$work/$scenario.mini.uleb"
  case "$scenario" in
    default|restored_pop) test "$(wc -l <"$work/$scenario.gnu.uleb")" -eq 2;;
    terminal_norelax|late_norelax) test ! -s "$work/$scenario.gnu.uleb";;
  esac
  echo "MINIAS_RV_ULEB_FINAL_OPTION=PASS scenario=$scenario GNU_relocation_order=exact"
done
