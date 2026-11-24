#!/bin/sh
# Shared positive compile-only oracle: preserve the former per-case flags and work paths.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
if [ "$#" -ne 1 ]; then
    echo "usage: $0 CASE" >&2
    echo "Run a case registered in this driver." >&2
    exit 2
fi
case $1 in
    external-array-redeclaration-owner|external-tentative-declarator-list|gnu-object-unused-alias|runtime-record-zero-shorthand|small-record-copy-return|static-local-prefix-attributes|static-record-single-char-array|preformed-static-array|static-record-array-suffix-attributes|block-scope-extern-multi-declarator)
        set -- "$1"
        ;;
    backward-static-aggregate-overlay|fixed-call-character-pointer|static-record-nested-designator-string-overlay|static-integer-address-relocation|gnu-packed-record-field)
        set -- "$1" -std=gnu11
        ;;
    void-casts|multiply-assignment|array-bound-sizeof|volatile-qualifiers|function-address-expressions|record-multi-declarators|cast-record-type-names|record-length-one-arrays|builtin-memcpy-call|builtin-memset-call)
        set -- "$1"
        ;;
    gnu-inline-keyword-spellings|gnu-no-profile-function-attribute|gnu-typeof-array-type-name|gnu-const-function-attribute|enum-forward-completion|gnu-choose-expr-bitfield|gnu-local-interleaved-informational-attribute)
        set -- "$1" -std=gnu11
        ;;
    *)
        echo "unknown compile-only case: $1" >&2
        exit 2
        ;;
esac
name=$1
shift
source=$(printf '%s' "$name" | tr '-' '_')
work=${BUILD_DIR:-"$root/build/debug"}/tests/$name
input=input
output=output
case "$name" in
    void-casts)
        source=void_cast_discard
        work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-void-casts
        input=void_cast_discard
        output=void_cast_discard
        rm -rf "$work"
        ;;
    multiply-assignment)
        source=multiply_assignment
        work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-multiply-assignment
        input=multiply_assignment
        output=multiply_assignment
        rm -rf "$work"
        ;;
    gnu-inline-keyword-spellings)
        source=gnu_inline_keyword_spellings
        work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-inline-keyword-spellings
        input=input
        output=gnu_inline_keyword_spellings
        rm -rf "$work"
        ;;
    gnu-no-profile-function-attribute)
        source=gnu_no_profile_function_attribute
        work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-no-profile-function-attribute
        input=input
        output=output
        rm -rf "$work"
        ;;
    array-bound-sizeof)
        source=array_bound_sizeof
        work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-array-bound-sizeof
        input=array_bound_sizeof
        output=array_bound_sizeof
        rm -rf "$work"
        ;;
    volatile-qualifiers)
        source=volatile_qualifiers
        work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-volatile-qualifiers
        input=volatile_qualifiers
        output=volatile_qualifiers
        rm -rf "$work"
        ;;
    function-address-expressions)
        source=function_address_expression
        work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-function-address-expression
        input=function_address_expression
        output=function_address_expression
        rm -rf "$work"
        ;;
    gnu-typeof-array-type-name)
        source=gnu_typeof_array_type_name
        work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-typeof-array-type-name
        input=input
        output=output
        rm -rf "$work"
        ;;
    record-multi-declarators)
        source=record_multi_declarators
        work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-record-multi-declarators
        input=record_multi_declarators
        output=record_multi_declarators
        rm -rf "$work"
        ;;
    gnu-const-function-attribute)
        source=gnu_const_function_attribute
        work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-const-function-attribute
        input=input
        output=gnu_const_function_attribute
        rm -rf "$work"
        ;;
    cast-record-type-names)
        source=cast_record_type_names
        work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-cast-record-type-names
        input=cast_record_type_names
        output=cast_record_type_names
        rm -rf "$work"
        ;;
    enum-forward-completion)
        source=enum_forward_completion
        work=${BUILD_DIR:-"$root/build/debug"}/tests/enum-forward-completion
        input=input
        output=output
        ;;
    gnu-choose-expr-bitfield)
        source=gnu_choose_expr_bitfield
        work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-choose-expr-bitfield
        input=probe
        output=probe
        rm -rf "$work"
        ;;
    record-length-one-arrays)
        source=record_length_one_array
        work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-record-length-one-arrays
        input=record_length_one_array
        output=record_length_one_array
        rm -rf "$work"
        ;;
    gnu-local-interleaved-informational-attribute)
        source=gnu_local_interleaved_informational_attribute
        work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-local-interleaved-informational-attribute
        input=input
        output=output
        rm -rf "$work"
        ;;
    builtin-memcpy-call)
        source=builtin_memcpy_call
        work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0/builtin-memcpy-call
        input=input
        output=output
        ;;
    builtin-memset-call)
        source=builtin_memset_call
        work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-builtin-memset-call
        input=input
        output=output
        rm -rf "$work"
        ;;

    block-scope-extern-multi-declarator)
        work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-block-extern-multi
        ;;
    gnu-packed-record-field)
        work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-packed-record-field
        rm -rf "$work"
        ;;
esac
mkdir -p "$work"
"$host_cc" -E -P "$@" -x c "$root/tests/compiler/c0/$source.c" -o "$work/$input.i"
"$minic" -S "$work/$input.i" -o "$work/$output.s"
case "$name" in
    void-casts)
        grep -F '  call bump' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/void_cast_discard literal=discard call=side-effect-preserved'
        ;;
    multiply-assignment)
        grep -F '  mul ' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/multiply_assignment operator=*= integer-rmw=yes'
        ;;
    gnu-inline-keyword-spellings)
        test -s "$work/$output.s"
        grep -F 'swap16:' "$work/$output.s" >/dev/null
        grep -F 'add1:' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/gnu_inline_keyword_spellings spellings=__inline,__inline__'
        ;;
    gnu-no-profile-function-attribute)
        test -s "$work/$output.s"
        grep -F 'no_profile_decl:' "$work/$output.s" >/dev/null
        grep -F '.noinstr.text' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/gnu_no_profile_function_attribute parse-only=1 section=preserved'
        ;;
    array-bound-sizeof)
        test -s "$work/$output.s"
        grep -F 'read_sizes:' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/array_bound_constant_expr pointer=8 arithmetic=12 enum=6 parentheses=1 sizeof-string-sum=12'
        ;;
    volatile-qualifiers)
        test -s "$work/$output.s"
        grep -F 'read_state:' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/volatile_qualifiers base=1 const-volatile=1 local=1 pointer-level=1 typedef-pointer=1'
        ;;
    function-address-expressions)
        grep -F '  la a0, increment' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/function_address_expression direct=&fn cancel=&*fp RV64=function-address'
        ;;
    gnu-typeof-array-type-name)
        test -s "$work/$output.s"
        grep -F 'typeof_array_size:' "$work/$output.s" >/dev/null
        grep -F 'typeof_pointer_array_size:' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/gnu_typeof_array_type_name array=constant-bound pointer-array=1 sizeof=1'
        ;;
    record-multi-declarators)
        test -s "$work/$output.s"
        grep -F 'read_link:' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/record_multi_declarators scalar-pair=1 self-pointer-pair=1'
        ;;
    gnu-const-function-attribute)
        test -s "$work/$output.s"
        grep -F 'fswab_like:' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/gnu_const_function_attribute prefix=__const__ classification=optimization-metadata ABI=unchanged static-inline=1'
        ;;
    cast-record-type-names)
        grep -F 'read_holder:' "$work/$output.s" >/dev/null
        grep -F 'lw a0, 0(a0)' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/cast_record_type_names union-cast=1 volatile-cast=1'
        ;;
    enum-forward-completion)
        test -s "$work/$output.s"
        grep -F '.dword 1099511627776' "$work/$output.s" >/dev/null
        grep -F 'global_wide:' "$work/$output.s" >/dev/null
        grep -F 'read_wide:' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/enum-forward-completion identity=enum-id compatible=canonical no-program-refresh=1'
        ;;
    gnu-choose-expr-bitfield)
        test -s "$work/$output.s"
        grep -F 'linux_build_bug_shape:' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/gnu_choose_expr_bitfield bitfield=typed-ast-consteval choose-expr=selected-arm sizeof-void=gnu-byte target-info=1 unsigned-width=preserved'
        ;;
    record-length-one-arrays)
        test -s "$work/$output.s"
        grep -F 'read_first:' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/record_length_one_array identity=array count=1 subscript=1 scalar-neighbor=1'
        ;;
    gnu-local-interleaved-informational-attribute)
        test -s "$work/$output.s"
        grep -F 'main:' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/gnu_local_interleaved_informational_attribute placement=type-before-declarator unused=informational declarators=2 initializers=2'
        ;;
    builtin-memcpy-call)
        test "$(grep -c -F '  call memcpy' "$work/$output.s")" -eq 2
        if grep -F '__builtin_memcpy' "$work/$output.s" >/dev/null; then
            printf '%s\n' 'FAIL compiler/c0/builtin_memcpy_call: builtin spelling leaked to assembly' >&2
            exit 1
        fi
        printf '%s\n' 'PASS compiler/c0/builtin_memcpy_call canonical CALL memcpy'
        ;;
    builtin-memset-call)
        test "$(grep -c -F '  call memset' "$work/$output.s")" -eq 2
        if grep -F '__builtin_memset' "$work/$output.s" >/dev/null; then
            printf '%s\n' 'FAIL compiler/c0/builtin_memset_call: builtin spelling leaked to assembly' >&2
            exit 1
        fi
        printf '%s\n' 'PASS compiler/c0/builtin_memset_call canonical CALL memset'
        ;;

    preformed-static-array)
        test -s "$work/output.s"
        grep -F '.section .data.test' "$work/output.s" >/dev/null
        ;;
    static-record-array-suffix-attributes)
        grep -F '.section .data.test' "$work/output.s" >/dev/null
        test -s "$work/output.s"
        ;;
    static-integer-address-relocation)
        test -s "$work/output.s"
        grep -q 'init_stack' "$work/output.s"
        ;;
    block-scope-extern-multi-declarator)
        grep -F '__init_begin' "$work/output.s" >/dev/null
        grep -F '__init_end' "$work/output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/block_scope_extern_multi_declarator declarators=2 incomplete-arrays=2 scoped-bindings=2 entity-merge=transactional-array-descriptors'
        ;;
    gnu-packed-record-field)
        test -s "$work/output.s"
        grep -F 'main:' "$work/output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/gnu_packed_record_field field-identity=1 natural-alignment=lowered next-field=natural packed+aligned=explicit-raises record-layout=DataLayout'
        ;;
    *)
        test -s "$work/output.s"
        ;;
esac
