#!/usr/bin/env bash
# Probe LoongArch static PIE dynamic relocation capacity against GNU BFD.
# Global /DISCARD/ references and references to global SHN_ABS definitions
# may reserve trailing R_NONE, even though neither needs an active rebasing
# record. Local references use section-scoped dynrel lists; non-ALLOC sites
# never reserve image-base relocation capacity.
set -Eeuo pipefail

repo=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
minias=${MINIAS:-"$repo/build/release/bin/minic-as"}
minild=${MINILD:-"$repo/build/release/bin/minic-ld"}
work=$(mktemp -d "${TMPDIR:-/tmp}/minild-static-discard-reserve.XXXXXX")
finish() {
    if [[ ${MINILD_STATIC_DISCARD_KEEP:-0} == 1 ]]; then
        echo "MINILD_STATIC_DISCARD_ARTIFACTS=$work"
    else
        rm -r -- "$work"
    fi
}
trap finish EXIT

cat >"$work/image.lds" <<'LDS'
OUTPUT_ARCH(loongarch)
ENTRY(_start)
PHDRS { text PT_LOAD FLAGS(7); }
SECTIONS {
  . = 0x9000000000100000;
  .text : { *(.text) } :text
  .iplt : { *(.iplt) }
  .got : ALIGN(16) { *(.got) }
  .igot.plt : { *(.igot.plt) }
  .data : { *(.data) }
  .rela.dyn : ALIGN(8) { *(.rela.dyn) }
  /DISCARD/ : { *(.data.dead) *(.text.dead) }
}
LDS
cat >"$work/live.s" <<'ASM'
.text
.globl _start
_start:
    jirl $r0,$r1,0
.Llocal_alive:
    jirl $r0,$r1,0
.section .text.dead,"ax",@progbits
.Llocal_dead:
    jirl $r0,$r1,0
.data
.globl live_pointer
live_pointer:
    .quad _start
    .quad absolute_value
.section .data.dead,"aw",@progbits
.globl discarded_pointer
discarded_pointer:
    .quad _start
    .quad _start
    .quad .Llocal_alive
    .quad .Llocal_dead
.section .debug_noise,"",@progbits
    .quad _start
ASM
cat >"$work/dead_only.s" <<'ASM'
.text
.globl _start
_start:
    jirl $r0,$r1,0
.section .data.dead,"aw",@progbits
.globl discarded_pointer
discarded_pointer:
    .quad _start
.section .debug_noise,"",@progbits
    .quad _start
ASM
cat >"$work/absolute.s" <<'ASM'
.globl absolute_value
.set absolute_value, 0x12345678
ASM
"$minias" --target=loongarch64 -march=loongarch64 -mabi=lp64d \
    -o "$work/live.o" "$work/live.s"
"$minias" --target=loongarch64 -march=loongarch64 -mabi=lp64d \
    -o "$work/dead_only.o" "$work/dead_only.s"
"$minias" --target=loongarch64 -march=loongarch64 -mabi=lp64d \
    -o "$work/absolute.o" "$work/absolute.s"

# Orthogonal GNU witnesses diagnose candidate lifetime separately.  In
# particular, an ABS relocation has a constant value, while a local symbol
# referenced from /DISCARD/ follows a different BFD dynrel ownership path
# from a global reference at the same place.
for kind in abs_live global_live local_live global_dead local_dead; do
    case "$kind" in
        abs_live)      body='.data
    .quad absolute_value' ;;
        global_live)   body='.data
    .quad _start' ;;
        local_live)    body='.data
    .quad .Llocal_alive' ;;
        global_dead)   body='.section .data.dead,"aw",@progbits
    .quad _start' ;;
        local_dead)    body='.section .data.dead,"aw",@progbits
    .quad .Llocal_alive' ;;
    esac
    printf '.text\n.globl _start\n_start:\n    nop\n.Llocal_alive:\n    nop\n%b\n' \
        "$body" >"$work/$kind.s"
    "$minias" --target=loongarch64 -march=loongarch64 -mabi=lp64d \
        -o "$work/$kind.o" "$work/$kind.s"
done
for mode in live dead_only; do
    inputs=("$work/$mode.o")
    [[ $mode == live ]] && inputs+=("$work/absolute.o")
    "$minild" -m elf64loongarch -static -pie -n --no-dynamic-linker \
        --script="$work/image.lds" -o "$work/$mode.elf" "${inputs[@]}"
done

# The exact same MiniAS ELF objects and linker script drive the GNU oracle.
# Allow local smoke without GNU cross-binutils, but fail closed in CI where
# MINILD_REQUIRE_GNU=1 is requested.
gnu_ld=
if [[ ${MINILD_REQUIRE_GNU:-0} == 1 || ${MINILD_COMPARE_GNU:-0} == 1 ]]; then
    gnu_ld=${LOONG_GNU_LD:-}
fi
if [[ -z $gnu_ld && ( ${MINILD_REQUIRE_GNU:-0} == 1 ||
                       ${MINILD_COMPARE_GNU:-0} == 1 ) ]]; then
    for candidate in loongarch64-linux-gnu-ld loongarch64-unknown-linux-gnu-ld; do
        if command -v "$candidate" >/dev/null 2>&1; then
            gnu_ld=$(command -v "$candidate")
            break
        fi
    done
fi
if [[ -z $gnu_ld && ( ${MINILD_REQUIRE_GNU:-0} == 1 ||
                       ${MINILD_COMPARE_GNU:-0} == 1 ) ]]; then
    echo 'MINILD_STATIC_SCRIPT_DISCARD_GNU_ORACLE=FAIL cross linker unavailable' >&2
    exit 1
fi
if [[ -n $gnu_ld ]]; then
    for mode in live dead_only; do
        inputs=("$work/$mode.o")
        [[ $mode == live ]] && inputs+=("$work/absolute.o")
        "$gnu_ld" -m elf64loongarch -static -pie -n --no-dynamic-linker \
            --script="$work/image.lds" -o "$work/$mode.gnu.elf" "${inputs[@]}"
    done
    for kind in abs_live global_live local_live global_dead local_dead; do
        inputs=("$work/$kind.o")
        [[ $kind == abs_live ]] && inputs+=("$work/absolute.o")
        "$minild" -m elf64loongarch -static -pie -n --no-dynamic-linker \
            --script="$work/image.lds" -o "$work/$kind.mini.elf" "${inputs[@]}"
        "$gnu_ld" -m elf64loongarch -static -pie -n --no-dynamic-linker \
            --script="$work/image.lds" -o "$work/$kind.gnu.elf" "${inputs[@]}"
    done
fi

oracles=("$work/live.elf" "$work/dead_only.elf")
if [[ -n $gnu_ld ]]; then
    oracles+=("$work/live.gnu.elf" "$work/dead_only.gnu.elf")
fi
python3 - "${oracles[@]}" <<'PY'
from pathlib import Path
import struct
import sys

def extract(path):
    raw = Path(path).read_bytes()
    assert raw[:6] == b'\x7fELF\x02\x01'
    shoff = struct.unpack_from('<Q', raw, 40)[0]
    shnum, shstridx = struct.unpack_from('<HH', raw, 60)
    sections = [struct.unpack_from('<IIQQQQIIQQ', raw, shoff + i * 64)
                for i in range(shnum)]
    strings = sections[shstridx]
    names = raw[strings[4]:strings[4]+strings[5]]
    byname = {names[sh[0]:names.index(0, sh[0])].decode(): sh
              for sh in sections}
    def read_section(name):
        sh = byname[name]
        return raw[sh[4]:sh[4]+sh[5]]
    return byname, read_section

live, bytes_of = extract(sys.argv[1])
assert '.data.dead' not in live
assert live['.got'][5] == 8
relocs = [struct.unpack_from('<QQQ', bytes_of('.rela.dyn'), off)
          for off in range(0, live['.rela.dyn'][5], 24)]
assert relocs == [(live['.data'][3], 3, live['.text'][3])] + \
                 [(0, 0, 0)] * 3, relocs
assert struct.unpack_from('<2Q', bytes_of('.data')) == (
    live['.text'][3], 0x12345678)

dead, unused = extract(sys.argv[2])
assert '.data.dead' not in dead
assert '.got' not in dead or dead['.got'][5] == 0
assert '.rela.dyn' in dead and dead['.rela.dyn'][5] == 24
assert unused('.rela.dyn') == b'\0' * 24
if len(sys.argv) == 5:
    for kind in ('abs_live','global_live','local_live','global_dead','local_dead'):
        for producer in ('mini','gnu'):
            path = Path(sys.argv[1]).parent / f'{kind}.{producer}.elf'
            sections, get_data = extract(str(path))
            entries = [struct.unpack_from('<QQQ',get_data('.rela.dyn'),off)
                       for off in range(0,sections['.rela.dyn'][5],24)] \
                if '.rela.dyn' in sections else []
            print(f'MINILD_LOONG_GNU_DYNREL_CLASS={kind}:{producer} '
                  f'count={len(entries)} types={[r[1]&0xffffffff for r in entries]}',
                  flush=True)
    for label, mini, gnu in [('live', live, extract(sys.argv[3])[0]),
                              ('dead_only', dead, extract(sys.argv[4])[0])]:
        mini_n = mini.get('.rela.dyn', (0, 0, 0, 0, 0, 0))[5] // 24
        gnu_n = gnu.get('.rela.dyn', (0, 0, 0, 0, 0, 0))[5] // 24
        print(f'MINILD_LOONG_GNU_DYNREL_CLASS={label} '
              f'mini={mini_n} gnu={gnu_n}',flush=True)
        assert mini_n == gnu_n, (label, 'rela.dyn slot count', mini_n, gnu_n)
        mini_bytes = extract(sys.argv[1 if label == 'live' else 2])[1]
        gnu_bytes = extract(sys.argv[3 if label == 'live' else 4])[1]
        def types(contents):
            return [struct.unpack_from('<Q', contents, off + 8)[0] & 0xffffffff
                    for off in range(0, len(contents), 24)]
        mtypes = types(mini_bytes('.rela.dyn')) if mini_n else []
        gtypes = types(gnu_bytes('.rela.dyn')) if gnu_n else []
        assert mtypes == gtypes, (label, 'rela.dyn relocation types', mtypes, gtypes)
    print('MINILD_STATIC_SCRIPT_DISCARD_GNU_ORACLE=PASS modes=live,dead_only '
          'count_and_type_parity=true')
print('MINILD_STATIC_SCRIPT_DISCARD_RESERVE=PASS mixed_relative=1 none=3 '
      'dead_only_relative=0 dead_only_none=1 '
      'discarded_runtime_relocs=NONE local_discard_filtered=true '
      'global_abs_reserve_none=1 nonalloc_no_reservation=true '
      'dead_only_synthetic_got=0')
PY
