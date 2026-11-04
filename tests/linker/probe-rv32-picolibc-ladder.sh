#!/usr/bin/env bash
# RV32I/ILP32 authentic Picolibc on Linux syscall adapter: five real C stages.
# Ordinary stages: GNU GCC -> MiniAS ET_REL -> same objects -> GNU BFD/MiniLD.
# Independent TLS relocation gate: GCC -> GNU GAS ET_REL -> same linkers;
# MiniAS `%tprel_*` source syntax is independently unsupported for now.
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
            timeout cmp sha256sum python3; do
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
tls_pass=0
failures=()
for stage in tls exit control memory multitu io; do
    dir="$work/$stage"
    mkdir -p "$dir/asm" "$dir/obj" "$dir/gnu" "$dir/mini" "$dir/evidence"
    errors=()
    case "$stage" in
        tls)
            sources=("$repo/tests/linker/fixtures/rv32-picolibc-tls-le.c")
            opt=(-O1 -ftls-model=local-exec)
            expected='MINILD_RV32_TLS init=74 zero=11 errno=19 status=PASS' ;;
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
        if [[ $stage == tls ]]; then
            # MiniAS does not yet accept GCC's `%tprel_*` assembler syntax.
            # This separate *linker* TLS gate feeds both linkers one genuine
            # GNU GAS ET_REL with all local-exec relocations preserved.
            # Ordinary C stages below still exclusively use MiniAS objects.
            if ! "$cc" "${target[@]}" -c "$asm" -o "$obj" \
                    >"$dir/evidence/gnu-as-$name.log" 2>&1; then
                errors+=("gnu-as-tls:$name")
                continue
            fi
        else
            if ! "$minias" --target=riscv -march=rv32i -mabi=ilp32 \
                    -o "$obj" "$asm" \
                    >"$dir/evidence/minias-$name.log" 2>&1; then
                errors+=("minias:$name")
                continue
            fi
        fi
        objects+=("$obj")
        sha256sum "$source" "$asm" "$obj" \
            >>"$dir/evidence/inputs.sha256"
    done
    if [[ $stage == tls && ${#objects[@]} -eq 1 ]]; then
        if ! "$readelf" -Wr "${objects[0]}" \
                 >"$dir/evidence/tls-relocations.txt" ||
           ! grep -q 'R_RISCV_TPREL_HI20' \
                 "$dir/evidence/tls-relocations.txt" ||
           ! grep -q 'R_RISCV_TPREL_ADD' \
                 "$dir/evidence/tls-relocations.txt" ||
           ! grep -q 'R_RISCV_TPREL_LO12_I' \
                 "$dir/evidence/tls-relocations.txt" ||
           ! grep -q 'R_RISCV_TPREL_LO12_S' \
                 "$dir/evidence/tls-relocations.txt"; then
            errors+=(missing_real_local_exec_tls_relocations)
        fi
        if ! "$readelf" -SW "${objects[0]}" \
                 >"$dir/evidence/tls-sections.txt" ||
           ! grep -Eq '\.tdata[[:space:]]+PROGBITS' \
                 "$dir/evidence/tls-sections.txt" ||
           ! grep -Eq '\.tbss[[:space:]]+NOBITS' \
                 "$dir/evidence/tls-sections.txt"; then
            errors+=(missing_initialized_or_zero_tls_sections)
        fi
    fi
    if (( ${#objects[@]} != ${#sources[@]} )); then
        errors+=(missing-object)
    else
        # Identical platform CRT/syscall objects, app objects and archive
        # ordering for both GNU BFD and MiniLD; the ordinary five app stages
        # use MiniAS, and only the isolated TLS gate uses GNU GAS.
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
            # The real Linux/Picolibc entry obtains PT_TLS using AT_PHDR.
            # ELF headers outside all PT_LOADs make that pointer unreadable.
            if ! python3 - "$program" \
                    >"$dir/evidence/$linker-linux-phdr.log" 2>&1 <<'PY'
from pathlib import Path
import struct
import sys

data = Path(sys.argv[1]).read_bytes()
if data[:6] != b'\x7fELF\x01\x01':
    raise SystemExit('not_linux_elf32le')
phoff = struct.unpack_from('<I', data, 28)[0]
phentsize, phnum = struct.unpack_from('<HH', data, 42)
if phentsize != 32 or phnum == 0 or phoff + phentsize * phnum > len(data):
    raise SystemExit('invalid_elf32_phdr_table')
headers = [struct.unpack_from('<IIIIIIII', data, phoff + 32*i)
           for i in range(phnum)]
if not any(h[0] == 1 and h[1] <= phoff and
           h[1] + h[4] >= phoff + phentsize * phnum for h in headers):
    raise SystemExit('AT_PHDR_not_backed_by_PT_LOAD')
tls = [h for h in headers if h[0] == 7]
if len(tls) != 1 or tls[0][4] > tls[0][5] or tls[0][5] == 0:
    raise SystemExit('invalid_or_missing_PT_TLS')
PY
            then
                errors+=("linux-phdr-or-tls:$linker")
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
        if [[ $stage == tls ]]; then
            tls_pass=1
            echo 'MINILD_RV32_TLS_LE=PASS gnu_as_tls_obj=1 minias_tls_asm=UNSUPPORTED gcc_tprel=1 pt_tls_qemu=2 errno=real_picolibc_tls gnu_bfd_parity=1'
        else
            ((pass_count += 1))
            echo "MINILD_RV32_PICOLIBC_STAGE=PASS stage=$stage gnu_bfd=1 minild=1 qemu=2 parity=1"
        fi
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
echo "MINILD_RV32_PICOLIBC=PASS passed=5/5 tls_le=$tls_pass libc=authentic_rv32i_picolibc linux_syscalls=1 gnu_bfd_parity=5" |
    tee "$work/evidence/summary.txt"
