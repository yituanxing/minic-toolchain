#!/usr/bin/env bash
# Full-source replay must use one immutable physical input graph, preserve
# expansion-site identity, and not manufacture new ELF symbols or fragments.
# The x86/RV GNU oracle covers semicolons, macro-in-macro, REPT, IRP,
# forward .set aliases and the order of public/local non-private symbols.
set -Eeuo pipefail
: "${MINIAS:?MiniAS executable required}"
: "${ARCH:?Target required}"

case "$ARCH" in
  x86_64)
    mini=(--target=x86_64); gnu=(as --64); copy=(objcopy)
    branch='je far'; nop='nop'; repetitions=256; expected_span=6;;
  riscv)
    mini=(--target=riscv -march=rv64i -mabi=lp64)
    branch='beq a0,a1,far'; nop='addi x0,x0,0'
    repetitions=1200; expected_span=8
    if command -v riscv64-linux-gnu-as >/dev/null &&
       command -v riscv64-linux-gnu-objcopy >/dev/null; then
      gnu=(riscv64-linux-gnu-as -march=rv64i -mabi=lp64)
      copy=(riscv64-linux-gnu-objcopy)
    elif [[ "${MINIAS_ALLOW_MISSING_GNU:-0}" == 1 ]]; then
      gnu=(); copy=()
    else
      echo 'RV GNU cross assembler and objcopy are mandatory for CI' >&2
      exit 1
    fi;;
  *) echo "unsupported ARCH=$ARCH" >&2; exit 2;;
esac

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
trap 'echo "MINIAS_SOURCE_GRAPH=FAIL arch=$ARCH command=$BASH_COMMAND" >&2' ERR
{
  printf '.text\n.globl entry\nentry:\n  %s\n' "$branch"
  printf '%s\n' '.set alias_forward, far' '.set alias_forward_plus, alias_forward+3'
  printf '%s\n' '.set branch_span, . - entry'
  printf '%s\n' '.if . - .' '  .error "unreachable source conditional"' '.else'
  printf '%s\n' '.macro nested_nop' "  $nop" '.endm'
  printf '%s\n' '.macro many_nops count' '  .rept \count' '    nested_nop' '  .endr' '.endm' '.endif'
  printf 'many_nops %d\nfar:\n  %s\n' "$repetitions" "$nop"
  printf '%s\n' '.data' '.globl data_record' 'data_record:'
  printf '%s\n' '.irp value,1,2,3' '  .byte \value ; .byte \value' '.endr'
  printf '%s\n' '.long branch_span' '.long alias_forward_plus-far'
} >"$work/source.s"

if ! MINIAS_TRACE_RELAYOUT=1 timeout 20s "$MINIAS" "${mini[@]}" \
    -o "$work/mini.o" "$work/source.s" 2>"$work/mini.trace"; then
  sed -n '1,40p' "$work/mini.trace" >&2
  exit 1
fi
grep -qx 'minias-source-reparse:pass=1' "$work/mini.trace"
grep -Eq '^minias-source-graph:file-reads=1 sites=[0-9]+ expansions=[0-9]+ replay-passes=1 site-drifts=0 conditional-changes=0$' "$work/mini.trace"
mini_span=$(readelf -sW "$work/mini.o" | awk '$NF == "branch_span" { print $2 }')
test -n "$mini_span" && test "$((16#$mini_span))" -eq "$expected_span"

oracle='NOT_TESTED:explicit_local_no_cross_GNU'
if ((${#gnu[@]})); then
  "${gnu[@]}" -o "$work/gnu.o" "$work/source.s"
  for sec in .text .data; do
    "${copy[@]}" -j "$sec" -O binary "$work/mini.o" "$work/mini.${sec}.bin"
    "${copy[@]}" -j "$sec" -O binary "$work/gnu.o" "$work/gnu.${sec}.bin"
    cmp "$work/mini.${sec}.bin" "$work/gnu.${sec}.bin"
  done
  gnu_span=$(readelf -sW "$work/gnu.o" | awk '$NF == "branch_span" { print $2 }')
  test "$gnu_span" = "$mini_span"
  python3 - "$work/mini.o" "$work/gnu.o" <<'PY'
import re
import subprocess
import sys
names = {'entry', 'alias_forward', 'alias_forward_plus', 'branch_span',
         'far', 'data_record'}
def symbols(path):
    result=[]
    dump=subprocess.check_output(['readelf','-sW',path],text=True)
    for line in dump.splitlines():
        fields=line.split()
        if len(fields)>=8 and re.fullmatch(r'[0-9]+:',fields[0]) and fields[7] in names:
            result.append((fields[7],fields[1],fields[3],fields[4]))
    return result
mini,gnu=symbols(sys.argv[1]),symbols(sys.argv[2])
if mini!=gnu:
    raise SystemExit(f'ELF ordered symbol plan differs: MiniAS={mini} GNU={gnu}')
if {name for name,*_ in mini}!=names:
    raise SystemExit(f'missing GNU alias/entry names: {mini}')
PY
  oracle='exact'
fi

# A parse error after valid nested SourceFrame expansions must not emit an
# object. The SourceGraph is transient to this invocation, not an ELF cache.
{
  printf '.text\n.macro ok\n  %s\n.endm\nok\n' "$nop"
  printf '%s\n' '.irp n,1,2' '  .byte \n' '.endr' '.this_is_not_an_instruction'
} >"$work/invalid.s"
if timeout 20s "$MINIAS" "${mini[@]}" -o "$work/invalid.o" \
    "$work/invalid.s" 2>"$work/invalid.err"; then
  echo 'accepted malformed SourceFrame after valid expansion' >&2; exit 1
fi
test ! -e "$work/invalid.o"

echo "MINIAS_SOURCE_GRAPH=PASS arch=$ARCH physical_reads=1 replay=1 site_drift=0 nested_macro_rept_irp_semicolon=1 GNU_bytes_symbols=$oracle invalid_no_object=1"
