#!/usr/bin/env bash
# Reuse the exact same GNU-assembled objects for GNU BFD and MiniLD.  In a
# final static link, --no-undefined must reject a strong unresolved reference;
# -nostdlib only suppresses implicit library search directories.  This is a
# small native runtime oracle, not a claim about linking a complete kernel.
set -Eeuo pipefail

: "${MINILD:?MINILD must point to minic-ld}"
for bin in as ld objcopy readelf; do
    command -v "$bin" >/dev/null || {
        echo "MINILD_LINUX_CLI=INCONCLUSIVE missing=$bin" >&2
        exit 8
    }
done
test -x "$MINILD" || {
    echo 'MINILD_LINUX_CLI=INCONCLUSIVE minic-ld-not-executable' >&2
    exit 8
}

work=$(mktemp -d "${TMPDIR:-/tmp}/minild-linux-cli.XXXXXXXX")
finish() {
    if [[ ${MINILD_LINUX_CLI_KEEP:-0} == 1 ]]; then
        echo "MINILD_LINUX_CLI_ARTIFACTS=$work"
    else
        rm -r -- "$work"
    fi
}
trap finish EXIT

cat >"$work/good.s" <<'ASM'
.section .text.entry,"ax",@progbits
.globl _start
.type _start,@function
_start:
    mov $1,%rax
    mov $1,%rdi
    lea fixture_text(%rip),%rsi
    mov $13,%rdx
    syscall
    mov $60,%rax
    mov $42,%rdi
    syscall
.size _start,.-_start
.section .rodata.fixture,"a",@progbits
fixture_text:
    .ascii "CLI_GNU_PASS\n"
.section .data.fixture,"aw",@progbits
    .quad 0x1122334455667788
    .weak missing_weak
    .quad missing_weak
ASM
cat >"$work/bad.s" <<'ASM'
.section .text.entry,"ax",@progbits
.globl _start
_start:
    call missing_strong
    ret
ASM
as -o "$work/good.o" "$work/good.s"
as -o "$work/bad.o" "$work/bad.s"

ld -melf_x86_64 -nostdlib --no-undefined -e _start \
    -o "$work/gnu.elf" "$work/good.o"
"$MINILD" -melf_x86_64 -nostdlib --no-undefined -e _start \
    -o "$work/mini.elf" "$work/good.o"

for section in .rodata .data; do
    # GNU's default script folds .rodata.fixture/.data.fixture into output
    # .rodata/.data. MiniLD preserves their input names in its unscripted
    # ordinary ELF path; compare the actual same-source section payloads.
    objcopy --dump-section "$section=$work/gnu${section}.bin" "$work/gnu.elf"
    objcopy --dump-section "$section.fixture=$work/mini${section}.bin" \
        "$work/mini.elf"
    cmp "$work/gnu${section}.bin" "$work/mini${section}.bin"
done
test "$(wc -c <"$work/mini.data.bin")" -eq 16

for owner in gnu mini; do
    readelf -W -h "$work/$owner.elf" >"$work/$owner.header"
    grep -Eq 'Class:[[:space:]]+ELF64' "$work/$owner.header"
    grep -Eq 'Type:[[:space:]]+EXEC ' "$work/$owner.header"
    grep -Eq 'Machine:[[:space:]]+Advanced Micro Devices X86-64' \
        "$work/$owner.header"
    chmod +x "$work/$owner.elf"
    status=0
    "$work/$owner.elf" >"$work/$owner.stdout" 2>"$work/$owner.stderr" || status=$?
    test "$status" -eq 42 || {
        echo "MINILD_LINUX_CLI=FAIL runtime_${owner}_exit=$status" >&2
        exit 1
    }
    printf 'CLI_GNU_PASS\n' >"$work/expected.stdout"
    cmp "$work/expected.stdout" "$work/$owner.stdout"
done

for owner in gnu mini; do
    if [[ $owner == gnu ]]; then linker=ld; else linker=$MINILD; fi
    if "$linker" -melf_x86_64 -nostdlib --no-undefined \
        -e _start -o "$work/$owner.bad.elf" "$work/bad.o" \
        >"$work/$owner.bad.stdout" 2>"$work/$owner.bad.stderr"; then
        echo "MINILD_LINUX_CLI=FAIL ${owner}_accepted_strong_undefined" >&2
        exit 1
    fi
    grep -Eq '(undefined reference|undefined-reference)' \
        "$work/$owner.bad.stderr" || {
            echo "MINILD_LINUX_CLI=FAIL ${owner}_wrong_undefined_diagnostic" >&2
            sed -n '1,20p' "$work/$owner.bad.stderr" >&2
            exit 1
        }
done

# GNU's --no-undefined also applies to ET_DYN, but MiniLD does not yet have
# an equivalent strict policy for shared/PIE.  It must reject those options
# explicitly rather than accept and silently weaken GNU semantics.
for mode in -r -shared -pie; do
    label=${mode#-}
    if "$MINILD" -melf_x86_64 "$mode" --no-undefined \
        -o "$work/$label.elf" "$work/good.o" \
        >"$work/$label.stdout" 2>"$work/$label.stderr"; then
        echo "MINILD_LINUX_CLI=FAIL weakened_${label}_undefined_policy" >&2
        exit 1
    fi
    grep -q -- '--no-undefined-requires-final-static-link' \
        "$work/$label.stderr"
done

echo 'MINILD_LINUX_CLI=PASS GNU_BFD_same_object=true nostdlib=true '
echo 'MINILD_LINUX_CLI_RUNTIME=PASS GNU_x86_64=42 MiniLD_x86_64=42 '
echo 'MINILD_LINUX_CLI_UNDEFINED=PASS strong=GNU_rejected+MiniLD_rejected weak=GNU+MiniLD_zero nonstatic=fail_closed'
