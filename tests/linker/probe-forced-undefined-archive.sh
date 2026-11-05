#!/usr/bin/env bash
# GNU -u/--undefined must seed the common symbol resolver before lazy archive
# extraction. Check every CLI spelling, GC root semantics, and fail-closed modes.
set -Eeuo pipefail
: "${MINILD:?MINILD required}"
for tool in as ar ld nm readelf; do command -v "$tool" >/dev/null; done
work=$(mktemp -d)
trap 'rm -r -- "$work"' EXIT

printf '%s\n' '.globl _start' '_start:' 'xor %edi, %edi' \
  'mov $60, %eax' 'syscall' >"$work/main.s"
printf '%s\n' '.section .text.force,"ax",@progbits' \
  '.globl force_anchor' 'force_anchor:' 'ret' >"$work/force.s"
as -o "$work/main.o" "$work/main.s"
as -o "$work/force.o" "$work/force.s"
ar rcs "$work/libforce.a" "$work/force.o"

for linker in ld "$MINILD"; do
  if [[ $linker == ld ]]; then owner=gnu; else owner=mini; fi
  "$linker" -m elf_x86_64 -o "$work/$owner.no-force" \
    "$work/main.o" "$work/libforce.a"
  if nm "$work/$owner.no-force" | grep -Eq '[[:space:]]force_anchor$'; then
    echo "MINILD_FORCED_UNDEFINED=FAIL archive_extracted_without_u:$owner" >&2
    exit 1
  fi

  for style in split compact long equals duplicate gc missing; do
    argv=()
    case "$style" in
      split) argv=(-u force_anchor) ;;
      compact) argv=(-uforce_anchor) ;;
      long) argv=(--undefined force_anchor) ;;
      equals) argv=(--undefined=force_anchor) ;;
      duplicate) argv=(-uforce_anchor --undefined=force_anchor -u force_anchor) ;;
      gc) argv=(--gc-sections -u force_anchor) ;;
      missing) argv=(-u non_existing_undefined_symbol) ;;
    esac
    "$linker" -m elf_x86_64 -o "$work/$owner.$style" \
      "${argv[@]}" "$work/main.o" "$work/libforce.a"
    if [[ $style == missing ]]; then
      nm -u "$work/$owner.$style" | grep -Fq non_existing_undefined_symbol || {
        echo "MINILD_FORCED_UNDEFINED=FAIL missing_unresolved_symtab:$owner" >&2
        exit 1
      }
      continue
    fi
    nm "$work/$owner.$style" | grep -Eq '[[:space:]][Tt][[:space:]]force_anchor$' || {
      echo "MINILD_FORCED_UNDEFINED=FAIL missing_extracted_symbol:$owner:$style" >&2
      exit 1
    }
    if [[ $style == gc ]]; then
      # A forced archive member must survive --gc-sections without callers.
      readelf -W -s "$work/$owner.$style" | grep -Eq 'force_anchor$' || {
        echo "MINILD_FORCED_UNDEFINED=FAIL gc_dropped_forced_root:$owner" >&2
        exit 1
      }
    fi
  done
done

for bad in '-u' '--undefined' '--undefined=' '-u -static'; do
  read -r -a argv <<<"$bad"
  if "$MINILD" -m elf_x86_64 -o "$work/bad" \
      "$work/main.o" "$work/libforce.a" "${argv[@]}" \
      >"$work/bad.stdout" 2>"$work/bad.stderr"; then
    echo "MINILD_FORCED_UNDEFINED=FAIL accepted_invalid_option:$bad" >&2
    exit 1
  fi
  grep -Eq 'missing-forced-undefined-symbol|invalid-forced-undefined-symbol' \
    "$work/bad.stderr"
  [[ ! -e "$work/bad" ]]
done
for mode in -r -shared -pie; do
  if "$MINILD" -m elf_x86_64 "$mode" -uforce_anchor \
      -o "$work/badmode" "$work/main.o" "$work/libforce.a" \
      >"$work/badmode.stdout" 2>"$work/badmode.stderr"; then
    echo "MINILD_FORCED_UNDEFINED=FAIL silently_ignored_u:$mode" >&2
    exit 1
  fi
  grep -Fq -- '-u-requires-final-static-link' "$work/badmode.stderr"
  [[ ! -e "$work/badmode" ]]
done

echo 'MINILD_FORCED_UNDEFINED=PASS GNU_BFD_parity=true archive_lazy_extraction=true four_spellings=true duplicate=true undefined_without_reference=true gc_root=true unsupported_modes_fail_closed=true'
