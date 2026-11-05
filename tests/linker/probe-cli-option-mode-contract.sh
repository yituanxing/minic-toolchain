#!/usr/bin/env bash
# Assert GNU static -S semantics and fail-closed options for non-owning modes.
# No kernel source rebuild: single x86 ET_REL fixture, GNU BFD differential.
set -Eeuo pipefail
: "${MINILD:?set MINILD to the exact-head minic-ld}"
for tool in as ld readelf; do
    command -v "$tool" >/dev/null || exit 8
done
work=$(mktemp -d "${TMPDIR:-/tmp}/minild-cli-options.XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT

printf '%s\n' \
    '.text' '.globl _start' '_start:' \
    ' mov $60,%eax' ' xor %edi,%edi' ' syscall' \
    '.section .debug_info,"",@progbits' \
    '.byte 1,2,3,4' \
    '.section .debug_line,"",@progbits' \
    '.byte 5,6,7,8' >"$work/start.s"
as -o "$work/start.o" "$work/start.s"

for kind in default stripped; do
    flags=()
    [[ $kind == default ]] || flags=(-S)
    for owner in gnu mini; do
        linker=ld
        [[ $owner == gnu ]] || linker=$MINILD
        "$linker" -m elf_x86_64 -static -e _start "${flags[@]}" \
            -o "$work/$kind-$owner.elf" "$work/start.o" \
            >"$work/$kind-$owner.log" 2>&1 || {
                echo "MINILD_CLI_OPTION_MODE=FAIL stage=$kind owner=$owner" >&2
                exit 1
            }
        "$work/$kind-$owner.elf" || exit 1
        if readelf -SW "$work/$kind-$owner.elf" |
            grep -Eq '[[:space:]]\.debug_(info|line)[[:space:]]'; then
            [[ $kind == default ]] || {
                echo "MINILD_CLI_OPTION_MODE=FAIL retained_debug=$kind:$owner" >&2
                exit 1
            }
        else
            [[ $kind == stripped ]] || {
                echo "MINILD_CLI_OPTION_MODE=FAIL missing_debug=$kind:$owner" >&2
                exit 1
            }
        fi
    done
    echo "MINILD_CLI_OPTION_MODE=PASS stage=$kind gnu_runtime=PASS mini_runtime=PASS"
done

# These options were once parsed without a destination in the selected
# output backend. If there is no owner, reject rather than claim success.
expect_reject() {
    local stage=$1
    shift
    if "$MINILD" -m elf_x86_64 -o "$work/$stage.elf" "$work/start.o" \
        "$@" >"$work/$stage.log" 2>&1; then
        echo "MINILD_CLI_OPTION_MODE=FAIL silently_accepted=$stage" >&2
        exit 1
    fi
    echo "MINILD_CLI_OPTION_MODE=PASS stage=$stage reject=PASS"
}

expect_reject reloc_script -r -T /dev/null
expect_reject reloc_strip -r -S
expect_reject reloc_build_id -r --build-id=sha1
expect_reject reloc_entry -r -e _start
expect_reject reloc_orphans -r --orphan-handling=error
expect_reject static_soname -static --soname=libfoo.so
expect_reject static_needed -static --needed=libfoo.so
expect_reject static_dynamic_list -static --dynamic-list=/dev/null
expect_reject multiple_needed -shared --needed=liba.so --needed=libb.so
expect_reject shared_build_id -shared --build-id=sha1
expect_reject shared_nodefaultlib -shared -z nodefaultlib
expect_reject shared_noexecstack -shared -z noexecstack

echo 'MINILD_CLI_OPTION_MODE=PASS stages=14 GNU_BFD=required'
