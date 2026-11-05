#!/usr/bin/env bash
# GNU/BFD removes empty script-only output sections, but their `.` symbols
# retain section provenance. Numeric VMA proximity is not the rule: a live
# successor can explicitly start at 0 while the empty output's dot is high.
set -Eeuo pipefail
: "${MINILD:?MINILD must identify the linker under test}"
for tool in as ld nm readelf; do command -v "$tool" >/dev/null; done

work=$(mktemp -d "${TMPDIR:-/tmp}/minild-empty-script-owner.XXXXXXXX")
trap 'rm -r -- "$work"' EXIT

for variant in ordinary tls; do
    case $variant in
        ordinary)
            assembly=tests/linker/fixtures/script-empty-successor.S
            script=tests/linker/fixtures/script-empty-successor.lds
            names='empty_start absolute_start top_before tail_empty'
            expected='absolute_start:A empty_start:D top_before:D tail_empty:T'
            ;;
        tls)
            assembly=tests/linker/fixtures/script-empty-successor-tls.S
            script=tests/linker/fixtures/script-empty-successor-tls.lds
            names='before_tls top_before_tls after_tls'
            expected='before_tls:T top_before_tls:T after_tls:T'
            ;;
    esac
    as -o "$work/$variant.o" "$assembly"
    ld -m elf_x86_64 -T "$script" -o "$work/$variant.gnu.elf" \
        "$work/$variant.o"
    "$MINILD" -m elf_x86_64 -T "$script" -o "$work/$variant.mini.elf" \
        "$work/$variant.o" >"$work/$variant.mini.stdout" \
        2>"$work/$variant.mini.stderr"

    for linker in gnu mini; do
        nm -n "$work/$variant.$linker.elf" | \
            awk -v selected="$names" '
                BEGIN { split(selected, names, " "); for (i in names) wanted[names[i]]=1 }
                $3 in wanted {print $1, $2, $3}
            ' | LC_ALL=C sort >"$work/$variant.$linker.symbols"
        count=$(wc -l <"$work/$variant.$linker.symbols")
        wanted_count=$(wc -w <<<"$names")
        if [[ $count -ne $wanted_count ]]; then
            echo "MINILD_SCRIPT_EMPTY_OWNER=FAIL missing:$variant:$linker" >&2
            exit 1
        fi
        for binding in $expected; do
            name=${binding%:*}
            type=${binding#*:}
            if ! awk -v name="$name" -v type="$type" \
                '$2==type && $3==name{found=1} END{exit !found}' \
                "$work/$variant.$linker.symbols"; then
                echo "MINILD_SCRIPT_EMPTY_OWNER=FAIL type:$variant:$linker:$binding" >&2
                exit 1
            fi
        done
    done
    if ! diff -u "$work/$variant.gnu.symbols" \
        "$work/$variant.mini.symbols"; then
        echo "MINILD_SCRIPT_EMPTY_OWNER=FAIL GNU_difference:$variant" >&2
        exit 1
    fi
done

echo 'MINILD_SCRIPT_EMPTY_OWNER=PASS GNU_BFD_successor_across_VMA=2 top_level=1 trailing_previous=1 TLS_skip_previous=3 ABSOLUTE_remains_ABS=1'
