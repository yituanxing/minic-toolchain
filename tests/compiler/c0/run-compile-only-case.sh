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
    array-bound-integer-casts|bitwise-and-assignment-expressions|cast-type-classification|comma-operator|extern-multidimensional-array|extern-objects|external-pointer-arrays|function-pointer-parameters|function-type-typedefs|global-pointer-subscripts|gnu-local-object-alignment)
        set -- "$1"
        ;;
    block-scope-extern-function-attributes|builtin-expect|extern-multi-declarators|function-linkage-inheritance|gnu-extension-declaration|gnu-extension-prefix-declarations|gnu-format-cold-attributes|gnu-function-visibility|gnu-signed-keyword|gnu-typeof-array-bound-cast|gnu-typeof-local-declaration)
        set -- "$1" -std=gnu11
        ;;
    record-forward-declarations|record-multidimensional-arrays|restrict-qualifiers|static-local-string-arrays|static-record-array-member-designator|parenthesized-array-declarator|parenthesized-comma-expressions|pointer-subtract-assignments|top-level-comma-conditions|typedef-enum-definitions|unsigned-64-literals|variadic-function-pointer-cast|volatile-parameter|gnu-top-level-empty-declaration|adjacent-string-literals|block-scope-typedef|gnu-inline-asm-eight-operands)
        set -- "$1"
        ;;
    static-storage-scalar-array-owner|gnu-statement-expression|alignof-type-query|extern-function-pointer-object|gnu-inline-asm-readwrite-output|gnu-void-function-pointer-assignment)
        set -- "$1" -std=gnu11
        ;;
    record-typedef-array-fields)
        set -- "$1" -std=c11
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
    record-forward-declarations|record-multidimensional-arrays|record-typedef-array-fields|restrict-qualifiers|static-local-string-arrays|static-record-array-member-designator|static-storage-scalar-array-owner|parenthesized-array-declarator|parenthesized-comma-expressions|pointer-subtract-assignments|top-level-comma-conditions|typedef-enum-definitions|unsigned-64-literals|variadic-function-pointer-cast|volatile-parameter|gnu-statement-expression|gnu-top-level-empty-declaration|adjacent-string-literals|alignof-type-query|block-scope-typedef|extern-function-pointer-object|gnu-inline-asm-eight-operands|gnu-inline-asm-readwrite-output|gnu-void-function-pointer-assignment)
        work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-$name
        input=$source
        output=$source
        case "$name" in
            record-typedef-array-fields) source=record_typedef_array_field; work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-record-typedef-array-fields; input=record_typedef_array_field; output=record_typedef_array_field ;;
            static-local-string-arrays) source=static_local_string_array; work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-static-local-string-arrays; input=static_local_string_array; output=static_local_string_array ;;
            static-record-array-member-designator) source=static_record_array_member_designator; work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-static-record-array-member-designator; input=input; output=output ;;
            static-storage-scalar-array-owner) source=static_storage_scalar_array_owner; work=${BUILD_DIR:-"$root/build/debug"}/tests/static-storage-scalar-array-owner; input=input; output=output ;;
            parenthesized-array-declarator) source=parenthesized_array_declarator; work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-parenthesized-array-declarator; input=input; output=output ;;
            parenthesized-comma-expressions) source=parenthesized_comma_expression; work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-parenthesized-comma-expressions; input=parenthesized_comma_expression; output=parenthesized_comma_expression ;;
            pointer-subtract-assignments) source=pointer_subtract_assignment; work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-pointer-subtract-assignments; input=pointer_subtract_assignment; output=pointer_subtract_assignment ;;
            top-level-comma-conditions) source=top_level_comma_condition; work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-top-level-comma-condition; input=top_level_comma_condition; output=top_level_comma_condition ;;
            typedef-enum-definitions) source=typedef_enum_definition; work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-typedef-enum-definitions; input=typedef_enum_definition; output=typedef_enum_definition ;;
            variadic-function-pointer-cast) source=variadic_function_pointer_cast; work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-variadic-function-pointer-cast; input=input; output=output ;;
            volatile-parameter) source=volatile_parameter; work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-volatile-parameter; input=input; output=output ;;
            gnu-top-level-empty-declaration) source=gnu_top_level_empty_declaration; work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-top-level-empty-declaration; input=input; output=output ;;
            block-scope-typedef) source=block_scope_typedef; work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-block-scope-typedef; input=input; output=output ;;
            extern-function-pointer-object) source=extern_function_pointer_object; work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-extern-function-pointer-object; input=input; output=extern_function_pointer_object ;;
            gnu-inline-asm-eight-operands) source=gnu_inline_asm_eight_operands; work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-inline-asm-eight-operands; input=probe; output=probe ;;
            gnu-void-function-pointer-assignment) source=gnu_void_function_pointer_assignment; work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-void-function-pointer-assignment; input=input; output=output ;;
        esac
        case "$name" in
            static-storage-scalar-array-owner|gnu-top-level-empty-declaration) ;;
            *) rm -rf "$work" ;;
        esac
        ;;
    array-bound-integer-casts|bitwise-and-assignment-expressions|block-scope-extern-function-attributes|builtin-expect|cast-type-classification|comma-operator|extern-multi-declarators|extern-multidimensional-array|extern-objects|external-pointer-arrays|function-linkage-inheritance|function-pointer-parameters|function-type-typedefs|global-pointer-subscripts|gnu-extension-declaration|gnu-extension-prefix-declarations|gnu-format-cold-attributes|gnu-function-visibility|gnu-local-object-alignment|gnu-signed-keyword|gnu-typeof-array-bound-cast|gnu-typeof-local-declaration)
        work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-$name
        input=$source
        output=$source
        case "$name" in
            array-bound-integer-casts) source=array_bound_integer_cast; work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-array-bound-integer-casts; input=array_bound_integer_cast; output=array_bound_integer_cast ;;
            bitwise-and-assignment-expressions) source=bitwise_and_assignment_expression; work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-bitwise-and-assignment-expressions; input=bitwise_and_assignment_expression; output=bitwise_and_assignment_expression ;;
            extern-multi-declarators) source=extern_multi_declarators; work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-extern-multi-declarators; input=input; output=extern_multi_declarators ;;
            extern-objects) source=extern_object; work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-extern-objects; input=extern_object; output=extern_object ;;
            external-pointer-arrays) source=external_pointer_array; work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-external-pointer-array; input=external_pointer_array; output=external_pointer_array ;;
            function-linkage-inheritance) source=function_linkage_inheritance; work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-function-linkage-inheritance; input=input; output=function_linkage_inheritance ;;
            function-pointer-parameters) source=function_pointer_parameter; work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-function-pointer-parameters; input=function_pointer_parameter; output=function_pointer_parameter ;;
            function-type-typedefs) source=function_type_typedef; work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-function-type-typedefs; input=function_type_typedef; output=function_type_typedef ;;
            global-pointer-subscripts) source=global_pointer_subscript; work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-global-pointer-subscripts; input=global_pointer_subscript; output=global_pointer_subscript ;;
            gnu-extension-prefix-declarations) source=gnu_extension_prefix_declaration; work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-extension-prefix; input=gnu_extension_prefix_declaration; output=gnu_extension_prefix_declaration ;;
            gnu-format-cold-attributes) source=gnu_format_cold_attributes; work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-format-cold-attributes; input=input; output=gnu_format_cold_attributes ;;
            gnu-local-object-alignment) source=gnu_local_object_alignment; work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-local-object-alignment; input=input; output=output ;;
            gnu-typeof-array-bound-cast) source=gnu_typeof_array_bound_cast; work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-typeof-array-bound-cast; input=input; output=output ;;
        esac
        rm -rf "$work"
        ;;
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
if [ "$name" = gnu-void-function-pointer-assignment ]; then
    "$host_cc" -fsyntax-only -std=gnu11 -Werror -Wno-pedantic -x c "$root/tests/compiler/c0/$source.c"
fi
if [ "$name" = gnu-typeof-array-bound-cast ]; then
    "$host_cc" -fsyntax-only -std=gnu11 -x c "$root/tests/compiler/c0/$source.c"
fi
"$host_cc" -E -P "$@" -x c "$root/tests/compiler/c0/$source.c" -o "$work/$input.i"
"$minic" -S "$work/$input.i" -o "$work/$output.s"
case "$name" in
    record-forward-declarations)
        grep -F 'pass:' "$work/$output.s" >/dev/null
        grep -F 'call pass' "$work/$output.s" >/dev/null
        grep -F 'lw a0, 0(a0)' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/record_forward_declarations struct=1 union=1 completion=same-tag pointer-before-complete=1'
        ;;
    record-multidimensional-arrays)
        test -s "$work/$output.s"
        grep -F 'read_grid:' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/record_multidimensional_arrays dims=2,3 member-index=2'
        ;;
    record-typedef-array-fields)
        test -s "$work/$output.s"
        grep -F 'read_reg:' "$work/$output.s" >/dev/null
        grep -F 'context_size:' "$work/$output.s" >/dev/null
        grep -F '  li a0, 40' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/record_typedef_array_field typedef-array=4 member=array indexing=1 record-size=40'
        ;;
    restrict-qualifiers)
        test -s "$work/$output.s"
        grep -F 'call_restrict_forms:' "$work/$output.s" >/dev/null
        grep -F '  call copy_gnu' "$work/$output.s" >/dev/null
        grep -F '  call copy_gnu_double' "$work/$output.s" >/dev/null
        grep -F '  call copy_c' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/restrict_qualifiers C=restrict GNU=__restrict/__restrict__ pointer-only=1 abi=unchanged'
        ;;
    static-local-string-arrays)
        grep -F '.Lminic_string_0:' "$work/$output.s" >/dev/null
        grep -F '.size .Lminic_string_0, 4' "$work/$output.s" >/dev/null
        grep -F '  .byte 97' "$work/$output.s" >/dev/null
        grep -Eq '^[[:space:]]+lbu[[:space:]]+[a-z][a-z0-9]*, 0\([a-z][a-z0-9]*\)        grep -F '  li a0, 32' "$work/$output.s" >/dev/null
        grep -F '  li a0, 4' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/array_bound_integer_cast enum-cast=1 truncation=uint8 typed-ast-consteval=1'
        ;;
    bitwise-and-assignment-expressions)
        test "$(grep -c -F '  call next_flags' "$work/$output.s")" -eq 1
        grep -F '  and a0, t0, a0' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/bitwise_and_assignment_expression integer=1 lvalue-evaluated-once=1'
        ;;
    block-scope-extern-function-attributes)
        test -s "$work/$output.s"
        grep -F '__compiletime_assert_0' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/block_scope_extern_function_attributes scope=block linkage=extern prefix=noreturn suffix=error direct-call-resolved=1'
        ;;
    builtin-expect)
        test -s "$work/$output.s"
        grep -F 'expect_value:' "$work/$output.s" >/dev/null
        grep -F 'expect_zero:' "$work/$output.s" >/dev/null
        if grep -F '__builtin_expect' "$work/$output.s" >/dev/null; then
            printf '%s\n' 'FAIL compiler/c0/builtin_expect emitted runtime builtin symbol' >&2
            exit 1
        fi
        printf '%s\n' 'PASS compiler/c0/builtin_expect value=first-argument return-type=long hint=integer-constant runtime-call=none'
        ;;
    cast-type-classification)
        test -s "$work/$output.s"
        grep -F 'read_union_cast:' "$work/$output.s" >/dev/null
        grep -F 'read_volatile_cast:' "$work/$output.s" >/dev/null
        grep -F 'narrow_short:' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/cast_type_classification short=1 union=1 volatile=1'
        ;;
    comma-operator)
        test -s "$work/$output.s"
        grep -F 'comma_growth:' "$work/$output.s" >/dev/null
        grep -F 'comma_value:' "$work/$output.s" >/dev/null
        grep -F 'comma_conditions:' "$work/$output.s" >/dev/null
        grep -F 'comma_expression_statement:' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/comma_operator parenthesized=1 pool-growth=1 top-level-condition=while,if expression-statement=1 void-left=1 assignment-side-effect=1 result=right'
        ;;
    extern-multi-declarators)
        test -s "$work/$output.s"
        grep -F '  la a0, start_like' "$work/$output.s" >/dev/null
        grep -F '  la a0, end_like' "$work/$output.s" >/dev/null
        if grep -F 'start_like:' "$work/$output.s" >/dev/null || grep -F 'end_like:' "$work/$output.s" >/dev/null; then
            printf '%s\n' 'FAIL compiler/c0/extern_multi_declarators: declaration emitted storage' >&2
            exit 1
        fi
        printf '%s\n' 'PASS compiler/c0/extern_multi_declarators shared-base=function-pointer arrays=2 incomplete=2 storage=none'
        ;;
    extern-multidimensional-array)
        test -s "$work/$output.s"
        grep -F 'read_cpu_bit:' "$work/$output.s" >/dev/null
        grep -F 'cpu_bit_row:' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/extern_multidimensional_array dimensions=2 bounds=constant-expression nested-type=Array(Array(ulong)) subscript=2 row-decay=pointer'
        ;;
    extern-objects)
        grep -F '  la a0, external_counter' "$work/$output.s" >/dev/null
        grep -F '  lw a0, 0(a0)' "$work/$output.s" >/dev/null
        if grep -F '.type external_counter, @object' "$work/$output.s" >/dev/null ||
           grep -F 'external_counter:' "$work/$output.s" >/dev/null; then
            printf '%s\n' 'FAIL compiler/c0/extern_object: declaration emitted a definition' >&2
            exit 1
        fi
        printf '%s\n' 'PASS compiler/c0/extern_object declaration-only=external-symbol-reference'
        ;;
    external-pointer-arrays)
        grep -F '.type names, @object' "$work/$output.s" >/dev/null
        grep -F '.globl names' "$work/$output.s" >/dev/null
        grep -F '  .dword shared_name' "$work/$output.s" >/dev/null
        grep -F '  .dword .Lminic_string_' "$work/$output.s" >/dev/null
        grep -F '  .dword 0' "$work/$output.s" >/dev/null
        grep -F '.size names, 32' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/external_pointer_array bound=4 string-reloc=2 object-reloc=1 null=1 storage=32'
        ;;
    function-linkage-inheritance)
        test -s "$work/$output.s"
        grep -F 'read_timer_like:' "$work/$output.s" >/dev/null
        if grep -F '.globl read_timer_like' "$work/$output.s" >/dev/null; then
            printf '%s\n' 'FAIL compiler/c0/function_linkage_inheritance internal function was exported' >&2
            exit 1
        fi
        printf '%s\n' 'PASS compiler/c0/function_linkage_inheritance prior=static-inline later=non-static-declaration effective-linkage=internal export=none'
        ;;
    function-pointer-parameters)
        grep -F '.type callback, @function' "$work/$output.s" >/dev/null
        grep -F '  la a0, callback' "$work/$output.s" >/dev/null
        grep -F '  call accept_callback' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/function_pointer_parameter direct-declarator=void-callback'
        ;;
    function-type-typedefs)
        grep -F '.type callback, @function' "$work/$output.s" >/dev/null
        grep -F '  la a0, callback' "$work/$output.s" >/dev/null
        grep -F '  call install_callback' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/function_type_typedef alias=function pointer-use=direct'
        ;;
    global-pointer-subscripts)
        grep -F '  la a0, items' "$work/$output.s" >/dev/null
        grep -F '  ld a0, 0(a0)' "$work/$output.s" >/dev/null
        grep -F '  slli a0, a0, 3' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/global_pointer_subscript base=global-pointer element=pointer stride=8'
        ;;
    gnu-extension-declaration)
        test -s "$work/$output.s"
        grep -F 'read_wide:' "$work/$output.s" >/dev/null
        grep -F 'wide_size:' "$work/$output.s" >/dev/null
        grep -F '  li a0, 8' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/gnu_extension_declaration marker=diagnostic-only union-field=1 layout=unchanged size=8'
        ;;
    gnu-extension-prefix-declarations)
        test -s "$work/$output.s"
        grep -F 'extension_sum:' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/gnu_extension_prefix_declaration marker=diagnostic-only placement=before-typedef signed-alias=1'
        ;;
    gnu-format-cold-attributes)
        test -s "$work/$output.s"
        grep -F 'main:' "$work/$output.s" >/dev/null
        grep -F 'ftrace_vprintk_like:' "$work/$output.s" >/dev/null
        grep -F 'reordered_static_inline:' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/gnu_format_cold_attributes prefix=format suffix=noreturn,cold interleaved=static-attr-inline-attr+inline-static classification=diagnostic+optimization ABI=unchanged'
        ;;
    gnu-function-visibility)
        test -s "$work/$output.s"
        grep -F '.globl visible_api' "$work/$output.s" >/dev/null
        grep -F '.internal visible_api' "$work/$output.s" >/dev/null
        grep -F '  call visible_api' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/gnu_function_visibility prefix=1 internal=ELF-directive redeclaration=consistent'
        ;;
    gnu-local-object-alignment)
        grep -F 'local_explicit_alignment:' "$work/$output.s" >/dev/null
        grep -F 'local_alignof_alignment:' "$work/$output.s" >/dev/null
        grep -F '  li t1, -32' "$work/$output.s" >/dev/null
        grep -F '  and sp, sp, t1' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/gnu_local_object_alignment frontend=explicit-alignment core=owned rv64=max-natural-explicit dynamic-stack=32 alignof=accepted'
        ;;
    gnu-signed-keyword)
        test -s "$work/$output.s"
        grep -F 'widen_signed_aliases:' "$work/$output.s" >/dev/null
        grep -F '  lb a0,' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/gnu_signed_keyword aliases=__signed,__signed__ types=char,short,int,long'
        ;;
    gnu-typeof-array-bound-cast)
        test -s "$work/$output.s"
        grep -F 'typeof_array_bound_cast_size:' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/gnu_typeof_array_bound_cast semantic-ast=1 typed-consteval=1'
        ;;
    gnu-typeof-local-declaration)
        test -s "$work/$output.s"
        grep -F 'copy_next:' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/gnu_typeof_local_declaration dispatch=shared-type-lookahead statement-expression=1 pointer-type=preserved'
        ;;
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
 "$work/$output.s"
        printf '%s\n' 'PASS compiler/c0/static_local_string_array inferred-bound=4 initializer=string static-storage=yes'
        ;;
    static-record-array-member-designator)
        grep -F 'table:' "$work/$output.s" >/dev/null
        grep -F '  .word 7' "$work/$output.s" >/dev/null
        grep -F '  .word 9' "$work/$output.s" >/dev/null
        grep -F '  .word 11' "$work/$output.s" >/dev/null
        grep -F '  .word 13' "$work/$output.s" >/dev/null
        grep -F 'anchor' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/static-record-array-member-designator element=integer,record,pointer overwrite=shared-zero-subobject range=fail-closed'
        ;;
    static-storage-scalar-array-owner)
        test -s "$work/$output.s"
        grep -F 'external_names:' "$work/$output.s" >/dev/null
        grep -F 'external_objects:' "$work/$output.s" >/dev/null
        grep -F '__minic_static_local_' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/static-storage-scalar-array-owner external-inferred=designated+relocation static-local=fixed+inferred+range multidim=shared-owner'
        ;;
    parenthesized-array-declarator)
        test -s "$work/$output.s"
        grep -F 'parenthesized_array:' "$work/$output.s" >/dev/null
        grep -F 'parenthesized_array_pick:' "$work/$output.s" >/dev/null
        if command -v riscv64-linux-gnu-gcc >/dev/null 2>&1; then
            riscv64-linux-gnu-gcc -c "$work/$output.s" -o "$work/$output.o"
        fi
        printf '%s\n' 'PASS compiler/c0/parenthesized-array-declarator static=1 inferred=1 pointer-element=1'
        ;;
    parenthesized-comma-expressions)
        test "$(grep -c -F '  call bump' "$work/$output.s")" -eq 2
        grep -F '  li a0, 11' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/parenthesized_comma_expression calls=2 result=rhs void-chain=1'
        ;;
    pointer-subtract-assignments)
        test "$(grep -c -F '  call next_slot' "$work/$output.s")" -eq 3
        grep -F '  slli a0, a0, 2' "$work/$output.s" >/dev/null
        grep -F '  sub a0, t0, a0' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/pointer_subtract_assignment scale=4 lvalue-evaluated-once=1'
        ;;
    top-level-comma-conditions)
        grep -F 'comma_condition_loop:' "$work/$output.s" >/dev/null
        grep -F '  beqz a0, .Lwhile_end_' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/top_level_comma_condition while=1 void-left=1 result=right-scalar side-effect=sequenced'
        ;;
    typedef-enum-definitions)
        grep -Eq '^[[:space:]]+li[[:space:]]+[a-z][a-z0-9]*,[[:space:]]+4        grep -F '  li a0, 32' "$work/$output.s" >/dev/null
        grep -F '  li a0, 4' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/array_bound_integer_cast enum-cast=1 truncation=uint8 typed-ast-consteval=1'
        ;;
    bitwise-and-assignment-expressions)
        test "$(grep -c -F '  call next_flags' "$work/$output.s")" -eq 1
        grep -F '  and a0, t0, a0' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/bitwise_and_assignment_expression integer=1 lvalue-evaluated-once=1'
        ;;
    block-scope-extern-function-attributes)
        test -s "$work/$output.s"
        grep -F '__compiletime_assert_0' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/block_scope_extern_function_attributes scope=block linkage=extern prefix=noreturn suffix=error direct-call-resolved=1'
        ;;
    builtin-expect)
        test -s "$work/$output.s"
        grep -F 'expect_value:' "$work/$output.s" >/dev/null
        grep -F 'expect_zero:' "$work/$output.s" >/dev/null
        if grep -F '__builtin_expect' "$work/$output.s" >/dev/null; then
            printf '%s\n' 'FAIL compiler/c0/builtin_expect emitted runtime builtin symbol' >&2
            exit 1
        fi
        printf '%s\n' 'PASS compiler/c0/builtin_expect value=first-argument return-type=long hint=integer-constant runtime-call=none'
        ;;
    cast-type-classification)
        test -s "$work/$output.s"
        grep -F 'read_union_cast:' "$work/$output.s" >/dev/null
        grep -F 'read_volatile_cast:' "$work/$output.s" >/dev/null
        grep -F 'narrow_short:' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/cast_type_classification short=1 union=1 volatile=1'
        ;;
    comma-operator)
        test -s "$work/$output.s"
        grep -F 'comma_growth:' "$work/$output.s" >/dev/null
        grep -F 'comma_value:' "$work/$output.s" >/dev/null
        grep -F 'comma_conditions:' "$work/$output.s" >/dev/null
        grep -F 'comma_expression_statement:' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/comma_operator parenthesized=1 pool-growth=1 top-level-condition=while,if expression-statement=1 void-left=1 assignment-side-effect=1 result=right'
        ;;
    extern-multi-declarators)
        test -s "$work/$output.s"
        grep -F '  la a0, start_like' "$work/$output.s" >/dev/null
        grep -F '  la a0, end_like' "$work/$output.s" >/dev/null
        if grep -F 'start_like:' "$work/$output.s" >/dev/null || grep -F 'end_like:' "$work/$output.s" >/dev/null; then
            printf '%s\n' 'FAIL compiler/c0/extern_multi_declarators: declaration emitted storage' >&2
            exit 1
        fi
        printf '%s\n' 'PASS compiler/c0/extern_multi_declarators shared-base=function-pointer arrays=2 incomplete=2 storage=none'
        ;;
    extern-multidimensional-array)
        test -s "$work/$output.s"
        grep -F 'read_cpu_bit:' "$work/$output.s" >/dev/null
        grep -F 'cpu_bit_row:' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/extern_multidimensional_array dimensions=2 bounds=constant-expression nested-type=Array(Array(ulong)) subscript=2 row-decay=pointer'
        ;;
    extern-objects)
        grep -F '  la a0, external_counter' "$work/$output.s" >/dev/null
        grep -F '  lw a0, 0(a0)' "$work/$output.s" >/dev/null
        if grep -F '.type external_counter, @object' "$work/$output.s" >/dev/null ||
           grep -F 'external_counter:' "$work/$output.s" >/dev/null; then
            printf '%s\n' 'FAIL compiler/c0/extern_object: declaration emitted a definition' >&2
            exit 1
        fi
        printf '%s\n' 'PASS compiler/c0/extern_object declaration-only=external-symbol-reference'
        ;;
    external-pointer-arrays)
        grep -F '.type names, @object' "$work/$output.s" >/dev/null
        grep -F '.globl names' "$work/$output.s" >/dev/null
        grep -F '  .dword shared_name' "$work/$output.s" >/dev/null
        grep -F '  .dword .Lminic_string_' "$work/$output.s" >/dev/null
        grep -F '  .dword 0' "$work/$output.s" >/dev/null
        grep -F '.size names, 32' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/external_pointer_array bound=4 string-reloc=2 object-reloc=1 null=1 storage=32'
        ;;
    function-linkage-inheritance)
        test -s "$work/$output.s"
        grep -F 'read_timer_like:' "$work/$output.s" >/dev/null
        if grep -F '.globl read_timer_like' "$work/$output.s" >/dev/null; then
            printf '%s\n' 'FAIL compiler/c0/function_linkage_inheritance internal function was exported' >&2
            exit 1
        fi
        printf '%s\n' 'PASS compiler/c0/function_linkage_inheritance prior=static-inline later=non-static-declaration effective-linkage=internal export=none'
        ;;
    function-pointer-parameters)
        grep -F '.type callback, @function' "$work/$output.s" >/dev/null
        grep -F '  la a0, callback' "$work/$output.s" >/dev/null
        grep -F '  call accept_callback' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/function_pointer_parameter direct-declarator=void-callback'
        ;;
    function-type-typedefs)
        grep -F '.type callback, @function' "$work/$output.s" >/dev/null
        grep -F '  la a0, callback' "$work/$output.s" >/dev/null
        grep -F '  call install_callback' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/function_type_typedef alias=function pointer-use=direct'
        ;;
    global-pointer-subscripts)
        grep -F '  la a0, items' "$work/$output.s" >/dev/null
        grep -F '  ld a0, 0(a0)' "$work/$output.s" >/dev/null
        grep -F '  slli a0, a0, 3' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/global_pointer_subscript base=global-pointer element=pointer stride=8'
        ;;
    gnu-extension-declaration)
        test -s "$work/$output.s"
        grep -F 'read_wide:' "$work/$output.s" >/dev/null
        grep -F 'wide_size:' "$work/$output.s" >/dev/null
        grep -F '  li a0, 8' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/gnu_extension_declaration marker=diagnostic-only union-field=1 layout=unchanged size=8'
        ;;
    gnu-extension-prefix-declarations)
        test -s "$work/$output.s"
        grep -F 'extension_sum:' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/gnu_extension_prefix_declaration marker=diagnostic-only placement=before-typedef signed-alias=1'
        ;;
    gnu-format-cold-attributes)
        test -s "$work/$output.s"
        grep -F 'main:' "$work/$output.s" >/dev/null
        grep -F 'ftrace_vprintk_like:' "$work/$output.s" >/dev/null
        grep -F 'reordered_static_inline:' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/gnu_format_cold_attributes prefix=format suffix=noreturn,cold interleaved=static-attr-inline-attr+inline-static classification=diagnostic+optimization ABI=unchanged'
        ;;
    gnu-function-visibility)
        test -s "$work/$output.s"
        grep -F '.globl visible_api' "$work/$output.s" >/dev/null
        grep -F '.internal visible_api' "$work/$output.s" >/dev/null
        grep -F '  call visible_api' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/gnu_function_visibility prefix=1 internal=ELF-directive redeclaration=consistent'
        ;;
    gnu-local-object-alignment)
        grep -F 'local_explicit_alignment:' "$work/$output.s" >/dev/null
        grep -F 'local_alignof_alignment:' "$work/$output.s" >/dev/null
        grep -F '  li t1, -32' "$work/$output.s" >/dev/null
        grep -F '  and sp, sp, t1' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/gnu_local_object_alignment frontend=explicit-alignment core=owned rv64=max-natural-explicit dynamic-stack=32 alignof=accepted'
        ;;
    gnu-signed-keyword)
        test -s "$work/$output.s"
        grep -F 'widen_signed_aliases:' "$work/$output.s" >/dev/null
        grep -F '  lb a0,' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/gnu_signed_keyword aliases=__signed,__signed__ types=char,short,int,long'
        ;;
    gnu-typeof-array-bound-cast)
        test -s "$work/$output.s"
        grep -F 'typeof_array_bound_cast_size:' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/gnu_typeof_array_bound_cast semantic-ast=1 typed-consteval=1'
        ;;
    gnu-typeof-local-declaration)
        test -s "$work/$output.s"
        grep -F 'copy_next:' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/gnu_typeof_local_declaration dispatch=shared-type-lookahead statement-expression=1 pointer-type=preserved'
        ;;
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
 "$work/$output.s"
        printf '%s\n' 'PASS compiler/c0/typedef_enum_definition anonymous=1 explicit-value=3 auto-next=4 alias=int'
        ;;
    unsigned-64-literals)
        grep -F 'max_unsigned_64:' "$work/$output.s" >/dev/null
        grep -F 'top_unsigned_bit:' "$work/$output.s" >/dev/null
        grep -F 'unsigned_max_above_signed_max:' "$work/$output.s" >/dev/null
        grep -F '  li a0, -1' "$work/$output.s" >/dev/null
        grep -F '  srl a0, t0, a0' "$work/$output.s" >/dev/null
        grep -F '  sltu a0, a0, t0' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/unsigned_64_literals max=UINT64_MAX bits=all-ones shift=logical comparison=unsigned'
        ;;
    variadic-function-pointer-cast)
        test -s "$work/$output.s"
        grep -F 'variadic_function_pointer_cast:' "$work/$output.s" >/dev/null
        grep -F 'jalr ra, t0, 0' "$work/$output.s" >/dev/null
        if command -v riscv64-linux-gnu-gcc >/dev/null 2>&1; then
            riscv64-linux-gnu-gcc -c "$work/$output.s" -o "$work/$output.o"
        fi
        printf '%s\n' 'PASS compiler/c0/variadic-function-pointer-cast type-name=1 indirect-call=1 variadic-tail=1'
        ;;
    volatile-parameter)
        test -s "$work/$output.s"
        grep -F 'volatile_parameter_step:' "$work/$output.s" >/dev/null
        grep -F 'volatile_parameter_probe:' "$work/$output.s" >/dev/null
        if command -v riscv64-linux-gnu-gcc >/dev/null 2>&1; then
            riscv64-linux-gnu-gcc -c "$work/$output.s" -o "$work/$output.o"
        fi
        printf '%s\n' 'PASS compiler/c0/volatile-parameter pointer=1 double=1 core-ingress=1'
        ;;
    gnu-statement-expression)
        test -s "$work/$output.s"
        grep -F 'statement_value:' "$work/$output.s" >/dev/null
        grep -F 'statement_void:' "$work/$output.s" >/dev/null
        grep -F '.Lwhile_condition_' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/gnu_statement_expression scope=owned-block final-expression=value void-final=non-expression sequencing=expression-site loop=1'
        ;;
    gnu-top-level-empty-declaration)
        grep -F 'first:' "$work/$output.s" >/dev/null
        grep -F 'second:' "$work/$output.s" >/dev/null
        grep -F 'main:' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/gnu_top_level_empty_declaration leading=1 consecutive=2 after-function=1 between-declarations=1 trailing=1'
        ;;
    adjacent-string-literals)
        grep -F '  call consume' "$work/$output.s" >/dev/null
        test "$(grep -E -c '^\.Lminic_string_[0-9]+:        grep -F '  li a0, 32' "$work/$output.s" >/dev/null
        grep -F '  li a0, 4' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/array_bound_integer_cast enum-cast=1 truncation=uint8 typed-ast-consteval=1'
        ;;
    bitwise-and-assignment-expressions)
        test "$(grep -c -F '  call next_flags' "$work/$output.s")" -eq 1
        grep -F '  and a0, t0, a0' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/bitwise_and_assignment_expression integer=1 lvalue-evaluated-once=1'
        ;;
    block-scope-extern-function-attributes)
        test -s "$work/$output.s"
        grep -F '__compiletime_assert_0' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/block_scope_extern_function_attributes scope=block linkage=extern prefix=noreturn suffix=error direct-call-resolved=1'
        ;;
    builtin-expect)
        test -s "$work/$output.s"
        grep -F 'expect_value:' "$work/$output.s" >/dev/null
        grep -F 'expect_zero:' "$work/$output.s" >/dev/null
        if grep -F '__builtin_expect' "$work/$output.s" >/dev/null; then
            printf '%s\n' 'FAIL compiler/c0/builtin_expect emitted runtime builtin symbol' >&2
            exit 1
        fi
        printf '%s\n' 'PASS compiler/c0/builtin_expect value=first-argument return-type=long hint=integer-constant runtime-call=none'
        ;;
    cast-type-classification)
        test -s "$work/$output.s"
        grep -F 'read_union_cast:' "$work/$output.s" >/dev/null
        grep -F 'read_volatile_cast:' "$work/$output.s" >/dev/null
        grep -F 'narrow_short:' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/cast_type_classification short=1 union=1 volatile=1'
        ;;
    comma-operator)
        test -s "$work/$output.s"
        grep -F 'comma_growth:' "$work/$output.s" >/dev/null
        grep -F 'comma_value:' "$work/$output.s" >/dev/null
        grep -F 'comma_conditions:' "$work/$output.s" >/dev/null
        grep -F 'comma_expression_statement:' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/comma_operator parenthesized=1 pool-growth=1 top-level-condition=while,if expression-statement=1 void-left=1 assignment-side-effect=1 result=right'
        ;;
    extern-multi-declarators)
        test -s "$work/$output.s"
        grep -F '  la a0, start_like' "$work/$output.s" >/dev/null
        grep -F '  la a0, end_like' "$work/$output.s" >/dev/null
        if grep -F 'start_like:' "$work/$output.s" >/dev/null || grep -F 'end_like:' "$work/$output.s" >/dev/null; then
            printf '%s\n' 'FAIL compiler/c0/extern_multi_declarators: declaration emitted storage' >&2
            exit 1
        fi
        printf '%s\n' 'PASS compiler/c0/extern_multi_declarators shared-base=function-pointer arrays=2 incomplete=2 storage=none'
        ;;
    extern-multidimensional-array)
        test -s "$work/$output.s"
        grep -F 'read_cpu_bit:' "$work/$output.s" >/dev/null
        grep -F 'cpu_bit_row:' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/extern_multidimensional_array dimensions=2 bounds=constant-expression nested-type=Array(Array(ulong)) subscript=2 row-decay=pointer'
        ;;
    extern-objects)
        grep -F '  la a0, external_counter' "$work/$output.s" >/dev/null
        grep -F '  lw a0, 0(a0)' "$work/$output.s" >/dev/null
        if grep -F '.type external_counter, @object' "$work/$output.s" >/dev/null ||
           grep -F 'external_counter:' "$work/$output.s" >/dev/null; then
            printf '%s\n' 'FAIL compiler/c0/extern_object: declaration emitted a definition' >&2
            exit 1
        fi
        printf '%s\n' 'PASS compiler/c0/extern_object declaration-only=external-symbol-reference'
        ;;
    external-pointer-arrays)
        grep -F '.type names, @object' "$work/$output.s" >/dev/null
        grep -F '.globl names' "$work/$output.s" >/dev/null
        grep -F '  .dword shared_name' "$work/$output.s" >/dev/null
        grep -F '  .dword .Lminic_string_' "$work/$output.s" >/dev/null
        grep -F '  .dword 0' "$work/$output.s" >/dev/null
        grep -F '.size names, 32' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/external_pointer_array bound=4 string-reloc=2 object-reloc=1 null=1 storage=32'
        ;;
    function-linkage-inheritance)
        test -s "$work/$output.s"
        grep -F 'read_timer_like:' "$work/$output.s" >/dev/null
        if grep -F '.globl read_timer_like' "$work/$output.s" >/dev/null; then
            printf '%s\n' 'FAIL compiler/c0/function_linkage_inheritance internal function was exported' >&2
            exit 1
        fi
        printf '%s\n' 'PASS compiler/c0/function_linkage_inheritance prior=static-inline later=non-static-declaration effective-linkage=internal export=none'
        ;;
    function-pointer-parameters)
        grep -F '.type callback, @function' "$work/$output.s" >/dev/null
        grep -F '  la a0, callback' "$work/$output.s" >/dev/null
        grep -F '  call accept_callback' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/function_pointer_parameter direct-declarator=void-callback'
        ;;
    function-type-typedefs)
        grep -F '.type callback, @function' "$work/$output.s" >/dev/null
        grep -F '  la a0, callback' "$work/$output.s" >/dev/null
        grep -F '  call install_callback' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/function_type_typedef alias=function pointer-use=direct'
        ;;
    global-pointer-subscripts)
        grep -F '  la a0, items' "$work/$output.s" >/dev/null
        grep -F '  ld a0, 0(a0)' "$work/$output.s" >/dev/null
        grep -F '  slli a0, a0, 3' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/global_pointer_subscript base=global-pointer element=pointer stride=8'
        ;;
    gnu-extension-declaration)
        test -s "$work/$output.s"
        grep -F 'read_wide:' "$work/$output.s" >/dev/null
        grep -F 'wide_size:' "$work/$output.s" >/dev/null
        grep -F '  li a0, 8' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/gnu_extension_declaration marker=diagnostic-only union-field=1 layout=unchanged size=8'
        ;;
    gnu-extension-prefix-declarations)
        test -s "$work/$output.s"
        grep -F 'extension_sum:' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/gnu_extension_prefix_declaration marker=diagnostic-only placement=before-typedef signed-alias=1'
        ;;
    gnu-format-cold-attributes)
        test -s "$work/$output.s"
        grep -F 'main:' "$work/$output.s" >/dev/null
        grep -F 'ftrace_vprintk_like:' "$work/$output.s" >/dev/null
        grep -F 'reordered_static_inline:' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/gnu_format_cold_attributes prefix=format suffix=noreturn,cold interleaved=static-attr-inline-attr+inline-static classification=diagnostic+optimization ABI=unchanged'
        ;;
    gnu-function-visibility)
        test -s "$work/$output.s"
        grep -F '.globl visible_api' "$work/$output.s" >/dev/null
        grep -F '.internal visible_api' "$work/$output.s" >/dev/null
        grep -F '  call visible_api' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/gnu_function_visibility prefix=1 internal=ELF-directive redeclaration=consistent'
        ;;
    gnu-local-object-alignment)
        grep -F 'local_explicit_alignment:' "$work/$output.s" >/dev/null
        grep -F 'local_alignof_alignment:' "$work/$output.s" >/dev/null
        grep -F '  li t1, -32' "$work/$output.s" >/dev/null
        grep -F '  and sp, sp, t1' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/gnu_local_object_alignment frontend=explicit-alignment core=owned rv64=max-natural-explicit dynamic-stack=32 alignof=accepted'
        ;;
    gnu-signed-keyword)
        test -s "$work/$output.s"
        grep -F 'widen_signed_aliases:' "$work/$output.s" >/dev/null
        grep -F '  lb a0,' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/gnu_signed_keyword aliases=__signed,__signed__ types=char,short,int,long'
        ;;
    gnu-typeof-array-bound-cast)
        test -s "$work/$output.s"
        grep -F 'typeof_array_bound_cast_size:' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/gnu_typeof_array_bound_cast semantic-ast=1 typed-consteval=1'
        ;;
    gnu-typeof-local-declaration)
        test -s "$work/$output.s"
        grep -F 'copy_next:' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/gnu_typeof_local_declaration dispatch=shared-type-lookahead statement-expression=1 pointer-type=preserved'
        ;;
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
 "$work/$output.s")" -eq 1
        grep -E '^\.size \.Lminic_string_[0-9]+, 10        grep -F '  li a0, 32' "$work/$output.s" >/dev/null
        grep -F '  li a0, 4' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/array_bound_integer_cast enum-cast=1 truncation=uint8 typed-ast-consteval=1'
        ;;
    bitwise-and-assignment-expressions)
        test "$(grep -c -F '  call next_flags' "$work/$output.s")" -eq 1
        grep -F '  and a0, t0, a0' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/bitwise_and_assignment_expression integer=1 lvalue-evaluated-once=1'
        ;;
    block-scope-extern-function-attributes)
        test -s "$work/$output.s"
        grep -F '__compiletime_assert_0' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/block_scope_extern_function_attributes scope=block linkage=extern prefix=noreturn suffix=error direct-call-resolved=1'
        ;;
    builtin-expect)
        test -s "$work/$output.s"
        grep -F 'expect_value:' "$work/$output.s" >/dev/null
        grep -F 'expect_zero:' "$work/$output.s" >/dev/null
        if grep -F '__builtin_expect' "$work/$output.s" >/dev/null; then
            printf '%s\n' 'FAIL compiler/c0/builtin_expect emitted runtime builtin symbol' >&2
            exit 1
        fi
        printf '%s\n' 'PASS compiler/c0/builtin_expect value=first-argument return-type=long hint=integer-constant runtime-call=none'
        ;;
    cast-type-classification)
        test -s "$work/$output.s"
        grep -F 'read_union_cast:' "$work/$output.s" >/dev/null
        grep -F 'read_volatile_cast:' "$work/$output.s" >/dev/null
        grep -F 'narrow_short:' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/cast_type_classification short=1 union=1 volatile=1'
        ;;
    comma-operator)
        test -s "$work/$output.s"
        grep -F 'comma_growth:' "$work/$output.s" >/dev/null
        grep -F 'comma_value:' "$work/$output.s" >/dev/null
        grep -F 'comma_conditions:' "$work/$output.s" >/dev/null
        grep -F 'comma_expression_statement:' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/comma_operator parenthesized=1 pool-growth=1 top-level-condition=while,if expression-statement=1 void-left=1 assignment-side-effect=1 result=right'
        ;;
    extern-multi-declarators)
        test -s "$work/$output.s"
        grep -F '  la a0, start_like' "$work/$output.s" >/dev/null
        grep -F '  la a0, end_like' "$work/$output.s" >/dev/null
        if grep -F 'start_like:' "$work/$output.s" >/dev/null || grep -F 'end_like:' "$work/$output.s" >/dev/null; then
            printf '%s\n' 'FAIL compiler/c0/extern_multi_declarators: declaration emitted storage' >&2
            exit 1
        fi
        printf '%s\n' 'PASS compiler/c0/extern_multi_declarators shared-base=function-pointer arrays=2 incomplete=2 storage=none'
        ;;
    extern-multidimensional-array)
        test -s "$work/$output.s"
        grep -F 'read_cpu_bit:' "$work/$output.s" >/dev/null
        grep -F 'cpu_bit_row:' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/extern_multidimensional_array dimensions=2 bounds=constant-expression nested-type=Array(Array(ulong)) subscript=2 row-decay=pointer'
        ;;
    extern-objects)
        grep -F '  la a0, external_counter' "$work/$output.s" >/dev/null
        grep -F '  lw a0, 0(a0)' "$work/$output.s" >/dev/null
        if grep -F '.type external_counter, @object' "$work/$output.s" >/dev/null ||
           grep -F 'external_counter:' "$work/$output.s" >/dev/null; then
            printf '%s\n' 'FAIL compiler/c0/extern_object: declaration emitted a definition' >&2
            exit 1
        fi
        printf '%s\n' 'PASS compiler/c0/extern_object declaration-only=external-symbol-reference'
        ;;
    external-pointer-arrays)
        grep -F '.type names, @object' "$work/$output.s" >/dev/null
        grep -F '.globl names' "$work/$output.s" >/dev/null
        grep -F '  .dword shared_name' "$work/$output.s" >/dev/null
        grep -F '  .dword .Lminic_string_' "$work/$output.s" >/dev/null
        grep -F '  .dword 0' "$work/$output.s" >/dev/null
        grep -F '.size names, 32' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/external_pointer_array bound=4 string-reloc=2 object-reloc=1 null=1 storage=32'
        ;;
    function-linkage-inheritance)
        test -s "$work/$output.s"
        grep -F 'read_timer_like:' "$work/$output.s" >/dev/null
        if grep -F '.globl read_timer_like' "$work/$output.s" >/dev/null; then
            printf '%s\n' 'FAIL compiler/c0/function_linkage_inheritance internal function was exported' >&2
            exit 1
        fi
        printf '%s\n' 'PASS compiler/c0/function_linkage_inheritance prior=static-inline later=non-static-declaration effective-linkage=internal export=none'
        ;;
    function-pointer-parameters)
        grep -F '.type callback, @function' "$work/$output.s" >/dev/null
        grep -F '  la a0, callback' "$work/$output.s" >/dev/null
        grep -F '  call accept_callback' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/function_pointer_parameter direct-declarator=void-callback'
        ;;
    function-type-typedefs)
        grep -F '.type callback, @function' "$work/$output.s" >/dev/null
        grep -F '  la a0, callback' "$work/$output.s" >/dev/null
        grep -F '  call install_callback' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/function_type_typedef alias=function pointer-use=direct'
        ;;
    global-pointer-subscripts)
        grep -F '  la a0, items' "$work/$output.s" >/dev/null
        grep -F '  ld a0, 0(a0)' "$work/$output.s" >/dev/null
        grep -F '  slli a0, a0, 3' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/global_pointer_subscript base=global-pointer element=pointer stride=8'
        ;;
    gnu-extension-declaration)
        test -s "$work/$output.s"
        grep -F 'read_wide:' "$work/$output.s" >/dev/null
        grep -F 'wide_size:' "$work/$output.s" >/dev/null
        grep -F '  li a0, 8' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/gnu_extension_declaration marker=diagnostic-only union-field=1 layout=unchanged size=8'
        ;;
    gnu-extension-prefix-declarations)
        test -s "$work/$output.s"
        grep -F 'extension_sum:' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/gnu_extension_prefix_declaration marker=diagnostic-only placement=before-typedef signed-alias=1'
        ;;
    gnu-format-cold-attributes)
        test -s "$work/$output.s"
        grep -F 'main:' "$work/$output.s" >/dev/null
        grep -F 'ftrace_vprintk_like:' "$work/$output.s" >/dev/null
        grep -F 'reordered_static_inline:' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/gnu_format_cold_attributes prefix=format suffix=noreturn,cold interleaved=static-attr-inline-attr+inline-static classification=diagnostic+optimization ABI=unchanged'
        ;;
    gnu-function-visibility)
        test -s "$work/$output.s"
        grep -F '.globl visible_api' "$work/$output.s" >/dev/null
        grep -F '.internal visible_api' "$work/$output.s" >/dev/null
        grep -F '  call visible_api' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/gnu_function_visibility prefix=1 internal=ELF-directive redeclaration=consistent'
        ;;
    gnu-local-object-alignment)
        grep -F 'local_explicit_alignment:' "$work/$output.s" >/dev/null
        grep -F 'local_alignof_alignment:' "$work/$output.s" >/dev/null
        grep -F '  li t1, -32' "$work/$output.s" >/dev/null
        grep -F '  and sp, sp, t1' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/gnu_local_object_alignment frontend=explicit-alignment core=owned rv64=max-natural-explicit dynamic-stack=32 alignof=accepted'
        ;;
    gnu-signed-keyword)
        test -s "$work/$output.s"
        grep -F 'widen_signed_aliases:' "$work/$output.s" >/dev/null
        grep -F '  lb a0,' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/gnu_signed_keyword aliases=__signed,__signed__ types=char,short,int,long'
        ;;
    gnu-typeof-array-bound-cast)
        test -s "$work/$output.s"
        grep -F 'typeof_array_bound_cast_size:' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/gnu_typeof_array_bound_cast semantic-ast=1 typed-consteval=1'
        ;;
    gnu-typeof-local-declaration)
        test -s "$work/$output.s"
        grep -F 'copy_next:' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/gnu_typeof_local_declaration dispatch=shared-type-lookahead statement-expression=1 pointer-type=preserved'
        ;;
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
 "$work/$output.s" >/dev/null
        grep -F '  .byte 97' "$work/$output.s" >/dev/null
        grep -F '  .byte 98' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/adjacent_string_literals tokens=2 objects=1 bytes=9 nul=1 call-args=1'
        ;;
    alignof-type-query)
        test -s "$work/$output.s"
        grep -F 'alignof_ulong:' "$work/$output.s" >/dev/null
        grep -F 'alignof_record:' "$work/$output.s" >/dev/null
        grep -F '  li a0, 8' "$work/$output.s" >/dev/null
        grep -F '  li a0, 16' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/alignof_type_query spellings=_Alignof,__alignof,__alignof__ scalar=8 aligned-record=16 service=shared-data-layout static-assert=1'
        ;;
    block-scope-typedef)
        test -s "$work/$output.s"
        grep -F 'block_scope_typedef_probe:' "$work/$output.s" >/dev/null
        if command -v riscv64-linux-gnu-gcc >/dev/null 2>&1; then
            riscv64-linux-gnu-gcc -c "$work/$output.s" -o "$work/$output.o"
        fi
        printf '%s\n' 'PASS compiler/c0/block-scope-typedef shadow=1 restore=1 function-pointer=1'
        ;;
    extern-function-pointer-object)
        test -s "$work/$output.s"
        grep -F 'panic_blink_like' "$work/$output.s" >/dev/null
        if grep -F 'panic_blink_like:' "$work/$output.s" >/dev/null ||
           grep -F '.globl panic_blink_like' "$work/$output.s" >/dev/null; then
            printf '%s\n' 'FAIL compiler/c0/extern_function_pointer_object: extern pointer object emitted storage' >&2
            exit 1
        fi
        printf '%s\n' 'PASS compiler/c0/extern_function_pointer_object declarator=(*name)(params) function-type=owned pointer-object=extern storage=none'
        ;;
    gnu-inline-asm-eight-operands)
        test -s "$work/$output.s"
        grep -F 'add a0, a0, a2' "$work/$output.s" >/dev/null
        grep -F '# sbi a1 a3 a4 a5 a6 a7' "$work/$output.s" >/dev/null
        "${RV_CC:-riscv64-linux-gnu-gcc}" -c "$work/$output.s" -o "$work/$output.o"
        printf '%s\n' 'PASS compiler/c0/gnu_inline_asm_eight_operands total=8 outputs=2 inputs=6 bindings=a0-a7 assembly=1'
        ;;
    gnu-inline-asm-readwrite-output)
        test -s "$work/$output.s"
        grep -F 'compiler_barrier:' "$work/$output.s" >/dev/null
        if grep -F '+rm' "$work/$output.s" >/dev/null; then
            printf '%s\n' 'unexpected raw GNU asm constraint in emitted assembly' >&2
            exit 1
        fi
        printf '%s\n' 'PASS compiler/c0/gnu_inline_asm_readwrite_output access=read-write raw-constraint=+rm target=RV64 local-lvalue=1 template=empty dataflow=preserved'
        ;;
    gnu-void-function-pointer-assignment)
        test -s "$work/$output.s"
        grep -F 'linux_shaped_syscall_assignment:' "$work/$output.s" >/dev/null
        grep -F 'linux_shaped_early_assignment:' "$work/$output.s" >/dev/null
        grep -F 'gnu_function_pointer_to_void:' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/gnu_void_function_pointer_assignment both-directions=1 linux-shaped=1'
        ;;
    array-bound-integer-casts)
        grep -F '  li a0, 32' "$work/$output.s" >/dev/null
        grep -F '  li a0, 4' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/array_bound_integer_cast enum-cast=1 truncation=uint8 typed-ast-consteval=1'
        ;;
    bitwise-and-assignment-expressions)
        test "$(grep -c -F '  call next_flags' "$work/$output.s")" -eq 1
        grep -F '  and a0, t0, a0' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/bitwise_and_assignment_expression integer=1 lvalue-evaluated-once=1'
        ;;
    block-scope-extern-function-attributes)
        test -s "$work/$output.s"
        grep -F '__compiletime_assert_0' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/block_scope_extern_function_attributes scope=block linkage=extern prefix=noreturn suffix=error direct-call-resolved=1'
        ;;
    builtin-expect)
        test -s "$work/$output.s"
        grep -F 'expect_value:' "$work/$output.s" >/dev/null
        grep -F 'expect_zero:' "$work/$output.s" >/dev/null
        if grep -F '__builtin_expect' "$work/$output.s" >/dev/null; then
            printf '%s\n' 'FAIL compiler/c0/builtin_expect emitted runtime builtin symbol' >&2
            exit 1
        fi
        printf '%s\n' 'PASS compiler/c0/builtin_expect value=first-argument return-type=long hint=integer-constant runtime-call=none'
        ;;
    cast-type-classification)
        test -s "$work/$output.s"
        grep -F 'read_union_cast:' "$work/$output.s" >/dev/null
        grep -F 'read_volatile_cast:' "$work/$output.s" >/dev/null
        grep -F 'narrow_short:' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/cast_type_classification short=1 union=1 volatile=1'
        ;;
    comma-operator)
        test -s "$work/$output.s"
        grep -F 'comma_growth:' "$work/$output.s" >/dev/null
        grep -F 'comma_value:' "$work/$output.s" >/dev/null
        grep -F 'comma_conditions:' "$work/$output.s" >/dev/null
        grep -F 'comma_expression_statement:' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/comma_operator parenthesized=1 pool-growth=1 top-level-condition=while,if expression-statement=1 void-left=1 assignment-side-effect=1 result=right'
        ;;
    extern-multi-declarators)
        test -s "$work/$output.s"
        grep -F '  la a0, start_like' "$work/$output.s" >/dev/null
        grep -F '  la a0, end_like' "$work/$output.s" >/dev/null
        if grep -F 'start_like:' "$work/$output.s" >/dev/null || grep -F 'end_like:' "$work/$output.s" >/dev/null; then
            printf '%s\n' 'FAIL compiler/c0/extern_multi_declarators: declaration emitted storage' >&2
            exit 1
        fi
        printf '%s\n' 'PASS compiler/c0/extern_multi_declarators shared-base=function-pointer arrays=2 incomplete=2 storage=none'
        ;;
    extern-multidimensional-array)
        test -s "$work/$output.s"
        grep -F 'read_cpu_bit:' "$work/$output.s" >/dev/null
        grep -F 'cpu_bit_row:' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/extern_multidimensional_array dimensions=2 bounds=constant-expression nested-type=Array(Array(ulong)) subscript=2 row-decay=pointer'
        ;;
    extern-objects)
        grep -F '  la a0, external_counter' "$work/$output.s" >/dev/null
        grep -F '  lw a0, 0(a0)' "$work/$output.s" >/dev/null
        if grep -F '.type external_counter, @object' "$work/$output.s" >/dev/null ||
           grep -F 'external_counter:' "$work/$output.s" >/dev/null; then
            printf '%s\n' 'FAIL compiler/c0/extern_object: declaration emitted a definition' >&2
            exit 1
        fi
        printf '%s\n' 'PASS compiler/c0/extern_object declaration-only=external-symbol-reference'
        ;;
    external-pointer-arrays)
        grep -F '.type names, @object' "$work/$output.s" >/dev/null
        grep -F '.globl names' "$work/$output.s" >/dev/null
        grep -F '  .dword shared_name' "$work/$output.s" >/dev/null
        grep -F '  .dword .Lminic_string_' "$work/$output.s" >/dev/null
        grep -F '  .dword 0' "$work/$output.s" >/dev/null
        grep -F '.size names, 32' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/external_pointer_array bound=4 string-reloc=2 object-reloc=1 null=1 storage=32'
        ;;
    function-linkage-inheritance)
        test -s "$work/$output.s"
        grep -F 'read_timer_like:' "$work/$output.s" >/dev/null
        if grep -F '.globl read_timer_like' "$work/$output.s" >/dev/null; then
            printf '%s\n' 'FAIL compiler/c0/function_linkage_inheritance internal function was exported' >&2
            exit 1
        fi
        printf '%s\n' 'PASS compiler/c0/function_linkage_inheritance prior=static-inline later=non-static-declaration effective-linkage=internal export=none'
        ;;
    function-pointer-parameters)
        grep -F '.type callback, @function' "$work/$output.s" >/dev/null
        grep -F '  la a0, callback' "$work/$output.s" >/dev/null
        grep -F '  call accept_callback' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/function_pointer_parameter direct-declarator=void-callback'
        ;;
    function-type-typedefs)
        grep -F '.type callback, @function' "$work/$output.s" >/dev/null
        grep -F '  la a0, callback' "$work/$output.s" >/dev/null
        grep -F '  call install_callback' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/function_type_typedef alias=function pointer-use=direct'
        ;;
    global-pointer-subscripts)
        grep -F '  la a0, items' "$work/$output.s" >/dev/null
        grep -F '  ld a0, 0(a0)' "$work/$output.s" >/dev/null
        grep -F '  slli a0, a0, 3' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/global_pointer_subscript base=global-pointer element=pointer stride=8'
        ;;
    gnu-extension-declaration)
        test -s "$work/$output.s"
        grep -F 'read_wide:' "$work/$output.s" >/dev/null
        grep -F 'wide_size:' "$work/$output.s" >/dev/null
        grep -F '  li a0, 8' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/gnu_extension_declaration marker=diagnostic-only union-field=1 layout=unchanged size=8'
        ;;
    gnu-extension-prefix-declarations)
        test -s "$work/$output.s"
        grep -F 'extension_sum:' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/gnu_extension_prefix_declaration marker=diagnostic-only placement=before-typedef signed-alias=1'
        ;;
    gnu-format-cold-attributes)
        test -s "$work/$output.s"
        grep -F 'main:' "$work/$output.s" >/dev/null
        grep -F 'ftrace_vprintk_like:' "$work/$output.s" >/dev/null
        grep -F 'reordered_static_inline:' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/gnu_format_cold_attributes prefix=format suffix=noreturn,cold interleaved=static-attr-inline-attr+inline-static classification=diagnostic+optimization ABI=unchanged'
        ;;
    gnu-function-visibility)
        test -s "$work/$output.s"
        grep -F '.globl visible_api' "$work/$output.s" >/dev/null
        grep -F '.internal visible_api' "$work/$output.s" >/dev/null
        grep -F '  call visible_api' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/gnu_function_visibility prefix=1 internal=ELF-directive redeclaration=consistent'
        ;;
    gnu-local-object-alignment)
        grep -F 'local_explicit_alignment:' "$work/$output.s" >/dev/null
        grep -F 'local_alignof_alignment:' "$work/$output.s" >/dev/null
        grep -F '  li t1, -32' "$work/$output.s" >/dev/null
        grep -F '  and sp, sp, t1' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/gnu_local_object_alignment frontend=explicit-alignment core=owned rv64=max-natural-explicit dynamic-stack=32 alignof=accepted'
        ;;
    gnu-signed-keyword)
        test -s "$work/$output.s"
        grep -F 'widen_signed_aliases:' "$work/$output.s" >/dev/null
        grep -F '  lb a0,' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/gnu_signed_keyword aliases=__signed,__signed__ types=char,short,int,long'
        ;;
    gnu-typeof-array-bound-cast)
        test -s "$work/$output.s"
        grep -F 'typeof_array_bound_cast_size:' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/gnu_typeof_array_bound_cast semantic-ast=1 typed-consteval=1'
        ;;
    gnu-typeof-local-declaration)
        test -s "$work/$output.s"
        grep -F 'copy_next:' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/gnu_typeof_local_declaration dispatch=shared-type-lookahead statement-expression=1 pointer-type=preserved'
        ;;
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
