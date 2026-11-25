#!/bin/sh
set -eu
RUNTIME_SUPPORT_SOURCE=

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
riscv_cc=${RISCV_CC:-riscv64-buildroot-linux-musl-gcc}
qemu=${QEMU_RISCV64:-qemu-riscv64}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-runtime

if ! command -v "$riscv_cc" >/dev/null 2>&1 || ! command -v "$qemu" >/dev/null 2>&1; then
    printf '%s\n' "SKIP compiler/c0/runtime: set RISCV_CC and QEMU_RISCV64 to available tools"
    test "${REQUIRE_RISCV_RUNTIME:-0}" != 1
    exit
fi

mkdir -p "$work"
"$riscv_cc" --version | sed -n '1p'
"$qemu" --version | sed -n '1p'

run_case() {
    name=$1
    expected=$2
    source_name=$3
    shift 3

    "$riscv_cc" -E -P -x c "$@" \
        "$root/tests/compiler/c0/$source_name.c" -o "$work/$name.i"
    "$minic" -S "$work/$name.i" -o "$work/$name.s"
    if [ -n "$RUNTIME_SUPPORT_SOURCE" ]; then
        "$riscv_cc" -static "$work/$name.s" \
            "$root/tests/compiler/c0/$RUNTIME_SUPPORT_SOURCE.c" -o "$work/$name.elf"
    else
        "$riscv_cc" -static "$work/$name.s" -o "$work/$name.elf"
    fi

    set +e
    "$qemu" "$work/$name.elf"
    status=$?
    set -e

    if test "$status" -ne "$expected"; then
        printf '%s\n' "FAIL compiler/c0/runtime/$name: expected=$expected actual=$status" >&2
        exit 1
    fi
    printf '%s\n' "PASS compiler/c0/runtime/$name exit=$status"
}

run_double_return_abi() {
    name=double_return_abi

    "$riscv_cc" -E -P -x c \
        "$root/tests/compiler/c0/$name.c" -o "$work/$name.i"
    "$minic" -S "$work/$name.i" -o "$work/$name.s"
    grep -F "  fmv.x.d a0, fa0" "$work/$name.s" >/dev/null
    grep -F "  fmv.d.x fa0, a0" "$work/$name.s" >/dev/null
    grep -F "0x405ee00000000000" "$work/$name.s" >/dev/null
    grep -F "  fadd.d ft0, ft0, ft1" "$work/$name.s" >/dev/null
    grep -F "  fsub.d ft0, ft0, ft1" "$work/$name.s" >/dev/null
    grep -F "  fmul.d ft0, ft0, ft1" "$work/$name.s" >/dev/null
    grep -F "  fdiv.d ft0, ft0, ft1" "$work/$name.s" >/dev/null
    "$riscv_cc" -static \
        "$work/$name.s" \
        "$root/tests/compiler/c0/${name}_helper.c" \
        -o "$work/$name.elf"

    set +e
    "$qemu" "$work/$name.elf"
    status=$?
    set -e

    if test "$status" -ne 0; then
        printf '%s\n' "FAIL compiler/c0/runtime/$name: expected=0 actual=$status" >&2
        exit 1
    fi
    printf '%s\n' \
        "PASS compiler/c0/runtime/$name exit=$status abi=rv64d-fa0 literal=binary64 arithmetic=add-sub-mul-div"
}

run_case empty_main 0 empty_main
run_case return_0 0 return_0
run_case return_42 42 return_42
run_case arithmetic_precedence 7 arithmetic -DCASE=1
run_case arithmetic_parentheses 10 arithmetic -DCASE=2
run_case arithmetic_divrem 8 arithmetic -DCASE=3
run_case arithmetic_unary 9 arithmetic -DCASE=4
run_case local_init 7 locals -DCASE=1
run_case local_assign 11 locals -DCASE=2
run_case local_reassign 15 locals -DCASE=3
run_case comparison_equal 1 comparisons -DCASE=1
run_case comparison_not_equal 0 comparisons -DCASE=2
run_case comparison_less 1 comparisons -DCASE=3
run_case comparison_less_equal 1 comparisons -DCASE=4
run_case comparison_greater 1 comparisons -DCASE=5
run_case comparison_greater_equal 0 comparisons -DCASE=6
run_case comparison_precedence 1 comparisons -DCASE=7
run_case comparison_local 1 comparisons -DCASE=8
run_case logical_not_zero 1 logical_not -DCASE=1
run_case logical_not_nonzero 0 logical_not -DCASE=2
run_case logical_not_recursive 1 logical_not -DCASE=3
run_case logical_not_comparison 0 logical_not -DCASE=4
run_case logical_not_local 1 logical_not -DCASE=5
run_case if_true_assignment 7 if_else -DCASE=1
run_case if_false_else 9 if_else -DCASE=2
run_case if_comparison_block 7 if_else -DCASE=3
run_case if_nested_dangling_else 8 if_else -DCASE=4
run_case if_branch_return 6 if_else -DCASE=5
run_case if_false_fallthrough 2 if_else -DCASE=6
run_case if_multi_statement 9 if_else -DCASE=7
run_case array_declaration 0 array_declaration
run_case gnu_array_range_runtime 0 gnu_array_range_runtime
run_case identity_record_typedef_cast 0 identity_record_typedef_cast
run_case record_field_nonstring_attribute 0 record_field_nonstring_attribute
run_case static_local_pointer_array_decay 0 static_local_pointer_array_decay
run_case inferred_static_unsigned_char_list 0 inferred_static_unsigned_char_list
run_case static_union_active_member_relocation 0 static_union_active_member_relocation
run_case runtime_record_array_value_initializer 0 runtime_record_array_value_initializer
run_case static_zero_length_record_field_relocation 0 static_zero_length_record_field_relocation
run_case gnu_cleanup_runtime 0 gnu_cleanup_runtime
run_case gnu_void_pointer_relational 0 gnu_void_pointer_relational
run_case core_scalar_comma_qualified_runtime 0 core_scalar_comma_qualified_runtime
run_case long_double_declaration_runtime 0 long_double_declaration_runtime
run_case gnu_word_mode_runtime 0 gnu_word_mode_runtime
run_case double_to_float_runtime 0 double_to_float_runtime
run_case main_parameters_runtime 0 main_parameters_runtime
run_case static_label_jump_table_runtime 0 static_label_jump_table_runtime
run_case core_double_compound_runtime 0 core_double_compound_runtime
run_case netlink_warn_guard_runtime 0 netlink_warn_guard_runtime
run_case netfilter_hook_insertion_runtime 0 netfilter_hook_insertion_runtime
run_case builtin_huge_val_runtime 0 builtin_huge_val_runtime
run_case gnu_extension_prefix_runtime 0 gnu_extension_prefix_runtime
run_double_return_abi

# Consolidated six formerly orphaned RV64 GNU/QEMU shell regressions.
# Keep independent fixtures and semantic checks; share the subprocess path.
run_rv64_gnu_diff() {
    name=$1
    check=$2
    marker=$3
    "$riscv_cc" -E -P -std=gnu11 -x c \
        "$root/tests/compiler/c0/$name.c" -o "$work/$name.i"
    "$minic" -S "$work/$name.i" -o "$work/$name.s"
    test -s "$work/$name.s"
    if [ "$check" = no_zbb_clz ] &&
       grep -Eq '(^|[[:space:]])clz([[:space:]]|$)' "$work/$name.s"; then
        echo 'unexpected Zbb clz dependency' >&2
        exit 1
    fi
    "$riscv_cc" -static "$work/$name.s" -o "$work/$name.mini.elf"
    "$riscv_cc" -static -std=gnu11 "$root/tests/compiler/c0/$name.c" \
        -o "$work/$name.gnu.elf"
    set +e
    "$qemu" "$work/$name.gnu.elf"
    gcc_status=$?
    "$qemu" "$work/$name.mini.elf"
    mini_status=$?
    set -e
    if [ "$gcc_status" -ne 0 ] || [ "$mini_status" -ne "$gcc_status" ]; then
        printf '%s\n' "FAIL compiler/c0/$name gnu=$gcc_status minic=$mini_status" >&2
        exit 1
    fi
    printf '%s\n' "$marker"
}

# Raw MiniC .c inputs are intentionally not replaced with GNU -E .i inputs.
run_rv64_raw() {
    name=$1
    support=$2
    "$minic" -S "$root/tests/compiler/c0/$name.c" -o "$work/$name.s"
    test -s "$work/$name.s"
    if [ -n "$support" ]; then
        "$riscv_cc" -O0 -static "$work/$name.s" \
            "$root/tests/compiler/c0/$support.c" -o "$work/$name.elf"
    else
        "$riscv_cc" -O0 -static "$work/$name.s" -o "$work/$name.elf"
    fi
    set +e
    "$qemu" "$work/$name.elf"
    status=$?
    set -e
    if [ "$status" -ne 0 ]; then
        printf '%s\n' "FAIL compiler/c0/$name qemu=$status" >&2
        exit 1
    fi
}

# +&r earlyclobber stays a separate freestanding GNU ABI oracle with _start.
run_rv64_earlyclobber() {
    name=gnu_inline_asm_readwrite_earlyclobber
    host_cc=cc
    "$host_cc" -E -P -x c "$root/tests/compiler/c0/$name.c" \
        -o "$work/$name.i"
    "$minic" -S "$work/$name.i" -o "$work/$name.s"
    grep -F 'add a0, a0,' "$work/$name.s" >/dev/null
    grep -F 'add a1, a1,' "$work/$name.s" >/dev/null
    cat >"$work/$name.crt0.S" <<'START'
.text
.globl _start
_start:
    call main
    li a7, 93
    ecall
START
    "$riscv_cc" -ffreestanding -fno-stack-protector -fno-pie \
        -c -std=gnu11 "$root/tests/compiler/c0/$name.c" -o "$work/$name.gnu.o"
    "$riscv_cc" -nostdlib -static -no-pie "$work/$name.crt0.S" \
        "$work/$name.s" -o "$work/$name.mini.elf"
    "$riscv_cc" -nostdlib -static -no-pie "$work/$name.crt0.S" \
        "$work/$name.gnu.o" -o "$work/$name.gnu.elf"
    set +e
    "$qemu" "$work/$name.gnu.elf"
    gcc_status=$?
    "$qemu" "$work/$name.mini.elf"
    mini_status=$?
    set -e
    if [ "$gcc_status" -ne 0 ] || [ "$mini_status" -ne "$gcc_status" ]; then
        printf '%s\n' "FAIL compiler/c0/$name gnu=$gcc_status minic=$mini_status" >&2
        exit 1
    fi
    printf '%s\n' 'PASS compiler/c0/gnu_inline_asm_readwrite_earlyclobber_rv64 constraint=+&r fixed=a0,a1 unmatched_nonoverlap=1 qemu=1'
}

run_rv64_gnu_diff gnu_inline_asm_operands normal \
    'PASS compiler/c0/gnu_inline_asm_operands_rv64 gcc=minic amoadd=write+fetch constraints=+A,=r,r qemu=1'
run_rv64_gnu_diff gnu_inline_asm_named_operands normal \
    'PASS compiler/c0/gnu_inline_asm_named_operands_rv64 gcc=minic lrsc=fetch-add-unless names=5 early-clobber=1 qemu=1'
run_rv64_gnu_diff builtin_clzll no_zbb_clz \
    'PASS compiler/c0/builtin_clzll_rv64 gcc=minic ull=4 uint32=1 signed-int=1 baseline=rv64i qemu=1'
run_rv64_earlyclobber
RUNTIME_SUPPORT_SOURCE=stack_fixed_arguments_gcc \
    run_case stack_fixed_arguments 0 stack_fixed_arguments
printf '%s\n' 'PASS compiler/c0/stack_fixed_arguments_rv64 callers=minic,gcc callees=gcc,minic fixed=9 stack-slot=1'
run_rv64_raw dynamic_stack_alloca dynamic_stack_alloca_helper
run_rv64_raw dynamic_local_vla ''
printf '%s\n' 'PASS compiler/c0/dynamic_stack_alloca nested=2 fixed-frame=1 outgoing-stack-args=2 vla=runtime-bound-once,int-elements runtime=qemu'
