#!/usr/bin/env bash
# Real GCC-produced debug data must remain consumable after MiniAS + GNU ld.
# Check *linked* address-to-source semantics (not only ET_REL relocations),
# and compare the entire decoded line program with GNU GAS for the SAME .s.
set -Eeuo pipefail

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
: "${MINIAS:?Point MINIAS to the exact-head minic-as binary}"
for tool in gcc as readelf addr2line nm cmp awk sed grep diff mktemp; do
    command -v "$tool" >/dev/null || {
        echo "MINIAS_GCC_DWARF_E2E=BLOCKED missing=$tool" >&2
        exit 2
    }
done
test -x "$MINIAS"

probe="$repo/tests/external/linux/runtime_initramfs/probe.c"
expected_main=$(awk '/^int main\(void\)/ {print NR;exit}' "$probe")
expected_eventfd=$(awk '/^static void test_eventfd\(void\)/ {print NR;exit}' "$probe")
test -n "$expected_main" && test -n "$expected_eventfd"
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

source_position() {
    local image=$1 symbol=$2 source_line=$3
    local pc result function location
    pc=$(nm "$image" | awk -v name="$symbol" '$3 == name {print "0x" $1;exit}')
    test -n "$pc" || {
        echo "MINIAS_GCC_DWARF_E2E=FAIL missing-symbol=$symbol image=$image" >&2
        exit 1
    }
    result=$(addr2line -f -e "$image" "$pc")
    function=$(printf '%s\n' "$result" | sed -n '1p')
    location=$(printf '%s\n' "$result" | sed -n '2p')
    if [[ $function != "$symbol" || $location != *"probe.c:$source_line" ]]; then
        echo "MINIAS_GCC_DWARF_E2E=FAIL wrong-source-symbol=$symbol function=$function location=$location" >&2
        exit 1
    fi
}

for profile in unoptimized-v4 optimized-v5-views; do
    case "$profile" in
        unoptimized-v4) debug_flags=(-O0 -g -gdwarf-4) ;;
        optimized-v5-views)
            debug_flags=(-O2 -g -gdwarf-5 -gvariable-location-views) ;;
    esac
    gcc "${debug_flags[@]}" -fno-pie -fno-pic -S \
        -o "$work/$profile.s" "$probe"
    grep -q '^[[:space:]]*\.loc[[:space:]]' "$work/$profile.s"
    grep -q '^[[:space:]]*\.cfi_startproc' "$work/$profile.s"
    if [[ $profile == optimized-v5-views ]]; then
        grep -Eq '\.loc .*view \.LVU[0-9]+' "$work/$profile.s"
    fi

    as -o "$work/$profile.gnu.o" "$work/$profile.s"
    "$MINIAS" --target=x86_64 -o "$work/$profile.mini.o" "$work/$profile.s"
    for producer in gnu mini; do
        gcc -no-pie -o "$work/$profile.$producer.elf" \
            "$work/$profile.$producer.o"
        image="$work/$profile.$producer.elf"
        readelf -S "$image" | grep -q '[[:space:]]\.debug_line[[:space:]]'
        readelf -S "$image" | grep -q '[[:space:]]\.debug_info[[:space:]]'
        readelf -S "$image" | grep -q '[[:space:]]\.eh_frame[[:space:]]'
        readelf --debug-dump=decodedline "$image" >"$work/$profile.$producer.rows"
        readelf --debug-dump=frames "$image" >"$work/$profile.$producer.frames"
        grep -q ' FDE ' "$work/$profile.$producer.frames"
        source_position "$image" main "$expected_main"
        if [[ $profile == unoptimized-v4 ]]; then
            source_position "$image" test_eventfd "$expected_eventfd"
        fi
    done
    if ! cmp -s "$work/$profile.gnu.rows" "$work/$profile.mini.rows"; then
        echo "MINIAS_GCC_DWARF_E2E=FAIL decoded-line-mismatch profile=$profile" >&2
        diff -u "$work/$profile.gnu.rows" "$work/$profile.mini.rows" | head -80 >&2 || true
        exit 1
    fi
    echo "MINIAS_GCC_DWARF_E2E=PASS profile=$profile linked=GNU_and_MiniAS addr2line=exact decoded_lines=exact frame_FDE=valid"
done
