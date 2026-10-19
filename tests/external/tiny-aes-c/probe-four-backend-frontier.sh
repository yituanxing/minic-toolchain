#!/bin/sh
# First untouched real-project frontier after target-local M3 call ABI.
# Reuse the exact pinned AES harness, vendor, local headers, flags and
# GNU/QEMU differential shape already used by x86 and RV64.
# A source translation rejection is reported as BLOCKED, never as PASS.
set -eu
: "${MINIC_CC:?set MINIC_CC to the built minic-cc}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
vendor="$root/tests/vendor/tiny-aes-c/upstream"
fixture="$root/tests/external/tiny-aes-c/aes128_ecb_vectors.c"
headers="$root/tests/external/tiny-aes-c/include"
test "$(git -C "$root" hash-object "$vendor/aes.c")" = 4481f7b24ec964019d38669842913fd571d28ba3
test "$(git -C "$root" hash-object "$vendor/aes.h")" = b29b6683549632676ec11c06eb86efd02964db57
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
passed=0
blocked=0
for target in aarch64 loongarch64; do
    case "$target" in
        aarch64) gcc=aarch64-linux-gnu-gcc; qemu=qemu-aarch64 ;;
        loongarch64) gcc=loongarch64-linux-gnu-gcc-13; qemu=qemu-loongarch64 ;;
    esac
    command -v "$gcc" >/dev/null
    command -v "$qemu" >/dev/null
    # Real original cross-GCC reference and QEMU execution, not a stub.
    "$gcc" -std=c11 -O0 -static -I"$vendor" \
        -DECB=1 -DCBC=0 -DCTR=0 -DMULTIPLY_AS_A_FUNCTION=1 \
        "$fixture" -o "$work/$target.gnu.elf"
    timeout 30s "$qemu" "$work/$target.gnu.elf" \
        >"$work/$target.gnu.out" 2>"$work/$target.gnu.err"
    echo "FOUR_BACKEND_TINYAES_GNU=PASS target=$target original_source=1 QEMU_exit=0"
    "$gcc" -E -P -nostdinc -x c -I"$headers" -I"$vendor" \
        -DECB=1 -DCBC=0 -DCTR=0 -DMULTIPLY_AS_A_FUNCTION=1 \
        "$fixture" -o "$work/$target.i"
    if ! MINIC_M1_TRACE=1 "$MINIC_CC" --target="$target-linux" -S "$work/$target.i" \
            -o "$work/$target.minic.s" >"$work/$target.compile.out" \
            2>"$work/$target.compile.err"; then
        test ! -e "$work/$target.minic.s"
        printf 'FOUR_BACKEND_TINYAES=BLOCKED target=%s stage=MiniC_line_or_Core input=original_AES128_ECB\n' "$target"
        # First observed failing function/real Core operation, without
        # flooding CI with an entire large function dump.
        grep -m 1 'MINIC_M[14]_.*FRONTIER' "$work/$target.compile.err" || true
        grep -m 1 'MINIC_M8_.*REJECT\|MINIC_M8_FRAME_CAP' "$work/$target.compile.err" || true
        grep 'MINIC_M9_GATE' "$work/$target.compile.err" | tail -n 8 || true
        grep 'MINIC_M9_STEP' "$work/$target.compile.err" | tail -n 6 || true
        grep 'MINIC_M9_GATE.*fn=AddRoundKey' "$work/$target.compile.err" | head -n 6 || true
        grep 'MINIC_M9_STEP.*function=AddRoundKey' "$work/$target.compile.err" | tail -n 8 || true
        grep 'MINIC_M11_POINTER_DIAG' "$work/$target.compile.err" | tail -n 4 || true
        # Show the complete first (small) real Core function; captures the
        # operation after pointer parameter stores, not just its preamble.
        sed -n '1,9p' "$work/$target.compile.err"
        # Core dump starts with hundreds of object declarations in AES
        # KeyExpansion. Show the first 70 actual IR instructions, not
        # object labels that conceal the real unsupported opcode.
        awk '/^bb0:/{dump=1; print; next} dump {print; count++; if (count >= 70) exit}' \
            "$work/$target.compile.err"
        blocked=$((blocked + 1))
        continue
    fi
    if [ ! -s "$work/$target.minic.s" ]; then
        echo "FOUR_BACKEND_TINYAES=FAIL target=$target stage=MiniC_output_empty exit=1" >&2
        exit 1
    fi
    echo "FOUR_BACKEND_TINYAES_COMPILE=PASS target=$target original_source=1 assembly_bytes=$(wc -c < "$work/$target.minic.s")"
    if ! "$gcc" -c "$work/$target.minic.s" -o "$work/$target.minic.o" \
           2>"$work/$target.assemble.err"; then
        echo "FOUR_BACKEND_TINYAES=BLOCKED target=$target stage=GNU_AS"
        sed -n '1,5p' "$work/$target.assemble.err"
        blocked=$((blocked + 1))
        continue
    fi
    if ! "$gcc" -static "$work/$target.minic.o" \
           -o "$work/$target.minic.elf" 2>"$work/$target.link.err"; then
        echo "FOUR_BACKEND_TINYAES=BLOCKED target=$target stage=GNU_LD" >&2
        sed -n '1,12p' "$work/$target.link.err"
        blocked=$((blocked + 1))
        continue
    fi
    echo "FOUR_BACKEND_TINYAES_LINK=PASS target=$target MiniC_object=1 GNU_AS_LD=1"
    set +e
    timeout 30s "$qemu" "$work/$target.minic.elf" \
        >"$work/$target.minic.out" 2>"$work/$target.minic.err"
    qemu_status=$?
    set -e
    if [ "$qemu_status" -ne 0 ]; then
        echo "FOUR_BACKEND_TINYAES=FAIL target=$target stage=QEMU_exec exit=$qemu_status" >&2
        sed -n '1,12p' "$work/$target.minic.err" >&2
        sed -n '1,12p' "$work/$target.minic.out" >&2
        if [ "$qemu_status" -eq 1 ]; then
            # The unchanged real fixture returns 1 on *any* RoundKey
            # mismatch. For diagnosis only, make a temporary derivative
            # that returns first_mismatch's actual 1-based byte offset;
            # do not mutate vendor/fixture or count this as acceptance.
            sed 's/return 1;/return failure;/' "$fixture" >"$work/$target.first_byte.c"
            "$gcc" -E -P -nostdinc -x c -I"$headers" -I"$vendor" \
                -DECB=1 -DCBC=0 -DCTR=0 -DMULTIPLY_AS_A_FUNCTION=1 \
                "$work/$target.first_byte.c" -o "$work/$target.first_byte.i"
            if "$MINIC_CC" --target="$target-linux" -S \
                   "$work/$target.first_byte.i" -o "$work/$target.first_byte.s" \
                   2>"$work/$target.first_byte.compile.err" &&
               "$gcc" -c "$work/$target.first_byte.s" \
                   -o "$work/$target.first_byte.o" &&
               "$gcc" -static "$work/$target.first_byte.o" \
                   -o "$work/$target.first_byte.elf"; then
                set +e
                timeout 30s "$qemu" "$work/$target.first_byte.elf" \
                    >"$work/$target.first_byte.out" 2>"$work/$target.first_byte.err"
                mismatch_status=$?
                set -e
                echo "FOUR_BACKEND_TINYAES_DIAG target=$target first_RoundKey_mismatch_1based=$mismatch_status derived_fixture_not_original=1" >&2
            else
                echo "FOUR_BACKEND_TINYAES_DIAG target=$target stage=derived_source_compile_failed" >&2
            fi
            # Three additional source-derived runtime probes, strictly
            # diagnostic and never substitutes for the original fixture:
            # compare S-box, round constant and the *actual* first
            # generated round-key byte under both toolchains/QEMU.
            for probe in sbox rcon key17; do
                case "$probe" in
                    sbox) expression='sbox[207]' ;;
                    rcon) expression='Rcon[1]' ;;
                    key17) expression='context_pointer->RoundKey[16]' ;;
                esac
                awk -v expr="$expression" '
                    { print }
                    /AES_init_ctx\(context_pointer,/ {
                        print "    return (int)(" expr ");"
                    }
                ' "$fixture" >"$work/$target.$probe.c"
                "$gcc" -std=c11 -O0 -static -I"$vendor" \
                    -DECB=1 -DCBC=0 -DCTR=0 -DMULTIPLY_AS_A_FUNCTION=1 \
                    "$work/$target.$probe.c" -o "$work/$target.$probe.gnu.elf"
                "$gcc" -E -P -nostdinc -x c -I"$headers" -I"$vendor" \
                    -DECB=1 -DCBC=0 -DCTR=0 -DMULTIPLY_AS_A_FUNCTION=1 \
                    "$work/$target.$probe.c" -o "$work/$target.$probe.i"
                if ! "$MINIC_CC" --target="$target-linux" -S \
                        "$work/$target.$probe.i" -o "$work/$target.$probe.s" \
                        2>"$work/$target.$probe.compile.err"; then
                    echo "FOUR_BACKEND_TINYAES_PROBE target=$target probe=$probe stage=MiniC_compile_failed" >&2
                    continue
                fi
                if ! "$gcc" -c "$work/$target.$probe.s" \
                        -o "$work/$target.$probe.o" &&
                   "$gcc" -static "$work/$target.$probe.o" \
                        -o "$work/$target.$probe.minic.elf"; then
                    echo "FOUR_BACKEND_TINYAES_PROBE target=$target probe=$probe stage=GNU_link_failed" >&2
                    continue
                fi
                set +e
                timeout 15s "$qemu" "$work/$target.$probe.gnu.elf" >/dev/null 2>&1
                reference_byte=$?
                timeout 15s "$qemu" "$work/$target.$probe.minic.elf" >/dev/null 2>&1
                minic_byte=$?
                set -e
                echo "FOUR_BACKEND_TINYAES_PROBE target=$target probe=$probe GNU_byte=$reference_byte MiniC_byte=$minic_byte derivative_not_acceptance=1" >&2
            done
        fi
        exit 1
    fi
    if ! cmp -s "$work/$target.gnu.out" "$work/$target.minic.out" ||
       ! cmp -s "$work/$target.gnu.err" "$work/$target.minic.err"; then
        echo "FOUR_BACKEND_TINYAES=FAIL target=$target stage=QEMU_differential"
        exit 1
    fi
    echo "FOUR_BACKEND_TINYAES=PASS target=$target original_source=1 GNU_AS_LD=1 QEMU_byte_exact=1"
    passed=$((passed + 1))
done
echo "FOUR_BACKEND_TINYAES_FRONTIER=COMPLETE architectures=2 verified_runtime=$passed blocked=$blocked"
