#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
riscv_cc=${RISCV_CC:-riscv64-buildroot-linux-musl-gcc}
qemu=${QEMU_RISCV64:-qemu-riscv64}
riscv_objdump=${RISCV_OBJDUMP:-}
work=${BUILD_DIR:-"$root/build/debug"}/tests/programs-c0
manifest="$root/tests/programs/c0/manifest.txt"
programs=

inventory_error() {
    printf '%s\n' "FAIL programs/c0 inventory: $1" >&2
    exit 1
}

load_manifest() {
    if test ! -f "$manifest"; then
        inventory_error "missing manifest $manifest"
    fi

    while IFS= read -r name || test -n "$name"; do
        case "$name" in
        ''|'#'*)
            continue
            ;;
        *[!a-z0-9_]*)
            inventory_error "invalid program name '$name'"
            ;;
        esac

        case " $programs " in
        *" $name "*)
            inventory_error "duplicate program '$name'"
            ;;
        esac
        if test ! -f "$root/tests/programs/c0/$name.c"; then
            inventory_error "manifest entry '$name' has no source file"
        fi
        programs="$programs $name"
    done <"$manifest"

    if test -z "$programs"; then
        inventory_error "manifest contains no programs"
    fi

    for source in "$root"/tests/programs/c0/*.c; do
        test -e "$source" || continue
        source_name=${source##*/}
        source_name=${source_name%.c}
        case " $programs " in
        *" $source_name "*)
            ;;
        *)
            inventory_error "source '$source_name.c' is not listed in manifest"
            ;;
        esac
    done
}

load_manifest

if ! command -v "$riscv_cc" >/dev/null 2>&1 ||
   ! command -v "$qemu" >/dev/null 2>&1; then
    printf '%s\n' "SKIP programs/c0: set RISCV_CC and QEMU_RISCV64"
    test "${REQUIRE_RISCV_RUNTIME:-0}" != 1
    exit
fi

if test -z "$riscv_objdump"; then
    candidate=${riscv_cc%gcc}objdump
    if command -v "$candidate" >/dev/null 2>&1; then
        riscv_objdump=$candidate
    fi
fi

mkdir -p "$work"

run_elf() {
    elf=$1
    stdout_file=$2
    stderr_file=$3
    status_file=$4

    set +e
    "$qemu" "$elf" >"$stdout_file" 2>"$stderr_file"
    status=$?
    set -e
    printf '%s\n' "$status" >"$status_file"
}

write_disassembly() {
    elf=$1
    output=$2

    if test -n "$riscv_objdump"; then
        "$riscv_objdump" -dr "$elf" >"$output" 2>&1 || true
    fi
}

report_difference() {
    name=$1
    kind=$2
    gcc_file=$3
    minic_file=$4

    printf '%s\n' "FAIL programs/c0/$name: $kind differs" >&2
    diff -u "$gcc_file" "$minic_file" >&2 || true
    printf '%s\n' "--- MiniC assembly: $name.minic.s ---" >&2
    sed -n '1,260p' "$work/$name.minic.s" >&2 || true
    write_disassembly "$work/$name.gcc.elf" "$work/$name.gcc.disasm"
    write_disassembly "$work/$name.minic.elf" "$work/$name.minic.disasm"
    if test -f "$work/$name.gcc.disasm"; then
        printf '%s\n' "--- GCC disassembly: $name.gcc.elf ---" >&2
        sed -n '1,260p' "$work/$name.gcc.disasm" >&2 || true
    fi
    if test -f "$work/$name.minic.disasm"; then
        printf '%s\n' "--- MiniC disassembly: $name.minic.elf ---" >&2
        sed -n '1,260p' "$work/$name.minic.disasm" >&2 || true
    fi
    printf '%s\n' "Artifacts retained in $work" >&2
    exit 1
}

run_program() {
    name=$1
    source="$root/tests/programs/c0/$name.c"

    "$riscv_cc" -std=c11 -O0 -static "$source" -o "$work/$name.gcc.elf"
    "$riscv_cc" -std=c11 -E -P -x c "$source" -o "$work/$name.i"
    "$minic" -S "$work/$name.i" -o "$work/$name.minic.s"
    "$riscv_cc" -static "$work/$name.minic.s" -o "$work/$name.minic.elf"

    run_elf \
        "$work/$name.gcc.elf" \
        "$work/$name.gcc.stdout" \
        "$work/$name.gcc.stderr" \
        "$work/$name.gcc.status"
    run_elf \
        "$work/$name.minic.elf" \
        "$work/$name.minic.stdout" \
        "$work/$name.minic.stderr" \
        "$work/$name.minic.status"

    if ! cmp -s "$work/$name.gcc.status" "$work/$name.minic.status"; then
        report_difference \
            "$name" "exit status" \
            "$work/$name.gcc.status" "$work/$name.minic.status"
    fi
    if ! cmp -s "$work/$name.gcc.stdout" "$work/$name.minic.stdout"; then
        report_difference \
            "$name" "standard output" \
            "$work/$name.gcc.stdout" "$work/$name.minic.stdout"
    fi
    if ! cmp -s "$work/$name.gcc.stderr" "$work/$name.minic.stderr"; then
        report_difference \
            "$name" "standard error" \
            "$work/$name.gcc.stderr" "$work/$name.minic.stderr"
    fi

    status=$(cat "$work/$name.minic.status")
    stdout_bytes=$(wc -c <"$work/$name.minic.stdout" | tr -d ' ')
    stderr_bytes=$(wc -c <"$work/$name.minic.stderr" | tr -d ' ')
    printf '%s\n' \
        "PASS programs/c0/$name exit=$status stdout=$stdout_bytes stderr=$stderr_bytes"
}

for name in $programs; do
    run_program "$name"
done
