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

# MachineIR may select either an unrolled byte-only transport or a fast
# native-width load/store with a physical address-alignment branch and a
# bounded byte-loop fallback. Count loop trips, not textual byte opcodes.
# Frame spill/reload accesses are unrelated to the packed C lvalue.
awk '
  /^[a-z_][a-z_0-9]*:$/ {
    name=substr($0,1,length($0)-1)
  }
  /^  andi \$t3, \$(t1|t7), (3|7)$/ {
    mask=($0 ~ /, 3$/ ? 3 : 7)
    if (getline branch != 1 ||
        branch !~ /^  bnez \$t3, \.Lminic_mir_/ ||
        getline fast_label != 1 ||
        fast_label !~ /^\.Lminic_mir_[a-z_0-9]+_[0-9]+:$/ ||
        getline native != 1 ||
        native !~ /^  (ld\.(w|wu|d)|st\.(w|d)) \$t[0-9]+, \$(t1|t7), 0$/ ||
        (mask == 3 && native !~ /^  (ld\.(w|wu)|st\.w) /) ||
        (mask == 7 && native !~ /^  (ld\.d|st\.d) /)) {
      printf "LOONGARCH_PACKED_ALIGNMENT=FAIL unguarded native function=%s line=%d\n", name, NR > "/dev/stderr"
      bad++
    }
    gate[name]++
    if (mask==3) gate4[name]++
    else gate8[name]++
  }
  /^  ld\.bu \$t[0-9]+, \$(t1|t7), [0-9]+$/ { byte_read[name]++ }
  /^  st\.b \$t[0-9]+, \$(t1|t7), [0-9]+$/ { byte_write[name]++ }
  /^  li\.d \$t[34], (4|8)$/ {
    if ($0 ~ /, 4$/) trip4[name]++
    else trip8[name]++
  }
  /^  addi\.d \$t[34], \$t[34], -1$/ { loop_count[name]++ }
  /^  bnez \$t[34], \.Lminic_mir_/ { loop_branch[name]++ }
  /^  jirl \$zero, \$t6, 0$/ { helper_return[name]++ }
  /^  pcaddi \$t6, 2$/ {
    pc[name]++
    if (getline branch != 1 ||
        branch !~ /^  b \.Lminic_mir_[a-z_0-9]+_[0-9]+$/ ||
        getline continuation != 1 ||
        continuation !~ /^\.Lminic_mir_[a-z_0-9]+_[0-9]+:$/) {
      printf "LOONGARCH_PACKED_ALIGNMENT=FAIL pcaddi PC+8 function=%s line=%d\n", name, NR > "/dev/stderr"
      bad++
    }
  }
  END {
    if (ENVIRON["MINIC_PACKED_ALIGNMENT_TRACE"] != "") {
      for (n in gate)
        printf "PACKED_TRACE %s gate=%d gate4=%d gate8=%d byte_r=%d byte_w=%d trip4=%d trip8=%d pc=%d loop=%d loopbranch=%d return=%d\n", n, gate[n], gate4[n], gate8[n], byte_read[n], byte_write[n], trip4[n], trip8[n], pc[n], loop_count[n], loop_branch[n], helper_return[n] > "/dev/stderr"
    }
    # Packed bitfields operate on 3/2 source bytes; they may not replace
    # an unaligned field by an unchecked native word/dword load/store.
    if (byte_read["read_packed_high"] < 3 ||
        byte_read["write_packed_high"] < 3 ||
        byte_write["write_packed_high"] < 3 ||
        byte_read["read_packed_tail"] < 2 ||
        gate["read_packed_high"] || gate["write_packed_high"] ||
        gate["read_packed_tail"] ||
        pc["read_packed_high"] || pc["write_packed_high"] ||
        pc["read_packed_tail"]) bad++
    # A 4/8-byte packed scalar uses either all-byte lowering or exactly
    # one guarded native instruction with a width-matched byte-loop escape.
    if (!((gate4["read_packed_scalar"]==1 &&
           byte_read["read_packed_scalar"]>=1 &&
           trip4["read_packed_scalar"]>=1 && pc["read_packed_scalar"]==1 &&
           helper_return["read_packed_scalar"]>=1) ||
          (gate["read_packed_scalar"]==0 &&
           byte_read["read_packed_scalar"]>=4))) bad++
    if (!((gate4["write_packed_scalar"]==1 &&
           ((byte_write["write_packed_scalar"]>=4 &&
             pc["write_packed_scalar"]==0) ||
            (byte_write["write_packed_scalar"]>=1 &&
             trip4["write_packed_scalar"]>=1 &&
             pc["write_packed_scalar"]==1 &&
             helper_return["write_packed_scalar"]>=1))) ||
          (gate["write_packed_scalar"]==0 &&
           byte_write["write_packed_scalar"]>=4))) bad++
    if (!((gate8["read_packed_pointer"]==1 &&
           byte_read["read_packed_pointer"]>=1 &&
           trip8["read_packed_pointer"]>=1 && pc["read_packed_pointer"]==1 &&
           helper_return["read_packed_pointer"]>=1) ||
          (gate["read_packed_pointer"]==0 &&
           byte_read["read_packed_pointer"]>=8))) bad++
    # Both halves of a packed 128-bit value must pass independent aligned
    # native gates, but may share one eight-trip byte fallback helper.
    if (!((gate8["read_packed_wide"]==2 &&
           byte_read["read_packed_wide"]>=1 &&
           trip8["read_packed_wide"]>=1 && pc["read_packed_wide"]==2 &&
           helper_return["read_packed_wide"]>=1) ||
          (gate["read_packed_wide"]==0 &&
           byte_read["read_packed_wide"]>=16))) bad++
    if (!((gate8["write_packed_wide"]==2 &&
           byte_write["write_packed_wide"]>=1 &&
           trip8["write_packed_wide"]>=1 && pc["write_packed_wide"]==2 &&
           helper_return["write_packed_wide"]>=1) ||
          (gate["write_packed_wide"]==0 &&
           byte_write["write_packed_wide"]>=16))) bad++
    for (n in gate)
      if (n ~ /^(read|write)_packed_(scalar|pointer|wide)$/ &&
          gate[n] != 0 && pc[n] != 0 &&
          (loop_count[n]<1 || loop_branch[n]<1)) bad++
    # A statically aligned 128-bit global requires no slow path.
    if (gate["read_static_wide"] || pc["read_static_wide"] ||
        byte_read["read_static_wide"] || byte_write["read_static_wide"]) bad++
    if (bad) exit 1
  }
' "$work/source.s" || {
    echo 'LOONGARCH_PACKED_ALIGNMENT=FAIL packed byte transport or native alignment proof' >&2
    exit 1
}
echo 'LOONGARCH_PACKED_ALIGNMENT=PASS -S bitfield_byte_transport=3x3+2 scalar_pointer_guarded_or_bytes=3 wide_two_halves=2 fallback_bounded=1 pcaddi_when_emitted=verified static_global_fastpath=1 GNU_QEMU=NOT_RUN'
