#!/usr/bin/env bash
# Five-target static linker-script / input-section GC boundary, intentionally
# separate from the 2064-object Linux build and from libc runtime tests.
# MiniAS produces one identical object set per target for GNU ld and MiniLD.
# TargetProfile is the admission authority: a gated target must reject both
# independent switches, and an enabled target must match GNU ELF/runtime.
set -Eeuo pipefail

: "${MINIAS:?set MINIAS to exact-head minic-as}"
: "${MINILD:?set MINILD to exact-head minic-ld}"
for tool in python3 readelf timeout; do
    command -v "$tool" >/dev/null || { echo "MINILD_SCRIPT_GC_5ARCH=FAIL missing=$tool" >&2; exit 1; }
done

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/minild-script-gc-5arch.XXXXXX")
finish() {
    if [[ ${MINILD_SCRIPT_GC_5ARCH_KEEP:-0} == 1 ]]; then
        echo "MINILD_SCRIPT_GC_5ARCH_ARTIFACTS=$work"
    else
        rm -rf -- "$work"
    fi
}
trap finish EXIT

# Fails closed if someone changes a target gate but leaves the acceptance
# proof unmodified. The actual false/true flags are parsed from the C table;
# no second hand-maintained copy of the capability matrix is trusted.
python3 - "$repo_root/linker/src/linker_target_elf.inc" "$work/gates" <<'PY'
import re
import sys
from pathlib import Path

source = Path(sys.argv[1]).read_text()
emulations = {
    'rv32': 'elf32lriscv', 'rv64': 'elf64lriscv',
    'x86_64': 'elf_x86_64', 'aarch64': 'aarch64linux',
    'loongarch64': 'elf64loongarch',
}
lines = []
for arch, emulation in emulations.items():
    pattern = (r'\{"' + re.escape(emulation) +
               r'"[^{}]*?4096U,\s*'
               r'(true|false),\s*(true|false),\s*'
               r'(true|false),\s*(true|false),')
    match = re.search(pattern, source, re.S)
    if match is None or match.group(1) != 'true' or match.group(2) != 'true':
        raise SystemExit(f'MINILD_SCRIPT_GC_5ARCH=FAIL target_profile={emulation}')
    if match.group(3) != match.group(4):
        raise SystemExit(f'MINILD_SCRIPT_GC_5ARCH=FAIL split_script_gc_policy={emulation}')
    lines.append(f'{arch} {emulation} {match.group(3)}')
Path(sys.argv[2]).write_text('\n'.join(lines) + '\n')
PY

die() { echo "MINILD_SCRIPT_GC_5ARCH=FAIL arch=$arch stage=$1" >&2; exit 1; }
have() { command -v "$1" >/dev/null 2>&1; }
probe_arch() {
    arch=$1
    emulation=$2
    policy=$3
    local script_base=0x10000
    case "$arch" in
        rv32)
            target=riscv; march=rv32i; mabi=ilp32
            gnu_ld=riscv64-linux-gnu-ld; qemu=qemu-riscv32
            output_arch=riscv; klass=1; machine=243
            exit_code='addi a0,zero,0'; exit_syscall='addi a7,zero,93'; trap_insn='ecall'
            ;;
        rv64)
            target=riscv; march=rv64i; mabi=lp64
            gnu_ld=riscv64-linux-gnu-ld; qemu=qemu-riscv64
            output_arch=riscv; klass=2; machine=243
            exit_code='addi a0,zero,0'; exit_syscall='addi a7,zero,93'; trap_insn='ecall'
            ;;
        x86_64)
            target=x86_64; march=x86-64; mabi=sysv
            gnu_ld=ld; qemu=native
            output_arch=i386:x86-64; klass=2; machine=62
            exit_code='xorq %rdi,%rdi'; exit_syscall='movq $60,%rax'; trap_insn='syscall'
            ;;
        aarch64)
            target=aarch64; march=armv8-a; mabi=lp64
            gnu_ld=aarch64-linux-gnu-ld; qemu=qemu-aarch64
            output_arch=aarch64; klass=2; machine=183
            exit_code='mov x0,#0'; exit_syscall='mov x8,#93'; trap_insn='svc #0'
            ;;
        loongarch64)
            target=loongarch64; march=loongarch64; mabi=lp64d
            gnu_ld=loongarch64-linux-gnu-ld; qemu=qemu-loongarch64
            output_arch=loongarch64; klass=2; machine=258
            script_base=0x120010000
            exit_code='ori $a0,$zero,0'; exit_syscall='ori $a7,$zero,93'; trap_insn='syscall 0'
            ;;
        *) die unknown_arch ;;
    esac
    if ! have "$gnu_ld" || { [[ $qemu != native ]] && ! have "$qemu"; }; then
        if [[ ${MINILD_REQUIRE_GNU:-0} == 1 || ${MINILD_REQUIRE_QEMU:-0} == 1 ]]; then
            die "missing_gnu_or_qemu:gcc_bfd=$gnu_ld:qemu=$qemu"
        fi
        echo "MINILD_SCRIPT_GC_5ARCH=SKIP arch=$arch missing_gnu_or_qemu"
        return
    fi
    local dir="$work/$arch"
    mkdir -p "$dir"
    printf '%s\n' \
        '.section .text.entry,"ax",@progbits' \
        '.globl _start' \
        '.type _start,@function' \
        '_start:' \
        "    $exit_code" \
        "    $exit_syscall" \
        "    $trap_insn" \
        '.size _start,.-_start' \
        '.section .text.keep,"ax",@progbits' \
        '.globl keep_fn' \
        'keep_fn:' \
        '    .long 0' \
        '.section .discard.gone,"ax",@progbits' \
        '.globl discard_fn' \
        'discard_fn:' \
        '    .long 0' >"$dir/entry.s"
    printf '%s\n' \
        '.section .text.dead,"ax",@progbits' \
        '.globl dead_fn' \
        'dead_fn:' \
        "    $( [[ $klass == 1 ]] && echo '.long' || echo '.quad') missing_dead" \
        >"$dir/dead.s"
    # GNU BFD's OUTPUT_ARCH spelling may differ from MiniLD's gated target
    # profile (notably on targets that do not yet support -T at all).  For
    # gated targets the test is GNU -m ABI/runtime versus MiniLD admission,
    # not a claim that MiniLD parses a foreign GNU script dialect.  Only a
    # script-enabled target exercises its exact OUTPUT_ARCH contract.
    local script_arch=()
    if [[ $policy == true ]]; then
        script_arch=("OUTPUT_ARCH($output_arch)")
    fi
    printf '%s\n' \
        "${script_arch[@]}" \
        'ENTRY(_start)' \
        'SECTIONS {' \
        "  . = $script_base;" \
        '  .text : { *(.text.entry) }' \
        '  .keeper : { KEEP(*(.text.keep)) }' \
        '  .dead : { *(.text.dead) }' \
        '  /DISCARD/ : { *(.discard.*) }' \
        '}' >"$dir/script.lds"
    for part in entry dead; do
        "$MINIAS" "--target=$target" "-march=$march" "-mabi=$mabi" \
            -o "$dir/$part.o" "$dir/$part.s" >"$dir/as-$part.log" 2>&1 || die "as-$part"
    done

    # Without script and GC all five profiles currently admit ET_EXEC. It
    # also provides a control to distinguish policy gating from bad objects.
    "$MINILD" -m "$emulation" -static -e _start -o "$dir/plain-mini.elf" \
        "$dir/entry.o" >"$dir/plain-mini.log" 2>&1 || die plain_link
    "$gnu_ld" -m "$emulation" -static -e _start -o "$dir/plain-gnu.elf" \
        "$dir/entry.o" >"$dir/plain-gnu.log" 2>&1 || die plain_gnu
    check_elf "$dir/plain-mini.elf" plain
    check_elf "$dir/plain-gnu.elf" plain
    run_elf "$dir/plain-mini.elf" plain_mini
    run_elf "$dir/plain-gnu.elf" plain_gnu

    # GNU independently proves the exact same MiniAS objects and script can
    # yield a runnable, section-GCed image, even on MiniLD-gated targets.
    "$gnu_ld" -m "$emulation" -static --gc-sections \
        -T "$dir/script.lds" -o "$dir/script-gnu.elf" \
        "$dir/entry.o" "$dir/dead.o" >"$dir/script-gnu.log" 2>&1 || die script_gnu
    check_elf "$dir/script-gnu.elf" script_gc
    run_elf "$dir/script-gnu.elf" script_gnu
    if [[ $policy == false ]]; then
        local option stage
        for stage in script gc combined; do
            case "$stage" in
                script) option=(-T "$dir/script.lds") ;;
                gc) option=(--gc-sections) ;;
                combined) option=(-T "$dir/script.lds" --gc-sections) ;;
            esac
            if "$MINILD" -m "$emulation" -static "${option[@]}" \
                -o "$dir/should-not-exist-$stage.elf" "$dir/entry.o" \
                >"$dir/negative-$stage.log" 2>&1; then
                die "unintended_admission:$stage"
            fi
            grep -Fq "unsupported-target-static-script-or-gc:$emulation" \
                "$dir/negative-$stage.log" || die "missing_policy_diagnostic:$stage"
            [[ ! -e "$dir/should-not-exist-$stage.elf" ]] || die "wrote_gated_elf:$stage"
        done
        echo "MINILD_SCRIPT_GC_5ARCH=GATED arch=$arch profile_script=false profile_gc=false gnu_reference=PASS minild_negative=3/3 plain_runtime=PASS"
    else
        # Independently admit and exercise each flag, not only the combined
        # mode. Script-only must exclude dead.o: its missing_dead reference
        # is supposed to fail without --gc-sections, not be silently lost.
        "$gnu_ld" -m "$emulation" -static \
            -T "$dir/script.lds" -o "$dir/script-only-gnu.elf" \
            "$dir/entry.o" >"$dir/script-only-gnu.log" 2>&1 || die script_only_gnu
        "$MINILD" -m "$emulation" -static \
            -T "$dir/script.lds" -o "$dir/script-only-mini.elf" \
            "$dir/entry.o" >"$dir/script-only-mini.log" 2>&1 || die script_only_mini
        for producer in gnu mini; do
            check_elf "$dir/script-only-$producer.elf" script_only
            run_elf "$dir/script-only-$producer.elf" "script_only_$producer"
        done

        # No linker script: the unreferenced KEEP fixture must now disappear.
        # The separate dead object contains an unresolved relocation that
        # both linkers must prune before resolving live symbols.
        "$gnu_ld" -m "$emulation" -static --gc-sections -e _start \
            -o "$dir/gc-only-gnu.elf" "$dir/entry.o" "$dir/dead.o" \
            >"$dir/gc-only-gnu.log" 2>&1 || die gc_only_gnu
        "$MINILD" -m "$emulation" -static --gc-sections -e _start \
            -o "$dir/gc-only-mini.elf" "$dir/entry.o" "$dir/dead.o" \
            >"$dir/gc-only-mini.log" 2>&1 || die gc_only_mini
        for producer in gnu mini; do
            check_elf "$dir/gc-only-$producer.elf" gc_only
            run_elf "$dir/gc-only-$producer.elf" "gc_only_$producer"
        done

        # Finally combine the two independent features: KEEP retains the
        # otherwise dead .text.keep; /DISCARD/ and GC remove the other inputs.
        "$MINILD" -m "$emulation" -static --gc-sections \
            -T "$dir/script.lds" -o "$dir/script-mini.elf" \
            "$dir/entry.o" "$dir/dead.o" >"$dir/script-mini.log" 2>&1 || die script_mini
        check_elf "$dir/script-mini.elf" script_gc
        run_elf "$dir/script-mini.elf" script_mini
        echo "MINILD_SCRIPT_GC_5ARCH=PASS arch=$arch profile_script=true profile_gc=true gnu_mini_modes=3/3 script_only=PASS gc_only=PASS script_gc=PASS plain_runtime=PASS"
    fi
}

check_elf() {
    python3 - "$1" "$klass" "$machine" "$2" <<'PY'
import subprocess
import struct
import sys
from pathlib import Path

path, klass, machine, mode = sys.argv[1:]
raw = Path(path).read_bytes()
if raw[:5] != b'\x7fELF' + bytes([int(klass)]) or len(raw) < 52:
    raise SystemExit(f'MINILD_SCRIPT_GC_5ARCH=FAIL malformed_ELF={path}')
kind, emachine = struct.unpack_from('<HH', raw, 16)
if kind != 2 or emachine != int(machine):
    raise SystemExit(f'MINILD_SCRIPT_GC_5ARCH=FAIL wrong_identity={path}')
if not any('LOAD' in line for line in subprocess.check_output(
        ['readelf', '-lW', path], text=True).splitlines()):
    raise SystemExit(f'MINILD_SCRIPT_GC_5ARCH=FAIL no_PT_LOAD={path}')
symbols = subprocess.check_output(['readelf', '-Ws', path], text=True)
def defined(name):
    return any(len(parts := line.split()) >= 8 and parts[-1] == name
               and parts[6] != 'UND' for line in symbols.splitlines())
if not defined('_start'):
    raise SystemExit(f'MINILD_SCRIPT_GC_5ARCH=FAIL missing_entry={path}')
if mode in ('script_only', 'script_gc') and not defined('keep_fn'):
    raise SystemExit(f'MINILD_SCRIPT_GC_5ARCH=FAIL KEEP_missing={path}')
if mode == 'gc_only' and defined('keep_fn'):
    raise SystemExit(f'MINILD_SCRIPT_GC_5ARCH=FAIL nonKEEP_section_survived_GC={path}')
if mode in ('script_only', 'script_gc', 'gc_only') and (
        defined('dead_fn') or defined('discard_fn')):
    raise SystemExit(f'MINILD_SCRIPT_GC_5ARCH=FAIL bad_dead_or_DISCARD={path}')
PY
}

run_elf() {
    local elf=$1 stage=$2
    if [[ $qemu == native ]]; then
        timeout 8 "$elf" >"$work/$arch/runtime-$stage.log" 2>&1 || die "runtime_$stage"
    else
        timeout 8 "$qemu" "$elf" >"$work/$arch/runtime-$stage.log" 2>&1 || die "runtime_$stage"
    fi
}

arch_filter=" ${MINILD_SCRIPT_GC_ARCHES:-rv32 rv64 x86_64 aarch64 loongarch64} "
while read -r name emulation policy; do
    if [[ $arch_filter == *" $name "* ]]; then
        probe_arch "$name" "$emulation" "$policy"
    fi
done <"$work/gates"
echo 'MINILD_SCRIPT_GC_5ARCH=COMPLETE tiny_objects_only=true no_linux_kbuild=true'
