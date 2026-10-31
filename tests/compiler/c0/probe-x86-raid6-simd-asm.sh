#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/debug/bin/minic-cc"}
gnu=${GNU_CC:-gcc}
qemu=${QEMU_X86_64:-qemu-x86_64}
work=${BUILD_DIR:-"$root/build/debug"}/tests/x86-raid6-simd
mkdir -p "$work"

# Exact same GNU-E TU is MiniC's source. The original x86 kernel build
# disables compiler-generated SIMD but enables privileged kernel-owned ASM.
for isa in sse2 avx2 avx512; do
    if [ "$isa" = sse2 ]; then
        source=x86_raid6_simd_native_gnu.c
    else
        source=x86_raid6_${isa}_native_gnu.c
    fi
    "$gnu" -E -P -x c "$root/tests/compiler/c0/$source" -o "$work/$isa.i"
    "$minic" --target=x86_64-linux -S "$work/$isa.i" -o "$work/$isa.minic.s"
    "$gnu" -static "$work/$isa.minic.s" -o "$work/$isa.minic.elf"
    "$gnu" -static -O2 -mno-sse -mno-mmx -mno-avx \
        "$root/tests/compiler/c0/$source" -o "$work/$isa.gnu.elf"
    objdump -d "$work/$isa.minic.elf" > "$work/$isa.minic.disasm"
    objdump -d "$work/$isa.gnu.elf" > "$work/$isa.gnu.disasm"
    case "$isa" in
        sse2) mnemonic=pxor ;;
        avx2) mnemonic=vpxor ;;
        avx512) mnemonic=vpxorq ;;
    esac
    grep -E "[[:space:]]$mnemonic[[:space:]]" "$work/$isa.minic.disasm" >/dev/null
    grep -E "[[:space:]]$mnemonic[[:space:]]" "$work/$isa.gnu.disasm" >/dev/null
    if [ "$isa" = avx512 ]; then
        # This QEMU TCG build explicitly reports unsupported AVX512F/BW;
        # native AVX512 hardware serves as the independent execution oracle.
        if ! grep -m1 'avx512bw' /proc/cpuinfo >/dev/null; then
            echo 'FAIL native AVX512BW unavailable (no vector runtime test)' >&2
            exit 1
        fi
        "$work/$isa.gnu.elf"
        "$work/$isa.minic.elf"
    else
        "$qemu" -cpu max "$work/$isa.gnu.elf"
        "$qemu" -cpu max "$work/$isa.minic.elf"
    fi
done

# Negative cases must not compile through a weak generic ASM fallback.
for bad in 1 2 3 4 5; do
    "$gnu" -E -P -x c -DBAD_FORM="$bad" \
        "$root/tests/compiler/c0/x86_raid6_simd_reject.c" \
        -o "$work/bad-$bad.i"
    if "$minic" --target=x86_64-linux -S "$work/bad-$bad.i" \
        -o "$work/bad-$bad.s" >"$work/bad-$bad.out" \
        2>"$work/bad-$bad.err"; then
        echo "FAIL unsupported SIMD ASM template $bad was silently accepted" >&2
        exit 1
    fi
done

# The six *full* GNU-preprocessed Linux 6.6.143 C owners must each produce a
# real GNU-assembled native object. Missing input is an explicit FAIL, never
# an omitted owner marked green. Set LINUX_RAID6_I_ROOT to the frozen tree.
if [ -z "${LINUX_RAID6_I_ROOT:-}" ]; then
    echo 'FAIL LINUX_RAID6_I_ROOT required for all six Linux GNU .i owners' >&2
    exit 1
fi
for name in sse2 avx2 avx512 recov_ssse3 recov_avx2 recov_avx512; do
    if [ ! -f "$LINUX_RAID6_I_ROOT/$name.i" ]; then
        echo "FAIL missing Linux RAID6 GNU owner: $name.i" >&2
        exit 1
    fi
    "$minic" --target=x86_64-linux -S "$LINUX_RAID6_I_ROOT/$name.i" \
        -o "$work/linux-$name.s"
    "$gnu" -x assembler -c "$work/linux-$name.s" -o "$work/linux-$name.o"
    objdump -d "$work/linux-$name.o" > "$work/linux-$name.disasm"
    case "$name" in
        sse2) mnemonic=pxor ;;
        avx2) mnemonic=vpxor ;;
        avx512) mnemonic=vpxorq ;;
        recov_ssse3) mnemonic=pshufb ;;
        recov_avx2|recov_avx512) mnemonic=vpshufb ;;
    esac
    grep -E "[[:space:]]$mnemonic[[:space:]]" \
        "$work/linux-$name.disasm" >/dev/null || {
        echo "FAIL $name object lacks real $mnemonic hardware instructions" >&2
        exit 1
    }
done

echo 'PASS x86 RAID6 6/6 original GNU .i->MiniC->GNU AS; SSE2+AVX2 GNU/MiniC QEMU; AVX512 GNU/MiniC native; five negatives rejected'
