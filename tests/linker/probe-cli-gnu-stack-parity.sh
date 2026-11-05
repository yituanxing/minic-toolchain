#!/usr/bin/env bash
# GNU BFD output oracle for default/executable/explicit stack policy.
# No Linux objects are recompiled; tests both ET_EXEC program headers and
# GNU's synthesized ET_REL .note.GNU-stack section.
set -Eeuo pipefail
: "${MINILD:?set MINILD to the exact-head minic-ld}"
: "${MINIAS:?set MINIAS to the exact-head minic-as}"
for tool in as ld readelf; do
    command -v "$tool" >/dev/null || exit 8
done
work=$(mktemp -d "${TMPDIR:-/tmp}/minild-gnu-stack.XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT

for kind in absent nonexec exec; do
    printf '%s\n' \
        '.text' '.globl _start' '_start:' \
        ' mov $60,%eax' ' xor %edi,%edi' ' syscall' >"$work/$kind.s"
    if [[ $kind == nonexec ]]; then
        printf '%s\n' '.section .note.GNU-stack,"",@progbits' \
            >>"$work/$kind.s"
    elif [[ $kind == exec ]]; then
        printf '%s\n' '.section .note.GNU-stack,"x",@progbits' \
            >>"$work/$kind.s"
    fi
    as -o "$work/$kind.o" "$work/$kind.s"
done

printf '%s\n' \
    'SECTIONS { . = 0x401000; .text : { *(.text) } }' \
    >"$work/sections.lds"
printf '%s\n' \
    'PHDRS { text PT_LOAD FLAGS(5); }' \
    'SECTIONS { . = 0x401000; .text : { *(.text) } :text }' \
    >"$work/phdrs.lds"

stack_header() {
    readelf -lW "$1" | awk '$1=="GNU_STACK" {print $7 ":" $8}'
}

check_static() {
    local stage=$1 object=$2 expected=$3
    shift 3
    local gnu_stack mini_stack owner linker
    for owner in gnu mini; do
        linker=ld
        [[ $owner == gnu ]] || linker=$MINILD
        "$linker" -m elf_x86_64 -static -e _start -o \
            "$work/$stage-$owner.elf" "$work/$object.o" "$@" \
            >"$work/$stage-$owner.log" 2>&1 || {
                echo "MINILD_GNU_STACK=FAIL stage=$stage owner=$owner" >&2
                exit 1
            }
        "$work/$stage-$owner.elf" || exit 1
        if readelf -SW "$work/$stage-$owner.elf" |
            grep -Eq '[[:space:]]\.note\.GNU-stack[[:space:]]'; then
            echo "MINILD_GNU_STACK=FAIL stage=$stage owner=$owner consumed_note_leaked=true" >&2
            exit 1
        fi
    done
    gnu_stack=$(stack_header "$work/$stage-gnu.elf")
    mini_stack=$(stack_header "$work/$stage-mini.elf")
    if [[ $gnu_stack != "$expected" || $mini_stack != "$gnu_stack" ]]; then
        echo "MINILD_GNU_STACK=FAIL stage=$stage expected=$expected gnu=$gnu_stack mini=$mini_stack" >&2
        exit 1
    fi
    echo "MINILD_GNU_STACK=PASS stage=$stage stack=${expected:-absent} runtime=2/2"
}

check_static plain_no_note absent ''
check_static note_nonexec nonexec RW:0x10
check_static note_exec exec RWE:0x10
check_static forced_no_note absent RW:0x10 -z noexecstack
check_static forced_exec_note exec RW:0x10 -z noexecstack
check_static plain_script absent RW:0x10 -z noexecstack -T "$work/sections.lds"
check_static explicit_phdrs absent '' -z noexecstack -T "$work/phdrs.lds"

for owner in gnu mini; do
    linker=ld
    [[ $owner == gnu ]] || linker=$MINILD
    "$linker" -m elf_x86_64 -r -z noexecstack -e _start \
        -o "$work/relo-$owner.o" "$work/absent.o" \
        >"$work/relo-$owner.log" 2>&1 || {
            echo "MINILD_GNU_STACK=FAIL stage=relo owner=$owner" >&2
            exit 1
        }
    readelf -h "$work/relo-$owner.o" | grep -q 'Entry point address:.*0x0'
    test "$(readelf -SW "$work/relo-$owner.o" |
        grep -Ec '[[:space:]]\.note\.GNU-stack[[:space:]]')" = 1 || {
        echo "MINILD_GNU_STACK=FAIL stage=relo missing_note=$owner" >&2
        exit 1
    }
done
echo 'MINILD_GNU_STACK=PASS stage=relo synthesized_note=GNU_and_mini entry=0'

# An existing note keeps its original section flags in ET_REL, even when an
# explicit final-image policy would make the later stack non-executable.
for owner in gnu mini; do
    linker=ld
    [[ $owner == gnu ]] || linker=$MINILD
    "$linker" -m elf_x86_64 -r -z noexecstack \
        -o "$work/reloexec-$owner.o" "$work/exec.o" \
        >"$work/reloexec-$owner.log" 2>&1
    readelf -SW "$work/reloexec-$owner.o" |
        grep -Eq '\.note\.GNU-stack[[:space:]]+PROGBITS.*[[:space:]]X[[:space:]]'
done
echo 'MINILD_GNU_STACK=PASS stage=relo_existing_note executable_input_flag=preserved'

# The same state/options path must emit correct class-neutral headers for
# every Linux target, including ELF32 RV32. GNU cross tools are not needed.
for target_spec in \
    'rv32:riscv:rv32i:ilp32:elf32lriscv' \
    'rv64:riscv:rv64i:lp64:elf64lriscv' \
    'x86_64:x86_64:x86-64:sysv:elf_x86_64' \
    'aarch64:aarch64:armv8-a:lp64:aarch64elf' \
    'loongarch64:loongarch64:loongarch64:lp64d:elf64loongarch'; do
    IFS=: read -r arch target march mabi emulation <<<"$target_spec"
    printf '%s\n' \
        '.section .text.entry,"ax",@progbits' \
        '.globl _start' '_start:' \
        ' .byte 0,0,0,0' >"$work/$arch.s"
    "$MINIAS" "--target=$target" "-march=$march" "-mabi=$mabi" \
        -o "$work/$arch.o" "$work/$arch.s"
    "$MINILD" -m "$emulation" -static -z noexecstack -e _start \
        -o "$work/$arch-static.elf" "$work/$arch.o"
    test "$(stack_header "$work/$arch-static.elf")" = RW:0x10 || {
        echo "MINILD_GNU_STACK=FAIL stage=multitarget_static arch=$arch" >&2
        exit 1
    }
    "$MINILD" -m "$emulation" -r -z noexecstack \
        -o "$work/$arch-rel.o" "$work/$arch.o"
    readelf -SW "$work/$arch-rel.o" |
        grep -Eq '[[:space:]]\.note\.GNU-stack[[:space:]]' || {
            echo "MINILD_GNU_STACK=FAIL stage=multitarget_reloc arch=$arch" >&2
            exit 1
        }
    echo "MINILD_GNU_STACK=PASS stage=multitarget arch=$arch static=RW reloc_note=true"
done

"$MINILD" -m elf64lriscv -shared -z noexecstack \
    -o "$work/rv64-shared.so" "$work/rv64.o"
"$MINILD" -m elf64lriscv -pie -z noexecstack \
    -o "$work/rv64-pie.elf" "$work/rv64.o"
for mode in shared pie; do
    output="$work/rv64-pie.elf"
    [[ $mode == pie ]] || output="$work/rv64-shared.so"
    [[ $(stack_header "$output") == RW:0x10 ]] || {
        echo "MINILD_GNU_STACK=FAIL stage=rv64_dynamic mode=$mode" >&2
        exit 1
    }
done
echo 'MINILD_GNU_STACK=PASS stage=rv64_dynamic shared=RW pie=RW'
echo 'MINILD_GNU_STACK=PASS GNU_BFD_cases=9 mini_multitarget=5 dynamic_modes=2'
