#!/usr/bin/env bash
# GNU BFD parity for independent group membership and whole-archive flags.
# Uses real archive extraction and native ET_EXEC, without Linux C rebuild.
set -Eeuo pipefail
: "${MINILD:?set MINILD to the exact-head minic-ld}"
for tool in as ar ld nm; do
    command -v "$tool" >/dev/null || exit 8
done
work=$(mktemp -d "${TMPDIR:-/tmp}/minild-cli-groups.XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT

printf '%s\n' \
    '.text' '.globl _start' '_start:' \
    ' call foo' ' mov $60,%eax' ' xor %edi,%edi' ' syscall' >"$work/start.s"
printf '%s\n' \
    '.text' '.globl foo' 'foo:' ' call bar' ' ret' >"$work/foo.s"
printf '%s\n' \
    '.text' '.globl bar' 'bar:' ' call baz' ' ret' >"$work/bar.s"
printf '%s\n' \
    '.text' '.globl baz' 'baz:' ' ret' >"$work/baz.s"
printf '%s\n' \
    '.text' '.globl sentinel' 'sentinel:' ' ret' >"$work/sentinel.s"
printf '%s\n' \
    '.text' '.globl extra' 'extra:' ' ret' >"$work/extra.s"
for name in start foo bar baz sentinel extra; do
    as -o "$work/$name.o" "$work/$name.s"
done
ar rcsD "$work/libA.a" "$work/foo.o" "$work/baz.o" "$work/sentinel.o"
ar rcsD "$work/libB.a" "$work/bar.o"

expect_ok() {
    local stage=$1
    shift
    for owner in gnu mini; do
        local linker=ld
        [[ $owner == gnu ]] || linker=$MINILD
        "$linker" -m elf_x86_64 -static -e _start \
            -o "$work/$stage-$owner.elf" "$work/start.o" "$@" \
            >"$work/$stage-$owner.log" 2>&1 || {
                echo "MINILD_CLI_ARCHIVE_GROUP=FAIL stage=$stage owner=$owner" >&2
                exit 1
            }
        "$work/$stage-$owner.elf" || exit 1
    done
    echo "MINILD_CLI_ARCHIVE_GROUP=PASS stage=$stage gnu_runtime=PASS mini_runtime=PASS"
}

expect_fail() {
    local stage=$1
    shift
    for owner in gnu mini; do
        local linker=ld
        [[ $owner == gnu ]] || linker=$MINILD
        if "$linker" -m elf_x86_64 -static -e _start \
            -o "$work/$stage-$owner.elf" "$work/start.o" "$@" \
            >"$work/$stage-$owner.log" 2>&1; then
            echo "MINILD_CLI_ARCHIVE_GROUP=FAIL unexpected_link=$stage:$owner" >&2
            exit 1
        fi
    done
    echo "MINILD_CLI_ARCHIVE_GROUP=PASS stage=$stage gnu_reject=PASS mini_reject=PASS"
}

# A -> B -> A requires a revisit of libA after libB introduces baz.
expect_fail no_group "$work/libA.a" "$work/libB.a"
expect_ok explicit_group --start-group "$work/libA.a" "$work/libB.a" --end-group
expect_ok searched_group -L "$work" --start-group -lA -lB --end-group
expect_ok mixed_object_group --start-group "$work/libA.a" \
    "$work/extra.o" "$work/libB.a" --end-group

# Adjacent independent groups cannot accidentally become one scan domain.
expect_fail separate_groups \
    --start-group "$work/libA.a" --end-group \
    --start-group "$work/libB.a" --end-group

# Group and whole-archive are orthogonal: even an unreferenced sentinel must
# be included, with exactly the same semantics for explicit and -l inputs.
for mode in explicit searched; do
    if [[ $mode == explicit ]]; then
        args=(--start-group --whole-archive "$work/libA.a" \
              --no-whole-archive "$work/libB.a" --end-group)
    else
        args=(-L "$work" --start-group --whole-archive -lA \
              --no-whole-archive -lB --end-group)
    fi
    expect_ok "whole_$mode" "${args[@]}"
    for owner in gnu mini; do
        nm --defined-only "$work/whole_$mode-$owner.elf" |
            grep -Eq '[[:space:]]sentinel$' || {
                echo "MINILD_CLI_ARCHIVE_GROUP=FAIL missing_sentinel=$mode:$owner" >&2
                exit 1
            }
    done
done

for bad in --end-group --start-group; do
    if "$MINILD" -m elf_x86_64 -static -o "$work/invalid.elf" \
         "$work/start.o" "$bad" "$work/libA.a" "$work/libB.a" \
         >"$work/invalid.log" 2>&1; then
        echo "MINILD_CLI_ARCHIVE_GROUP=FAIL malformed_group_accepted=$bad" >&2
        exit 1
    fi
done
if "$MINILD" -m elf_x86_64 -static -o "$work/invalid.elf" \
     --start-group --end-group >"$work/invalid.log" 2>&1; then
    echo 'MINILD_CLI_ARCHIVE_GROUP=FAIL empty_group_accepted=true' >&2
    exit 1
fi
echo 'MINILD_CLI_ARCHIVE_GROUP=PASS stages=7 malformed_groups=3 GNU_BFD=required'
