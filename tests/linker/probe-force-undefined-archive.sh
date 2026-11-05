#!/usr/bin/env bash
# GNU -u/--undefined adds an unresolved strong symbol to the input graph.
# That demand must drive lazy regular/thin archive extraction and group
# fixed-point selection and a preexisting weak undef.  The current MiniLD
# -r path rejects forced undef explicitly until relocatable ABI supports it.
# GNU also applies -u when it occurs AFTER the archive on the CLI.
set -Eeuo pipefail

repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd -P)
mini=${MINILD:-$repo/build/release/bin/minic-ld}
[[ -x $mini ]] || { echo 'MINILD_FORCE_UNDEFINED=FAIL missing_MiniLD' >&2; exit 1; }
for command in gcc ld ar nm readelf timeout; do
    command -v "$command" >/dev/null || {
        echo "MINILD_FORCE_UNDEFINED=FAIL missing_tool=$command" >&2
        exit 1
    }
done
work=$(mktemp -d)
trap 'rm -r -- "$work"' EXIT

printf '%s\n' \
    '.section .text.start,"ax",@progbits' \
    '.globl _start' \
    '_start:' \
    '    xorl %edi,%edi' \
    '    movl $60,%eax' \
    '    syscall' \
    '.weak weak_forced' >"$work/start.s"
printf '%s\n' \
    '.section .text.forced,"ax",@progbits' \
    '.globl forced' \
    'forced:' \
    '    ret' \
    '.globl weak_forced' \
    'weak_forced:' \
    '    ret' >"$work/forced.s"
printf '%s\n' \
    '.section .text.unused,"ax",@progbits' \
    '.globl never_selected' \
    'never_selected:' \
    '    ret' >"$work/unused.s"
for name in start forced unused; do
    gcc -m64 -x assembler -c "$work/$name.s" -o "$work/$name.o"
done
ar crs "$work/libchoice.a" "$work/forced.o" "$work/unused.o"
ar crsT "$work/libchoice-thin.a" "$work/forced.o" "$work/unused.o"

compare() {
    local name=$1
    local output_mode=$2
    shift 2
    local tool
    for tool in gnu mini; do
        local linker=ld
        [[ $tool == mini ]] && linker=$mini
        "$linker" -m elf_x86_64 "$output_mode" -o "$work/$name.$tool" "$@" \
            >"$work/$name.$tool.log" 2>&1 || {
            echo "MINILD_FORCE_UNDEFINED=FAIL link=$name:$tool" >&2
            tail -n 25 "$work/$name.$tool.log" >&2
            exit 1
        }
        nm "$work/$name.$tool" | grep -Eq ' [Tt] forced$' || {
            echo "MINILD_FORCE_UNDEFINED=FAIL archive_not_extracted=$name:$tool" >&2
            exit 1
        }
        if nm "$work/$name.$tool" | grep -q ' never_selected$'; then
            echo "MINILD_FORCE_UNDEFINED=FAIL unused_member_selected=$name:$tool" >&2
            exit 1
        fi
        if [[ $output_mode == -static ]]; then
            timeout 5 "$work/$name.$tool" || {
                echo "MINILD_FORCE_UNDEFINED=FAIL native_execution=$name:$tool" >&2
                exit 1
            }
        else
            readelf -h "$work/$name.$tool" | grep -Eq 'Type:[[:space:]]*REL' || {
                echo "MINILD_FORCE_UNDEFINED=FAIL not_ET_REL=$name:$tool" >&2
                exit 1
            }
        fi
    done
}

# Both GNU and MiniLD must resolve explicit symbol demand from .a and thin .a.
compare short_separate -static -u forced "$work/start.o" "$work/libchoice.a"
compare short_attached -static -uforced "$work/start.o" "$work/libchoice-thin.a"
compare trailing_force -static "$work/start.o" "$work/libchoice.a" -u forced
compare long_separate -static --undefined forced "$work/start.o" "$work/libchoice.a"
compare long_equals -static --undefined=forced "$work/start.o" "$work/libchoice.a"
compare weak_override -static -u weak_forced -u forced "$work/start.o" "$work/libchoice.a"
compare gc_root -static --gc-sections -u forced "$work/start.o" "$work/libchoice.a"

# A -u inside an archive group must not split its fixed-point scan; the
# archive that appears before the option still supplies the definition.
compare group_rescan -static "$work/start.o" \
    --start-group "$work/libchoice.a" -u forced --end-group

if "$mini" -m elf_x86_64 -r -u forced -o "$work/relocatable.elf" \
    "$work/start.o" "$work/libchoice.a" >"$work/relocatable.log" 2>&1; then
    echo 'MINILD_FORCE_UNDEFINED=FAIL relocatable_u_silently_admitted' >&2
    exit 1
fi
grep -Fq -- '-u-requires-final-static-link' "$work/relocatable.log"
[[ ! -e $work/relocatable.elf ]]

# Command-line demands that remain unsatisfied must obey GNU's final-link
# undefined-symbol policy; they are never silently treated as definitions.
gnu_missing=0
mini_missing=0
ld -m elf_x86_64 -static -u entirely_missing -o "$work/missing.gnu" \
    "$work/start.o" >"$work/missing.gnu.log" 2>&1 || gnu_missing=$?
"$mini" -m elf_x86_64 -static -u entirely_missing -o "$work/missing.mini" \
    "$work/start.o" >"$work/missing.mini.log" 2>&1 || mini_missing=$?
if (( (gnu_missing == 0) != (mini_missing == 0) )); then
    echo "MINILD_FORCE_UNDEFINED=FAIL unmatched_symbol_policy:gnu=$gnu_missing:mini=$mini_missing" >&2
    exit 1
fi

# Invalid empty form must fail before creating any ELF output.
if "$mini" -m elf_x86_64 -static --undefined= -o "$work/invalid.elf" \
    "$work/start.o" "$work/libchoice.a" >"$work/invalid.log" 2>&1; then
    echo 'MINILD_FORCE_UNDEFINED=FAIL empty_symbol_admitted' >&2
    exit 1
fi
grep -q 'invalid-forced-undefined-symbol' "$work/invalid.log"
[[ ! -e $work/invalid.elf ]] || {
    echo 'MINILD_FORCE_UNDEFINED=FAIL invalid_output_created' >&2
    exit 1
}

echo 'MINILD_FORCE_UNDEFINED=PASS GNU_parity=1 regular_archive=1 thin_archive=1 trailing_force=1 group_fixed_point=1 weak_override=1 gc_root=1 relocatable_fail_closed=1 unmatched_symbol_policy=1 unused_member_rejected=1 malformed_rejected=1'
