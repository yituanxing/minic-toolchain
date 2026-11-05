#!/usr/bin/env bash
# GNU BFD --gc-sections preserves debug relocations and final symbol values
# only when their immutable source object survived the allocated-section GC
# frontier.  A discarded runtime target must retain source provenance long
# enough to trigger BFD's complete non-ALLOC field-zeroing rule.
set -Eeuo pipefail

: "${MINILD:?MINILD must point to minic-ld}"
gnu_ld=${GNU_LD:-ld.bfd}
for tool in as "$gnu_ld" objcopy readelf cmp python3; do
    command -v "$tool" >/dev/null || {
        echo "MINILD_GC_NONALLOC_GNU=INCONCLUSIVE missing_tool=$tool" >&2
        exit 8
    }
done
work=$(mktemp -d "${TMPDIR:-/tmp}/minild-gc-nonalloc.XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT

printf '.text\n.globl _start\n_start: ret\n.section .debug_owned,"",@progbits\n.quad 111\n.globl debug_global\ndebug_global: .quad 222\n.section .debug_ref,"",@progbits\n.quad debug_global\n.long debug_global - .\n' |
    as -o "$work/same.o" -
printf '.text\n.globl _start\n_start: call helper\nret\n.section .debug_owned,"",@progbits\n.quad 111\n' |
    as -o "$work/live-head.o" -
printf '.text\n.globl helper\nhelper: ret\n.section .debug_owned,"",@progbits\n.globl debug_global\ndebug_global: .quad 222\n.section .debug_ref,"",@progbits\n.quad debug_global\n.long debug_global - .\n' |
    as -o "$work/live-tail.o" -
printf '.text\n.globl _start\n_start: ret\n.section .debug_owned,"",@progbits\n.quad 111\n' |
    as -o "$work/dead-head.o" -
printf '.section .debug_owned,"",@progbits\n.globl debug_global\ndebug_global: .quad 222\n.section .debug_ref,"",@progbits\n.quad debug_global\n.long debug_global - .\n' |
    as -o "$work/dead-tail.o" -
cat >"$work/output.lds" <<'LDS'
SECTIONS {
  . = 0x10000;
  .text : { *(.text) }
  .debug_owned 0 : { *(.debug_owned) }
  .debug_ref 0 : { *(.debug_ref) }
}
LDS

compare_retained() {
    local label=$1 mode=$2; shift 2
    local opts=()
    if [[ $mode == scripted ]]; then opts=(-T "$work/output.lds"); fi
    "$gnu_ld" -m elf_x86_64 -static --gc-sections -e _start \
        "${opts[@]}" -o "$work/$label.$mode.gnu.elf" "$@"
    "$MINILD" -m elf_x86_64 -static --gc-sections -e _start \
        "${opts[@]}" -o "$work/$label.$mode.mini.elf" "$@"
    for flavor in gnu mini; do
        objcopy --dump-section ".debug_owned=$work/$label.$mode.$flavor.owned" \
            --dump-section ".debug_ref=$work/$label.$mode.$flavor.ref" \
            "$work/$label.$mode.$flavor.elf"
    done
    cmp "$work/$label.$mode.gnu.owned" "$work/$label.$mode.mini.owned"
    cmp "$work/$label.$mode.gnu.ref" "$work/$label.$mode.mini.ref"
    python3 - "$work/$label.$mode.gnu.ref" \
        "$work/$label.$mode.gnu.elf" "$work/$label.$mode.mini.elf" <<'PY'
import pathlib, subprocess, struct, sys
payload = pathlib.Path(sys.argv[1]).read_bytes()
assert struct.unpack('<Qi', payload) == (8, 0), payload.hex()
for name in sys.argv[2:]:
    syms = [line.split() for line in subprocess.check_output(
        ['readelf', '-Ws', name], text=True).splitlines()
        if line.split() and line.split()[-1] == 'debug_global']
    assert len(syms) == 1 and int(syms[0][1], 16) == 8, (name, syms)
PY
}
for mode in default scripted; do
    compare_retained same "$mode" "$work/same.o"
    compare_retained cross_live "$mode" "$work/live-head.o" "$work/live-tail.o"
done

# A second object with no live SHF_ALLOC member contributes neither debug
# bytes nor a definition, even if its debug source refers to itself.
for mode in default scripted; do
    opts=()
    if [[ $mode == scripted ]]; then opts=(-T "$work/output.lds"); fi
    for flavor in gnu mini; do
        linker="$MINILD"
        if [[ $flavor == gnu ]]; then linker=$gnu_ld; fi
        "$linker" -m elf_x86_64 -static --gc-sections -e _start \
            "${opts[@]}" -o "$work/cross_dead.$mode.$flavor.elf" \
            "$work/dead-head.o" "$work/dead-tail.o"
        if readelf -SW "$work/cross_dead.$mode.$flavor.elf" |
                grep -q '\.debug_ref'; then
            echo "MINILD_GC_NONALLOC_GNU=FAIL dead_debug_kept:$mode:$flavor" >&2
            exit 1
        fi
        if readelf -Ws "$work/cross_dead.$mode.$flavor.elf" |
                awk '$NF=="debug_global" {found=1} END {exit !found}'; then
            echo "MINILD_GC_NONALLOC_GNU=FAIL dead_global_symtab:$mode:$flavor" >&2
            exit 1
        fi
        objcopy --dump-section ".debug_owned=$work/cross_dead.$mode.$flavor.owned" \
            "$work/cross_dead.$mode.$flavor.elf"
    done
    cmp "$work/cross_dead.$mode.gnu.owned" \
        "$work/cross_dead.$mode.mini.owned"
done

# Even a nonzero relocation *field* must be overwritten with zero when the
# ALLOC target was discarded. The undefined weak reference still uses S=0+A.
printf '.text\n.globl _start\n_start: ret\n.section .text.drop,"ax",@progbits\n.weak weakdead\nweakdead: ret\n.section .debug_info,"",@progbits\n.quad 0x1122334455667788\n.reloc .-8, R_X86_64_64, weakdead\n.quad weakdead+37\n.weak missingweak\n.quad missingweak+37\n' |
    as -o "$work/dead-target.o" -
for flavor in gnu mini; do
    linker="$MINILD"; [[ $flavor == gnu ]] && linker=$gnu_ld
    "$linker" -m elf_x86_64 -static --gc-sections -e _start \
        -o "$work/dead-target.$flavor.elf" "$work/dead-target.o"
    objcopy --dump-section ".debug_info=$work/dead-target.$flavor.info" \
        "$work/dead-target.$flavor.elf"
done
cmp "$work/dead-target.gnu.info" "$work/dead-target.mini.info"
python3 - "$work/dead-target.mini.info" <<'PY'
import pathlib, struct, sys
assert struct.unpack('<QQQ', pathlib.Path(sys.argv[1]).read_bytes()) == (0, 0, 37)
PY

# --emit-relocs cannot project the original dead definition as fake UND.
# BFD leaves two R_NONE slots in .rela.debug_info and preserves the real
# undefined weak reference with its S=0+A relocation identity.
for flavor in gnu mini; do
    linker="$MINILD"; [[ $flavor == gnu ]] && linker=$gnu_ld
    "$linker" -m elf_x86_64 -static --gc-sections --emit-relocs -e _start \
        -o "$work/emit.$flavor.elf" "$work/dead-target.o"
    if readelf -Ws "$work/emit.$flavor.elf" |
            awk '$NF=="weakdead" {found=1} END {exit !found}'; then
        echo "MINILD_GC_NONALLOC_GNU=FAIL emit_dead_symbol:$flavor" >&2
        exit 1
    fi
done
python3 - "$work/emit.gnu.elf" "$work/emit.mini.elf" <<'PY'
import re, subprocess, sys
def kinds(path):
    output = subprocess.check_output(['readelf', '-Wr', path], text=True)
    chunk = output.split("Relocation section '.rela.debug_info'", 1)[1]
    chunk = chunk.split('Relocation section ', 1)[0]
    result = []
    for line in chunk.splitlines():
        if re.search(r'R_X86_64_', line):
            parts = line.split()
            result.append((parts[2], 'missingweak' in line))
    return result
gnu = kinds(sys.argv[1]); mini = kinds(sys.argv[2]);
assert gnu == mini, (gnu, mini)
assert len(gnu) == 3 and gnu[0][0] == 'R_X86_64_NONE' \
       and gnu[1][0] == 'R_X86_64_NONE' \
       and gnu[2][0] == 'R_X86_64_64' and gnu[2][1], gnu
PY

# -S removes the surviving non-ALLOC source from the output projection.
for flavor in gnu mini; do
    linker="$MINILD"; [[ $flavor == gnu ]] && linker=$gnu_ld
    "$linker" -m elf_x86_64 -static --gc-sections -S -e _start \
        -o "$work/strip.$flavor.elf" "$work/same.o"
    if readelf -SW "$work/strip.$flavor.elf" | grep -q '\.debug_'; then
        echo "MINILD_GC_NONALLOC_GNU=FAIL strip_debug_kept:$flavor" >&2
        exit 1
    fi
done

# An otherwise unknown relocation type in stripped metadata is no ISA
# problem: validate target capability only after OutputPlan source liveness.
printf '.text\n.globl _start\n_start: ret\n.section .debug_info,"",@progbits\n.quad 0\n.reloc 0, R_X86_64_SIZE64, _start+7\n' |
    as -o "$work/unknown-debug.o" -
for options in strip strip_gc; do
    flags=(-S)
    if [[ $options == strip_gc ]]; then flags=(--gc-sections -S); fi
    for flavor in gnu mini; do
        linker="$MINILD"; [[ $flavor == gnu ]] && linker=$gnu_ld
        "$linker" -m elf_x86_64 -static -e _start "${flags[@]}" \
            -o "$work/unknown.$options.$flavor.elf" "$work/unknown-debug.o"
    done
done

# A live runtime relocation to non-ALLOC is never legal even with GC.
printf '.text\n.globl _start\n_start: .quad debug_global\n.section .debug_owned,"",@progbits\n.globl debug_global\ndebug_global: .quad 4\n' |
    as -o "$work/invalid-runtime.o" -
if "$MINILD" -m elf_x86_64 -static --gc-sections -e _start \
        -o "$work/invalid-runtime.elf" "$work/invalid-runtime.o" \
        >"$work/invalid-runtime.log" 2>&1; then
    echo 'MINILD_GC_NONALLOC_GNU=FAIL alloc_to_nonalloc_accepted' >&2
    exit 1
fi
grep -Fq 'unsupported-static-symbol:debug_global' "$work/invalid-runtime.log"
test ! -e "$work/invalid-runtime.elf"

# SHF_GNU_RETAIN roots a debug-only object without any ALLOC member.
printf '.section .debug_retained,"R",@progbits\n.byte 0x51\n' |
    as -o "$work/retain-only.o" -
for flavor in gnu mini; do
    linker="$MINILD"; [[ $flavor == gnu ]] && linker=$gnu_ld
    "$linker" -m elf_x86_64 -static --gc-sections -e _start \
        -o "$work/retain.$flavor.elf" \
        "$work/dead-head.o" "$work/retain-only.o"
    objcopy --dump-section ".debug_retained=$work/retain.$flavor.payload" \
        "$work/retain.$flavor.elf"
done
cmp "$work/retain.gnu.payload" "$work/retain.mini.payload"

# Explicit KEEP roots a non-ALLOC section of an otherwise dead input object.
printf '.section .debug_keep,"",@progbits\n.byte 0x52\n' |
    as -o "$work/keep-only.o" -
cat >"$work/keep.lds" <<'LDS'
ENTRY(_start)
SECTIONS {
  . = 0x401000;
  .text : { *(.text) }
  .debug_keep 0 : { KEEP(*(.debug_keep)) }
}
LDS
for flavor in gnu mini; do
    linker="$MINILD"; [[ $flavor == gnu ]] && linker=$gnu_ld
    "$linker" -m elf_x86_64 -static --gc-sections -T "$work/keep.lds" \
        -o "$work/keep.$flavor.elf" \
        "$work/dead-head.o" "$work/keep-only.o"
    objcopy --dump-section ".debug_keep=$work/keep.$flavor.payload" \
        "$work/keep.$flavor.elf"
done
cmp "$work/keep.gnu.payload" "$work/keep.mini.payload"

# Non-ALLOC SHF_LINK_ORDER follows its target in ordinary GC, but KEEP on
# the metadata also roots the reverse edge to the formerly dead target.
printf '.text\n.globl _start\n_start: ret\n.section .text.deadlink,"ax",@progbits\n.globl deadlink\ndeadlink: ret\n.section .meta,"o",@progbits,.text.deadlink\n.quad deadlink\n.section .debug_link,"",@progbits\n.quad deadlink+11\n' |
    as -o "$work/link-order.o" -
cat >"$work/keep-link.lds" <<'LDS'
ENTRY(_start)
SECTIONS {
  . = 0x401000;
  .text : { *(.text) *(.text.deadlink) }
  .meta 0 : { KEEP(*(.meta)) }
  .debug_link 0 : { *(.debug_link) }
}
LDS
for case in bare keep; do
    opts=()
    if [[ $case == keep ]]; then opts=(-T "$work/keep-link.lds"); fi
    for flavor in gnu mini; do
        linker="$MINILD"; [[ $flavor == gnu ]] && linker=$gnu_ld
        "$linker" -m elf_x86_64 -static --gc-sections -e _start \
            "${opts[@]}" -o "$work/link-order.$case.$flavor.elf" \
            "$work/link-order.o"
        for section in .meta deadlink; do
            present=no
            if [[ $section == .meta ]]; then
                if readelf -SW "$work/link-order.$case.$flavor.elf" |
                        grep -q '\.meta '; then present=yes; fi
            else
                # The script folds input .text.deadlink into output .text.
                if readelf -Ws "$work/link-order.$case.$flavor.elf" |
                        awk '$NF=="deadlink" && $7!="UND" {found=1} END {exit !found}'; then
                    present=yes
                fi
            fi
            if [[ $case == bare && $present != no ]] ||
               [[ $case == keep && $present != yes ]]; then
                echo "MINILD_GC_NONALLOC_GNU=FAIL link_order:$case:$flavor:$section" >&2
                exit 1
            fi
        done
    done
done

# GNU COMDAT group reachability is atomic: a KEEP'd non-ALLOC member roots
# allocated siblings. An otherwise dead group is not rescued just because
# another member of the same ET_REL object has a live ungrouped .text.
printf '.section .text.grp,"axG",@progbits,grp,comdat\n.globl grp\ngrp:\n.globl deadcomdat\ndeadcomdat: ret\n.section .debug_group,"G",@progbits,grp,comdat\n.quad deadcomdat+7\n' |
    as -o "$work/comdat.o" -
cat >"$work/keep-comdat.lds" <<'LDS'
ENTRY(_start)
SECTIONS {
  . = 0x401000;
  .text : { *(.text) *(.text.grp) }
  .debug_group 0 : { KEEP(*(.debug_group)) }
}
LDS
for case in bare keep; do
    opts=()
    if [[ $case == keep ]]; then opts=(-T "$work/keep-comdat.lds"); fi
    for flavor in gnu mini; do
        linker="$MINILD"; [[ $flavor == gnu ]] && linker=$gnu_ld
        "$linker" -m elf_x86_64 -static --gc-sections -e _start \
            "${opts[@]}" -o "$work/comdat.$case.$flavor.elf" \
            "$work/dead-head.o" "$work/comdat.o"
        if readelf -SW "$work/comdat.$case.$flavor.elf" |
                grep -q '\.debug_group'; then present=yes; else present=no; fi
        if [[ $case == bare && $present != no ]] ||
           [[ $case == keep && $present != yes ]]; then
            echo "MINILD_GC_NONALLOC_GNU=FAIL comdat:$case:$flavor" >&2
            exit 1
        fi
    done
done

# Once COMDAT selection/reachability is decided, GNU may coalesce its live
# metadata with an ordinary same-named section in the final output. GROUP
# identity controls GC, not output-section partitioning.
printf '.text\n.globl _start\n_start: call grp\nret\n.section .debug_owned,"",@progbits\n.quad group_debug\n' |
    as -o "$work/group-root.o" -
printf '.section .text.grp,"axG",@progbits,grp,comdat\n.globl grp\ngrp: ret\n.section .debug_owned,"G",@progbits,grp,comdat\n.globl group_debug\ngroup_debug: .quad 222\n' |
    as -o "$work/group-member.o" -
for flavor in gnu mini; do
    linker="$MINILD"; [[ $flavor == gnu ]] && linker=$gnu_ld
    "$linker" -m elf_x86_64 -static --gc-sections -e _start \
        -o "$work/group-merge.$flavor.elf" \
        "$work/group-root.o" "$work/group-member.o"
    objcopy --dump-section ".debug_owned=$work/group-merge.$flavor.payload" \
        "$work/group-merge.$flavor.elf"
done
cmp "$work/group-merge.gnu.payload" "$work/group-merge.mini.payload"
test "$(wc -c <"$work/group-merge.mini.payload")" -eq 16

echo 'MINILD_GC_NONALLOC_GNU=PASS cases=same,cross_live,cross_dead,retain,keep,link_order,comdat,group_merge modes=default_scripted weak_discarded_vs_undef=distinct whole_field_zero=1 strip=1 emit_R_NONE=2 stripped_unknown_relocation=2 alloc_strict=1'
