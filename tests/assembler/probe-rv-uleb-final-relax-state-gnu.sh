#!/usr/bin/env bash
# GNU GAS riscv_md_finish makes its ULEB relocation decision from the final
# `.option [no]relax` state.  That object-byte policy is unsafe if a prior
# R_RISCV_RELAX site can shrink inside the ULEB span.  MiniAS preserves the
# final-state GNU contract for stable spans, but deliberately retains the
# pair across an earlier relaxable instruction when the file ends norelax.
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
      default|late_norelax) printf '.option relax\n';;
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
  case "$scenario" in
    default|restored_pop)
      cmp "$work/$scenario.gnu.uleb" "$work/$scenario.mini.uleb"
      test "$(wc -l <"$work/$scenario.gnu.uleb")" -eq 2;;
    terminal_norelax)
      cmp "$work/$scenario.gnu.uleb" "$work/$scenario.mini.uleb"
      test ! -s "$work/$scenario.gnu.uleb";;
    late_norelax)
      test ! -s "$work/$scenario.gnu.uleb"
      test "$(wc -l <"$work/$scenario.mini.uleb")" -eq 2;;
  esac
  echo "MINIAS_RV_ULEB_FINAL_OPTION=PASS scenario=$scenario stable_GNU_parity_or_cross_site_link_safety"
done
