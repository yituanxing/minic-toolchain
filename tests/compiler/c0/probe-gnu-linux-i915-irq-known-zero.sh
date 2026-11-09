#!/bin/sh
# Linux v6.6.143 intel_gt_irq.c: conditional low-bit OR into a local followed
# by BUILD_BUG_ON(irqs & 0xffff0000). This fixture is source-model evidence;
# it does not authenticate the exact owner in a frozen GNU Linux .i corpus.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
mini=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host=${HOST_CC:-cc}
source="$root/tests/compiler/c0/gnu_linux_i915_irq_known_zero.c"
work=$(mktemp -d)
trap 'rm -r -- "$work"' EXIT HUP INT TERM

"$host" -E -P -std=gnu11 "$source" -o "$work/known.i"
"$host" -std=gnu11 -O2 -S "$work/known.i" -o "$work/known.gnu.s"
"$mini" --target=x86_64-linux -S "$work/known.i" -o "$work/known.mini.s"
"$host" -c -x assembler "$work/known.gnu.s" -o "$work/known.gnu.o"
"$host" -c -x assembler "$work/known.mini.s" -o "$work/known.mini.o"

if nm -u "$work/known.gnu.o" | grep -Fq '__compiletime_assert_461'; then
    echo 'LINUX_I915_IRQ_KNOWN_ZERO=FAIL GNU -O2 retained impossible assertion' >&2
    exit 1
fi
if nm -u "$work/known.mini.o" | grep -Fq '__compiletime_assert_461'; then
    echo 'LINUX_I915_IRQ_KNOWN_ZERO=FAIL MiniC retained impossible assertion' >&2
    exit 1
fi

"$host" -no-pie "$work/known.gnu.o" -o "$work/known.gnu"
"$host" -no-pie "$work/known.mini.o" -o "$work/known.mini"
set +e
"$work/known.gnu"
gnu_rc=$?
"$work/known.mini"
mini_rc=$?
set -e
if [ "$gnu_rc" -ne 42 ] || [ "$mini_rc" -ne "$gnu_rc" ]; then
    echo "LINUX_I915_IRQ_KNOWN_ZERO=FAIL native GNU=$gnu_rc MiniC=$mini_rc" >&2
    exit 1
fi

# Unknown incoming high bits, address escape and volatile local must *not*
# become false zero proofs. These are runtime calls, not suppressed linker
# references; GNU and MiniC should preserve their undefined symbol ownership.
"$host" -E -P -std=gnu11 -DMINIC_NEGATIVE_CASES "$source" \
    -o "$work/negative.i"
"$host" -std=gnu11 -O2 -c "$work/negative.i" -o "$work/negative.gnu.o"
"$mini" --target=x86_64-linux -S "$work/negative.i" -o "$work/negative.mini.s"
"$host" -c -x assembler "$work/negative.mini.s" -o "$work/negative.mini.o"
for object in "$work/negative.gnu.o" "$work/negative.mini.o"; do
    for symbol in runtime_dynamic_highbits_probe runtime_constant_highbits_probe \
                  runtime_escaped_highbits_probe runtime_volatile_highbits_probe \
                  mutate_irqs; do
        if ! nm -u "$object" | grep -Fq "$symbol"; then
            echo "LINUX_I915_IRQ_KNOWN_ZERO=FAIL lost negative owner $object $symbol" >&2
            exit 1
        fi
    done
done

printf 'LINUX_I915_IRQ_KNOWN_ZERO=PASS GNU_E=1 GNU_O2=1 MiniC_S=1 GNU_AS=4 GNU_LD=2 GNU_runtime=%s MiniC_runtime=%s negative=4\n' \
    "$gnu_rc" "$mini_rc"
