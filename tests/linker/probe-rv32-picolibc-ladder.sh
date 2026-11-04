#!/usr/bin/env bash
# RV32I/ILP32 authentic Picolibc on Linux syscall adapter: five real C stages.
# GNU GCC -> MiniAS ET_REL -> exactly same input objects -> GNU BFD and MiniLD.
# GNU's rv64 Linux glibc is NOT an RV32 ILP32 runtime; the existing upstream
# RV32 Picolibc archives and the project's tested Linux entry are reused.
set -Eeuo pipefail

repo=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
minias=${MINIAS:-"$repo/build/release/bin/minic-as"}
minild=${MINILD:-"$repo/build/release/bin/minic-ld"}
cc=${BARE_CC:-riscv64-unknown-elf-gcc}
gnu_ld=${GNU_LD:-riscv64-linux-gnu-ld}
readelf=${GNU_READELF:-riscv64-linux-gnu-readelf}
qemu=${QEMU_RV32:-qemu-riscv32}
work=${WORK_DIR:-"$repo/build/minild-rv32-picolibc-ladder"}
mkdir -p "$work/evidence" "$work/abi"

fatal() {
    printf 'MINILD_RV32_PICOLIBC=ERROR reason=%s\n' "$1" |
        tee "$work/evidence/summary.txt" >&2
    exit 127
}
for tool in "$cc" "$gnu_ld" "$readelf" "$qemu" "$minias" "$minild" \
            timeout cmp sha256sum; do
    command -v "$tool" >/dev/null 2>&1 || fatal "missing_tool:$tool"
done
"$gnu_ld" --version | grep -Fq 'GNU ld' || fatal 'gnu_ld_not_bfd'
"$cc" --version >"$work/evidence/gcc-version.txt"
"$gnu_ld" --version >"$work/evidence/gnu-ld-version.txt"
sha256sum "$minias" "$minild" >"$work/evidence/tools.sha256"

# Resolve the REAL Debian RV32I/ILP32 multilib archives. Picolibc's own
# embedded linker script is bare-metal and must not be used for Linux ELF32.
find_rv32_archive() {
    local name=$1 path
    path=$(find /usr/lib/picolibc -type f -path "*/rv32i/ilp32/$name" \
            -print -quit 2>/dev/null || :)
    [[ -f $path ]] || return 1
    printf '%s\n' "$path"
}
libc=$(find_rv32_archive libc.a) || fatal 'missing_rv32i_ilp32_libc.a'
libm=$(find_rv32_archive libm.a) || fatal 'missing_rv32i_ilp32_libm.a'
target=(-march=rv32i -mabi=ilp32)
specs=(-specs=picolibc.specs)
libgcc=$("$cc" "${target[@]}" -print-libgcc-file-name) ||
    fatal 'missing_rv32_libgcc'
[[ -f $libgcc ]] || fatal "invalid_rv32_libgcc:$libgcc"
printf '%s\n' "$libc" "$libm" "$libgcc" \
    >"$work/evidence/runtime-paths.txt"
printf 'arch=riscv32\nisa=rv32i\nabi=ilp32\nlibc=picolibc\nsyscall_abi=Linux\ncommit=%s\n' \
    "${GITHUB_SHA:-local-uncommitted}" >"$work/evidence/identity.txt"

# This is the SAME Linux syscall/CRT port already used by the project for
# upstream cJSON, zlib and Lua under qemu-riscv32. There is no replacement
# implementation of Picolibc printf, malloc, TLS errno, or file operations.
runtime=(rv32-linux-picolibc-start.S rv32-linux-picolibc-syscalls.c
         rv32-linux-picolibc-posix.c rv32-linux-picolibc-errno.c)
runtime_objects=()
for source in "${runtime[@]}"; do
    input="$repo/tests/compiler/c0/$source"
    name=${source%.*}
    object="$work/abi/$name.o"
    if ! "$cc" "${target[@]}" "${specs[@]}" -O0 -ffreestanding \
             -fno-builtin -fno-pie -fno-pic -c "$input" -o "$object" \
             >"$work/evidence/runtime-$name.log" 2>&1; then
        sed -n '1,35p' "$work/evidence/runtime-$name.log" >&2
        fatal "runtime_compile:$source"
    fi
    runtime_objects+=("$object")
done
sha256sum "${runtime_objects[@]}" >>"$work/evidence/tools.sha256"

pass_count=0
failures=()
for stage in exit control memory multitu io; do
    dir="$work/$stage"
    mkdir -p "$dir/asm" "$dir/obj" "$dir/gnu" "$dir/mini" "$dir/evidence"
    errors=()
    case "$stage" in
        exit)
            sources=("$repo/tests/assembler/runtime-gcc-ladder/exit.c")
            opt=(-O0); expected='' ;;
        control)
            sources=("$repo/tests/assembler/runtime-gcc-ladder/control.c")
            opt=(-O1); expected='LADDER_CONTROL value=165 status=PASS' ;;
        memory)
            sources=("$repo/tests/assembler/runtime-gcc-ladder/memory.c")
            opt=(-O2); expected='LADDER_MEMORY sum=135 status=PASS' ;;
        multitu)
            sources=("$repo/tests/assembler/runtime-gcc-single-variable/main.c"
                     "$repo/tests/assembler/runtime-gcc-single-variable/helper.c")
            opt=(-O1)
            expected='MINIAS_GCC_RUNTIME value=74 bias=11 length=10 status=PASS' ;;
        io)
            sources=("$repo/tests/assembler/runtime-gcc-ladder/io.c")
            opt=(-O2); expected='LADDER_IO sum=1015 status=PASS' ;;
    esac
    printf '%s\n' "$expected" >"$dir/evidence/expected.stdout"
    [[ -n $expected ]] || : >"$dir/evidence/expected.stdout"
    : >"$dir/evidence/inputs.sha256"
    objects=()
    for source in "${sources[@]}"; do
        name=$(basename "$source" .c)
        asm="$dir/asm/$name.s"
        obj="$dir/obj/$name.o"
        if ! "$cc" "${target[@]}" "${specs[@]}" "${opt[@]}" \
                -std=c11 -g0 -ffreestanding -fno-builtin -fno-pie -fno-pic \
                -S "$source" -o "$asm" \
                >"$dir/evidence/gcc-$name.log" 2>&1; then
            errors+=("gcc-S:$name")
            continue
        fi
        if ! "$minias" --target=riscv -march=rv32i -mabi=ilp32 \
                -o "$obj" "$asm" \
                >"$dir/evidence/minias-$name.log" 2>&1; then
            errors+=("minias:$name")
            continue
        fi
        objects+=("$obj")
        sha256sum "$source" "$asm" "$obj" \
            >>"$dir/evidence/inputs.sha256"
    done
    if (( ${#objects[@]} != ${#sources[@]} )); then
        errors+=(missing-object)
    else
        # Same platform CRT/syscall objects, MiniAS C objects and archive
        # ordering for both GNU BFD and MiniLD; no compiler driver fallback.
        inputs=("${runtime_objects[@]}" "${objects[@]}"
                --start-group "$libm" "$libc" "$libgcc" --end-group)
        for linker in gnu mini; do
            if [[ $linker == gnu ]]; then ld="$gnu_ld"; else ld="$minild"; fi
            program="$dir/$linker/program"
            if ! "$ld" -m elf32lriscv -static -e _start \
                    -o "$program" "${inputs[@]}" \
                    >"$dir/evidence/$linker-link.log" 2>&1 ||
               [[ ! -s $program ]]; then
                errors+=("link:$linker")
                continue
            fi
            if ! "$readelf" -h "$program" \
                    >"$dir/evidence/$linker-elf-header.txt" ||
               ! grep -Eq 'Class:[[:space:]]+ELF32' \
                    "$dir/evidence/$linker-elf-header.txt" ||
               ! grep -Eq 'Type:[[:space:]]+EXEC' \
                    "$dir/evidence/$linker-elf-header.txt" ||
               ! "$readelf" -l "$program" \
                    >"$dir/evidence/$linker-program-headers.txt" ||
               grep -Fq INTERP "$dir/evidence/$linker-program-headers.txt"; then
                errors+=("invalid-elf:$linker")
                continue
            fi
            result=0
            timeout 30s "$qemu" "$program" \
                >"$dir/evidence/$linker-stdout.txt" \
                2>"$dir/evidence/$linker-stderr.txt" || result=$?
            printf '%s\n' "$result" >"$dir/evidence/$linker-exit.txt"
            if (( result != 0 )); then errors+=("exit:$linker:$result"); fi
            if ! cmp -s "$dir/evidence/expected.stdout" \
                        "$dir/evidence/$linker-stdout.txt"; then
                errors+=("stdout:$linker")
            fi
            if [[ -s $dir/evidence/$linker-stderr.txt ]]; then
                errors+=("stderr:$linker")
            fi
        done
        if [[ -f $dir/evidence/gnu-exit.txt &&
              -f $dir/evidence/mini-exit.txt ]]; then
            for field in exit stdout stderr; do
                if ! cmp -s "$dir/evidence/gnu-$field.txt" \
                             "$dir/evidence/mini-$field.txt"; then
                    errors+=("runtime-diff:$field")
                fi
            done
        else
            errors+=(missing-runtime-parity)
        fi
    fi
    if (( ${#errors[@]} == 0 )); then
        ((pass_count += 1))
        echo "MINILD_RV32_PICOLIBC_STAGE=PASS stage=$stage gnu_bfd=1 minild=1 qemu=2 parity=1"
    else
        failures+=("$stage:${errors[*]}")
        echo "MINILD_RV32_PICOLIBC_STAGE=FAIL stage=$stage reasons=${errors[*]}" >&2
        for log in "$dir"/evidence/*.log; do
            if [[ -s $log ]]; then
                echo "MINILD_RV32_PICOLIBC_DIAG stage=$stage path=$log" >&2
                sed -n '1,12p' "$log" >&2
            fi
        done
    fi
done
printf '%s\n' "${failures[@]}" >"$work/evidence/failures.txt"
if (( ${#failures[@]} != 0 )); then
    echo "MINILD_RV32_PICOLIBC=FAIL passed=$pass_count/5 failures=${failures[*]}" |
        tee "$work/evidence/summary.txt" >&2
    exit 1
fi
echo 'MINILD_RV32_PICOLIBC=PASS passed=5/5 libc=authentic_rv32i_picolibc linux_syscalls=1 gnu_bfd_parity=5' |
    tee "$work/evidence/summary.txt"
