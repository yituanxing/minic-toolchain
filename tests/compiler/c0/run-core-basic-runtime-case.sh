#!/bin/sh
# RV64 Core production-code differential against GNU. One compiler path.
set -eu
case "${1:-}" in
    core-condition-and|core-fixed-call-scalar-conversions|core-for-postincrement-m20|core-global-scalar-memory|core-integer-bitwise-and-assignment|core-integer-bitwise-not|core-integer-equality|core-integer-or-shifts-m22|core-logical-and-value|core-nested-if-continuation|core-integer-add-overflow|core-integer-multiply-overflow|core-integer-subtract-overflow|core-pointer-condition-m21|core-pointer-equality-qualifiers|core-pointer-offset|core-scalar-not-equal|core-short-circuit-or|core-scalar-assignment-implicit-void|core-scalar-lvalue-bitcast|core-opaque-inline-asm|core-statement-expression|core-target-constant-fallback) name=$1 ;;
    *) echo "usage: $0 <known-core-basic-runtime-case>" >&2; exit 64 ;;
esac
test "$#" -eq 1 || exit 64
case "$name" in
    core-integer-or-shifts-m22|core-pointer-condition-m21)
        : "${MINIC:?MINIC is required}"
        : "${BUILD_DIR:?BUILD_DIR is required}"
        : "${RISCV_CC:=riscv64-linux-gnu-gcc}"
        : "${QEMU_RISCV64:=qemu-riscv64}"
        : "${HOST_CC:=cc}"
        host_cc=$HOST_CC ;;
    *)
        : "${MINIC:?MINIC must point to the compiler binary}"
        : "${RISCV_CC:=riscv64-linux-gnu-gcc}"
        : "${QEMU_RISCV64:=qemu-riscv64}"
        host_cc=cc ;;
esac
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work="${BUILD_DIR:-$root/build/$name}"
stem=$(printf '%s' "$name" | tr '-' '_')
source_file="$root/tests/compiler/c0/$stem.c"
runtime_file="$root/tests/compiler/c0/${stem}_runtime.c"
mkdir -p "$work"
case "$name" in
    core-fixed-call-scalar-conversions|core-global-scalar-memory|core-integer-bitwise-not|core-integer-equality|core-integer-add-overflow|core-integer-multiply-overflow|core-scalar-assignment-implicit-void|core-scalar-lvalue-bitcast)
        input="$work/$stem.i"; core="$work/$stem-core.s" ;;
    core-short-circuit-or)
        input="$work/$stem.i"; core="$work/core.s" ;;
    *)
        input="$work/input.i"; core="$work/core.s" ;;
esac
"$host_cc" -E -P -std=gnu11 "$source_file" -o "$input"
"$MINIC" -S "$input" -o "$core"

# Declarative per-case checks, in the original execution order.
case "$name" in
    core-condition-and)
        grep -q '^core_m12_short_circuit_and:' "$core"
        grep -q '^core_m12_wrapped_pointer_and:' "$core"
        ;;
    core-fixed-call-scalar-conversions)
        for symbol in core_m3_integer_conversion core_m3_pointer_qualification core_m3_null_pointer core_m3_pointer_bool core_m3_read_word_at_a_time; do
            grep -q "^${symbol}:" "$core"
            grep -q "${symbol}_core_bb0" "$core"
        done
        ;;
    core-for-postincrement-m20)
        for symbol in core_m20_for_assign core_m20_postincrement core_m20_list_count_nodes; do
            grep -q "^${symbol}:" "$core"
        done
        ;;
    core-global-scalar-memory)
        grep -q '^core_m5_global_load:' "$core"
        grep -q '^core_m5_global_store:' "$core"
        grep -q 'la t0, core_m5_global' "$core"
        ;;
    core-integer-bitwise-and-assignment)
        for symbol in core_m14_bitwise_and core_m14_compound_int core_m14_compound_uchar core_m14_compound_bool core_m14_single_lvalue core_m14_linux_tail; do
            grep -q "^${symbol}:" "$core"
        done
        ;;
    core-integer-bitwise-not)
        grep -q '^core_m7_not_int:' "$core"
        grep -q '^core_m7_not_uint:' "$core"
        grep -q '^core_m7_not_long:' "$core"
        grep -q '^core_m7_not_ulong:' "$core"
        grep -q '^core_m7_size_max:' "$core"
        grep -q 'xori t0, t0, -1' "$core"
        ;;
    core-integer-equality)
        grep -q '^core_m5b_equal:' "$core"
        grep -q '^core_m5b_set_if_equal:' "$core"
        grep -q '^core_m11_pointer_equal:' "$core"
        grep -q '^core_m11_member_pointer_equal:' "$core"
        grep -q 'xor t0, t0, t1' "$core"
        grep -q 'seqz t0, t0' "$core"
        grep -q 'la t0, core_m5b_global' "$core"
        ;;
    core-integer-or-shifts-m22)
        grep -F 'or t0, t0, t1' "$core" >/dev/null
        grep -F 'sll t0, t0, t1' "$core" >/dev/null
        grep -F 'srl t0, t0, t1' "$core" >/dev/null
        grep -F 'sra t0, t0, t1' "$core" >/dev/null
        ;;
    core-logical-and-value)
        for symbol in core_m19_plain core_m19_short_false core_m19_short_true core_m19_get_rhs_calls core_m19_nested core_m19_cfg_statement_rhs core_m19_cfg_initializer core_m19_equality_cfg_rhs core_m19_list_empty_careful_shape; do
            grep -q "^${symbol}:" "$core"
        done
        ;;
    core-nested-if-continuation)
        grep -q '^core_nested_then:' "$core"
        grep -q '^core_nested_both:' "$core"
        ;;
    core-integer-add-overflow)
        grep -q '^core_m8_checked_int:' "$core"
        grep -q '^core_m8_checked_long:' "$core"
        grep -q '^core_m8_checked_uint:' "$core"
        grep -q '^core_m8_checked_ulong:' "$core"
        grep -q '^core_m8_size_add:' "$core"
        grep -q 'sltu t4, t2, t0' "$core"
        grep -q 'srli t4, t4, 63' "$core"
        ;;
    core-integer-multiply-overflow)
        grep -q '^core_m6_checked_int:' "$core"
        grep -q '^core_m6_checked_long:' "$core"
        grep -q '^core_m6_checked_ulong:' "$core"
        grep -q '^core_m6_size_mul:' "$core"
        grep -q 'mulhu t4, t0, t1' "$core"
        grep -q 'mulh t4, t0, t1' "$core"
        ;;
    core-integer-subtract-overflow)
        grep -q 'sltu t4, t0, t1' "$core"
        grep -q 'sub t2, t0, t1' "$core"
        ;;
    core-pointer-equality-qualifiers)
        for symbol in core_m16_list_is_first core_m16_list_is_last core_m16_qualified_equal core_m16_qualified_not_equal core_m16_explicit_qualified_member_cast core_m16_null_equal core_m16_void_equal; do
            grep -q "^${symbol}:" "$core"
        done
        ;;
    core-pointer-offset)
        for symbol in core_m15_void_add core_m15_int_add core_m15_int_add_commuted core_m15_list_poison; do
            grep -q "^${symbol}:" "$core"
        done
        ;;
    core-scalar-not-equal)
        for symbol in core_m13_integer_not_equal core_m13_pointer_not_equal core_m13_member_pointer_not_equal core_m13_list_condition; do
            grep -q "^${symbol}:" "$core"
        done
        ;;
    core-scalar-assignment-implicit-void)
        for symbol in core_m4_init_list_head core_m4_pointer_store core_m4_empty_void; do
            grep -q "^${symbol}:" "$core"
            grep -q "${symbol}_core_bb0" "$core"
        done
        ;;
    core-scalar-lvalue-bitcast)
        for symbol in core_offset_to_ptr core_pointer_read core_member_read core_pointer_bits; do
            grep -q "^${symbol}:" "$core"
            grep -q "${symbol}_core_bb0" "$core"
        done
        ;;
    core-opaque-inline-asm)
        for symbol in core_m18_fence_only core_m18_release_shape core_m18_two_fences; do
            grep -q "^${symbol}:" "$core"
        done
        test "$(grep -c 'fence rw,w' "$core")" -ge 3
        grep -F 'fence r,rw' "$core" >/dev/null
        ;;
    core-statement-expression)
        for symbol in core_m17_scalar_value core_m17_read_once_shape core_m17_list_empty core_m17_prefix_store core_m17_prefix_call; do
            grep -q "^${symbol}:" "$core"
        done
        ;;
    core-target-constant-fallback)
        grep -q '^core_m10_pointer_size:' "$core"
        grep -q '^core_m10_init_list:' "$core"
        ;;
esac

if [ "$name" = core-pointer-offset ]; then
    "$RISCV_CC" -static -O2 -std=gnu11 "$source_file" "$runtime_file" -o "$work/reference-rv64"
else
    "$RISCV_CC" -static -O2 "$source_file" "$runtime_file" -o "$work/reference-rv64"
fi
"$RISCV_CC" -static -O2 "$runtime_file" "$core" -o "$work/minic-rv64"
"$QEMU_RISCV64" "$work/reference-rv64" >"$work/reference.out"
"$QEMU_RISCV64" "$work/minic-rv64" >"$work/minic.out"
case "$name" in
    core-integer-or-shifts-m22|core-pointer-condition-m21) diff -u "$work/reference.out" "$work/minic.out" ;;
    *) cmp "$work/reference.out" "$work/minic.out" ;;
esac
case "$name" in
    core-for-postincrement-m20)
        grep -F 'post=1,42' "$work/minic.out" >/dev/null
        grep -F 'assign=0,3' "$work/minic.out" >/dev/null
        grep -F 'count=0,3' "$work/minic.out" >/dev/null
        ;;
    core-integer-or-shifts-m22)
        grep -Fx 'or=4660' "$work/minic.out" >/dev/null
        grep -Fx 'shl=4608' "$work/minic.out" >/dev/null
        grep -Fx 'shru=4660' "$work/minic.out" >/dev/null
        grep -Fx 'shrs=-1' "$work/minic.out" >/dev/null
        grep -Fx 'swab=13330,65280,165' "$work/minic.out" >/dev/null
        ;;
    core-logical-and-value)
        grep -F 'plain=0,0,1 nested=1,0' "$work/minic.out" >/dev/null
        grep -F 'short=0/0,1/1' "$work/minic.out" >/dev/null
        grep -F 'cfg=0,1,1' "$work/minic.out" >/dev/null
        grep -F 'init=1,7' "$work/minic.out" >/dev/null
        grep -F 'list=1,0' "$work/minic.out" >/dev/null
        ;;
    core-pointer-condition-m21)
        grep -Fx 'if=7,3' "$work/minic.out" >/dev/null
        grep -Fx 'not=5,11' "$work/minic.out" >/dev/null
        grep -Fx 'hlist=1,1,1' "$work/minic.out" >/dev/null
        ;;
esac
printf 'PASS compiler/c0/%s\n' "$name"
