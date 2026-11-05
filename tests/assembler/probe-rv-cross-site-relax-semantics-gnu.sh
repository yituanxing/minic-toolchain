#!/usr/bin/env bash
# A final-link correctness oracle for a RISC-V source whose last
# .option norelax must not mask linker-relaxable CALL sites earlier in it.
#
# This intentionally does NOT demand GNU/MiniAS object-byte identity:
# GNU GAS may also make a non-relocatable local-LLA or ULEB decision from
# its final option state.  Report the GNU baseline's semantic status
# independently, and require MiniAS's *linked* address/length to be right.
set -Eeuo pipefail

: "${MINIAS:?Set MINIAS to the exact MiniAS binary}"
gas=${RISCV_GAS:-riscv64-linux-gnu-as}
linker=${RISCV_LD:-riscv64-linux-gnu-ld}
copy=${RISCV_OBJCOPY:-riscv64-linux-gnu-objcopy}
for program in "$gas" "$linker" "$copy" readelf python3; do
    if ! command -v "$program" >/dev/null 2>&1; then
        if [[ ${REQUIRE_ALL_GNU:-0} == 1 ]]; then
            echo "MINIAS_RV_CROSS_SITE=BLOCKED missing=$program" >&2
            exit 2
        fi
        echo "MINIAS_RV_CROSS_SITE=SKIP missing=$program"
        exit 0
    fi
done

work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

# The local LLA is deliberately emitted while relaxation is disabled.
# A later CALL is independently enabled for relaxation; final norelax
# restores the original mode.  If CALL shrinks 8 -> 4, .Ltarget moves
# but a pre-folded AUIPC+ADDI cannot be patched by the linker.
cat >"$work/lla.s" <<'ASM'
.option norelax
.text
.globl risk_start
risk_start:
    lla a0,.Ltarget
.option relax
    call external_target
.Ltarget:
.globl risk_target
risk_target:
    ret
.option norelax
ASM

# A DWARF-like symbolic range spanning the CALL must likewise retain a
# linker-visible ULEB adjustment if the CALL is shortened.  The final
# .option norelax is intentional and occurs after the ULEB directive.
cat >"$work/uleb.s" <<'ASM'
.option norelax
.text
.globl span_start
span_start:
.option relax
    call external_target
.globl span_end
span_end:
    ret
.section .debug_rnglists,"",@progbits
    .uleb128 span_end-span_start
.option norelax
ASM

cat >"$work/definitions.s" <<'ASM'
.text
.globl external_target
external_target:
    ret
ASM

for bits in 32 64; do
    if [[ $bits == 32 ]]; then
        mabi=ilp32
        emulation=elf32lriscv
    else
        mabi=lp64
        emulation=elf64lriscv
    fi
    march="rv${bits}i2p1"
    "$gas" --no-pad-sections "-march=$march" "-mabi=$mabi" \
        -mno-relax -o "$work/$bits-definitions.o" "$work/definitions.s"
    for scenario in lla uleb; do
        "$gas" --no-pad-sections "-march=$march" "-mabi=$mabi" \
            -mno-relax -o "$work/$bits-$scenario.gnu.o" "$work/$scenario.s"
        "$MINIAS" --target=riscv "-march=$march" "-mabi=$mabi" \
            -o "$work/$bits-$scenario.mini.o" "$work/$scenario.s"
        for producer in gnu mini; do
            for mode in relax no-relax; do
                linked="$work/$bits-$scenario.$producer.$mode.elf"
                "$linker" -m "$emulation" "--$mode" \
                    -e "$( [[ $scenario == lla ]] && echo risk_start || echo span_start )" \
                    -o "$linked" "$work/$bits-$scenario.$producer.o" \
                    "$work/$bits-definitions.o"
                "$copy" --dump-section \
                    ".text=$work/$bits-$scenario.$producer.$mode.text.bin" \
                    "$linked"
                if [[ $scenario == uleb ]]; then
                    # .debug_rnglists is non-ALLOC; -O binary may omit it.
                    "$copy" --dump-section \
                        ".debug_rnglists=$work/$bits-$scenario.$producer.$mode.uleb.bin" \
                        "$linked"
                fi
            done
        done
    done
done

# Verify a real 8 -> 4 CALL shrink BEFORE interpreting the consumer.
# The GNU baseline is independently classified; MiniAS is held to the
# semantic address/range contract even if GNU happens to be wrong too.
python3 - "$work" "$copy" <<'PY'
import pathlib
import re
import subprocess
import sys

root = pathlib.Path(sys.argv[1])
failures = []
gnu_bad = []

def symbol(path, name):
    lines = subprocess.check_output(["readelf", "-Ws", str(path)], text=True)
    for line in lines.splitlines():
        columns = line.split()
        if len(columns) >= 8 and columns[0].endswith(":") and columns[-1] == name:
            return int(columns[1], 16)
    raise RuntimeError(f"missing linked symbol {name}: {path}")

def section_vma(path, name):
    sections = subprocess.check_output(["readelf", "-SW", str(path)], text=True)
    regex = re.compile(r"^\s*\[\s*\d+\]\s+" + re.escape(name) +
                       r"\s+\S+\s+([0-9a-fA-F]+)\b")
    for line in sections.splitlines():
        match = regex.match(line)
        if match:
            return int(match.group(1), 16)
    raise RuntimeError(f"missing section {name}: {path}")

def signed(value, bits):
    return value - (1 << bits) if value & (1 << (bits - 1)) else value

def decode_lla(path, blob, start):
    raw = blob.read_bytes()
    offset = start - section_vma(path, ".text")
    if offset < 0 or offset + 8 > len(raw):
        raise RuntimeError(f"invalid LLA offset {offset}: {path}")
    hiword = int.from_bytes(raw[offset:offset + 4], "little")
    loword = int.from_bytes(raw[offset + 4:offset + 8], "little")
    if (hiword & 0x7f) != 0x17 or (loword & 0x707f) != 0x13:
        raise RuntimeError(f"LLA was not AUIPC+ADDI: {path}")
    return start + signed(hiword & 0xfffff000, 32) + signed(loword >> 20, 12)

def decode_uleb(blob):
    data = blob.read_bytes()
    result = shift = 0
    for byte in data:
        result |= (byte & 0x7f) << shift
        if not byte & 0x80:
            return result
        shift += 7
        if shift > 63:
            break
    raise RuntimeError(f"invalid ULEB payload: {blob}")

for bits in (32, 64):
    for scenario in ("lla", "uleb"):
        start_name, end_name = (
            ("risk_start", "risk_target") if scenario == "lla"
            else ("span_start", "span_end")
        )
        values = {}
        for producer in ("gnu", "mini"):
            for mode in ("no-relax", "relax"):
                stem = f"{bits}-{scenario}.{producer}.{mode}"
                linked = root / f"{stem}.elf"
                start = symbol(linked, start_name)
                end = symbol(linked, end_name)
                actual = (decode_lla(linked, root / f"{stem}.text.bin", start)
                          if scenario == "lla"
                          else decode_uleb(root / f"{stem}.uleb.bin"))
                expected = end if scenario == "lla" else end - start
                values[(producer, mode)] = (start, end, expected, actual)
                correct = actual == expected
                status = "OK" if correct else "BAD"
                print("MINIAS_RV_CROSS_SITE=CASE "
                      f"rv={bits} scenario={scenario} producer={producer} "
                      f"mode={mode} expected=0x{expected:x} "
                      f"actual=0x{actual:x} semantic={status}")
                if not correct:
                    (gnu_bad if producer == "gnu" else failures).append(stem)

            no_relax = values[(producer, "no-relax")]
            relaxed = values[(producer, "relax")]
            before = no_relax[1] - no_relax[0]
            after = relaxed[1] - relaxed[0]
            shrink = before - after
            if shrink != 4:
                failures.append(f"{bits}-{scenario}-{producer}-no-real-call-shrink:{shrink}")
                print("MINIAS_RV_CROSS_SITE=INVALID "
                      f"rv={bits} scenario={scenario} producer={producer} "
                      f"expected_call_shrink=4 actual={shrink}")

if gnu_bad:
    print("MINIAS_RV_CROSS_SITE=GNU_BASELINE_UNSAFE " + ",".join(gnu_bad))
if failures:
    print("MINIAS_RV_CROSS_SITE=FAIL " + ",".join(failures), file=sys.stderr)
    sys.exit(1)
print("MINIAS_RV_CROSS_SITE=PASS rv32_rv64=2 cases=2 "
      "link_modes=2 mini_final_semantics=correct")
PY
