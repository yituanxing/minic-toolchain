#!/usr/bin/env bash
# GNU GAS oracle for lexical C over an immutable RV32I/RV64I file ISA.
# A positive .option rvc changes instruction-site permission and sets a
# sticky ELF e_flags bit, but push/pop must restore the file ISA/mapping.
set -Eeuo pipefail
: "${MINIAS:?point MINIAS at the exact-head MiniAS executable}"

for program in riscv64-linux-gnu-as riscv64-linux-gnu-objcopy readelf cmp python3; do
    if ! command -v "$program" >/dev/null 2>&1; then
        if [[ ${REQUIRE_ALL_GNU:-0} == 1 ]]; then
            echo "MINIAS_RV_RVC_SITE=BLOCKED missing=$program" >&2
            exit 1
        fi
        echo "MINIAS_RV_RVC_SITE=SKIP missing=$program"
        exit 0
    fi
done

work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

write_scenario() {
    case "$1" in
        empty)
            cat <<'ASM'
.text
.option push
.option rvc
.option pop
addi a0,a0,1
ASM
            ;;
        emitted)
            cat <<'ASM'
.text
.option push
.option rvc
c.li a0,1
addi a1,a1,1
.option pop
addi a2,a2,1
ASM
            ;;
        toggle)
            cat <<'ASM'
.text
.option push
.option rvc
c.li a0,1
.option norvc
addi a1,a1,1
.option rvc
c.li a2,2
.option pop
addi a3,a3,1
ASM
            ;;
        align)
            cat <<'ASM'
.text
.option push
.option rvc
c.li a0,1
.option pop
.p2align 2
addi a1,a1,1
ASM
            ;;
    esac
}

cases=0
for bits in 32 64; do
    if [[ $bits == 32 ]]; then mabi=ilp32; else mabi=lp64; fi
    march="rv${bits}i2p1"
    for scenario in empty emitted toggle align; do
        stem="$work/$bits-$scenario"
        write_scenario "$scenario" >"$stem.s"
        riscv64-linux-gnu-as --no-pad-sections "-march=$march" "-mabi=$mabi" \
            -mno-relax -o "$stem.gnu.o" "$stem.s"
        "$MINIAS" --target=riscv "-march=$march" "-mabi=$mabi" \
            -o "$stem.mini.o" "$stem.s"
        for section in .text .riscv.attributes; do
            for producer in gnu mini; do
                riscv64-linux-gnu-objcopy -O binary --only-section="$section" \
                    "$stem.$producer.o" "$stem.$producer.${section#.}"
            done
            if ! cmp -s "$stem.gnu.${section#.}" "$stem.mini.${section#.}"; then
                echo "MINIAS_RV_RVC_SITE=FAIL xlen=$bits scenario=$scenario section=$section" >&2
                exit 1
            fi
        done
        python3 - "$stem.gnu.o" "$stem.mini.o" "$bits" "$scenario" <<'PY'
import re
import subprocess
import sys

gnu, mini, bits, scenario = sys.argv[1:]
def output(path, *args):
    return subprocess.check_output(['readelf', *args, path], text=True)
def flag_rvc(path):
    return 'RVC' in next(s for s in output(path, '-h').splitlines() if 'Flags:' in s)
def attribute(path):
    return next(s.strip() for s in output(path, '-A').splitlines() if 'Tag_RISCV_arch:' in s)
def mapping(path):
    sections = {}
    for line in output(path, '-SW').splitlines():
        match = re.match(r'^\s*\[\s*(\d+)\]\s+(\S+)', line)
        if match:
            sections[match.group(1)] = match.group(2)
    result = []
    for line in output(path, '-sW').splitlines():
        cols = line.split()
        if len(cols) < 8 or not re.fullmatch(r'\d+:', cols[0]):
            continue
        name = cols[7]
        if name in ('$d', '$x') or name.startswith(('$d.', '$x.', '$xrv')):
            result.append((cols[6], sections.get(cols[6], '?'), int(cols[1], 16), name))
    return result

assert flag_rvc(gnu) and flag_rvc(mini), (bits, scenario, 'RVC not sticky')
expected_arch = f'Tag_RISCV_arch: "rv{bits}i2p1"'
assert attribute(gnu) == attribute(mini) == expected_arch, (
    bits, scenario, attribute(gnu), attribute(mini))
gnu_mapping, mini_mapping = mapping(gnu), mapping(mini)
assert gnu_mapping and gnu_mapping == mini_mapping, (
    bits, scenario, 'mapping mismatch', gnu_mapping, mini_mapping)
print(f'MINIAS_RV_RVC_SITE=PASS xlen={bits} scenario={scenario} '
      'text=exact attributes=exact flags=RVC mapping=exact')
PY
        cases=$((cases+1))
    done

    # pop restores lexical C permission, independently of the sticky ELF bit.
    stem="$work/$bits-forbidden"
    printf '.text\n.option push\n.option rvc\nc.li a0,1\n.option pop\nc.li a1,2\n' >"$stem.s"
    if "$MINIAS" --target=riscv "-march=$march" "-mabi=$mabi" \
        -o "$stem.mini.o" "$stem.s" >"$stem.out" 2>"$stem.err"; then
        echo "MINIAS_RV_RVC_SITE=FAIL xlen=$bits C permission leaked after pop" >&2
        exit 1
    fi
    grep -q 'unsupported-isa-feature:c.li' "$stem.err"
    if riscv64-linux-gnu-as "-march=$march" "-mabi=$mabi" \
        -mno-relax -o "$stem.gnu.o" "$stem.s" >"$stem.gnu.out" 2>"$stem.gnu.err"; then
        echo "MINIAS_RV_RVC_SITE=FAIL GNU unexpectedly accepted C outside site" >&2
        exit 1
    fi
done

echo "MINIAS_RV_RVC_SITE_ORACLE=PASS cases=$cases invalid_outside_site=2"
