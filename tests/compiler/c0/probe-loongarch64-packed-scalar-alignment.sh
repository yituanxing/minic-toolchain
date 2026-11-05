#!/bin/sh
# Core memory pointer provenance is not an alignment proof. This source-only
# instruction-shape smoke observes byte gather/scatter in the physical MIR;
# it is not a substitute for GNU AS/QEMU runtime value oracles.
# the old direct emitter's guarded native fast path and pcaddi helper were
# performance mechanisms, not conditions for safe C packed memory semantics.
set -eu
: "${MINIC_CC:?MINIC_CC path required}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

${CC:-cc} -E -P -x c \
    "$root/tests/compiler/c0/loongarch64_packed_bitfield_alignment.c" \
    -o "$work/source.i"
"$MINIC_CC" --target=loongarch64-linux -S "$work/source.i" \
    -o "$work/source.s"

for name in read_packed_high write_packed_high read_packed_tail \
            read_packed_scalar write_packed_scalar read_packed_pointer \
            read_natural_scalar read_natural_pointer \
            read_packed_wide write_packed_wide read_static_wide; do
    grep -Eq "^${name}:$" "$work/source.s" || {
        echo "LOONGARCH_PACKED_ALIGNMENT=FAIL missing $name" >&2; exit 1;
    }
done

awk '
    function expect(minread, minwrite, signed32) {
        if (reads < minread || writes < minwrite ||
            (signed32 && sign32 == 0) || unsafe_native) {
            printf "LOONGARCH_PACKED_ALIGNMENT=FAIL function=%s byte_reads=%d byte_writes=%d sign32=%d unsafe_native=%d expected_read=%d expected_write=%d\n", name, reads, writes, sign32, unsafe_native, minread, minwrite > "/dev/stderr"
            bad++
        }
        checked++
    }
    function finish() {
        if (name == "read_packed_high") expect(4, 0, 0)
        else if (name == "write_packed_high") expect(4, 4, 0)
        else if (name == "read_packed_tail") expect(4, 0, 0)
        else if (name == "read_packed_scalar") expect(4, 0, 1)
        else if (name == "write_packed_scalar") expect(4, 4, 0)
        else if (name == "read_packed_pointer") expect(8, 0, 0)
        else if (name == "read_packed_wide") expect(16, 0, 0)
        else if (name == "write_packed_wide") expect(16, 16, 0)
    }
    /^[a-z_][a-z_0-9]*:$/ {
        finish()
        name = substr($0, 1, length($0) - 1)
        reads = writes = sign32 = unsafe_native = 0
        next
    }
    /^  ld\.bu \$t[0-9]+, \$t1, [0-9]+$/ { reads++ }
    /^  st\.b \$t[0-9]+, \$t1, [0-9]+$/ { writes++ }
    /^  add\.w \$t[0-9]+, \$t[0-9]+, \$zero$/ { sign32++ }
    /^  (ld\.(h|hu|w|wu|d)|st\.(h|w|d)) \$[a-z0-9]+, \$t1, [-0-9]+$/ {
        unsafe_native++
    }
    END {
        finish()
        if (checked != 8 || bad) exit 1
    }
' "$work/source.s"

# Aligned native fast paths and the shared helper remain a separate
# optimization migration. This structural observation alone does not prove
# execution semantics or generic address provenance.
echo 'LOONGARCH_PACKED_ALIGNMENT=PASS -S byte_transport_smoke=8 sign32_observed=1 native_t1_unguarded=NOT_OBSERVED aligned_fastpath=NOT_MIGRATED GNU_QEMU=NOT_RUN'
