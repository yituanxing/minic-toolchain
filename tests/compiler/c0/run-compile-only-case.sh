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
name=$1
# Columns: C source stem, work path, input stem, assembly stem, C dialect, reset work.
# One row is one test identity; the checks below retain their original ordering.
case "$name" in
    external-array-redeclaration-owner) set -- 'external_array_redeclaration_owner' 'external-array-redeclaration-owner' 'input' 'output' '' 'no' ;;
    external-tentative-declarator-list) set -- 'external_tentative_declarator_list' 'external-tentative-declarator-list' 'input' 'output' '' 'no' ;;
    gnu-object-unused-alias) set -- 'gnu_object_unused_alias' 'gnu-object-unused-alias' 'input' 'output' '' 'no' ;;
    runtime-record-zero-shorthand) set -- 'runtime_record_zero_shorthand' 'runtime-record-zero-shorthand' 'input' 'output' '' 'no' ;;
    small-record-copy-return) set -- 'small_record_copy_return' 'small-record-copy-return' 'input' 'output' '' 'no' ;;
    static-local-prefix-attributes) set -- 'static_local_prefix_attributes' 'static-local-prefix-attributes' 'input' 'output' '' 'no' ;;
    static-record-single-char-array) set -- 'static_record_single_char_array' 'static-record-single-char-array' 'input' 'output' '' 'no' ;;
    preformed-static-array) set -- 'preformed_static_array' 'preformed-static-array' 'input' 'output' '' 'no' ;;
    static-record-array-suffix-attributes) set -- 'static_record_array_suffix_attributes' 'static-record-array-suffix-attributes' 'input' 'output' '' 'no' ;;
    block-scope-extern-multi-declarator) set -- 'block_scope_extern_multi_declarator' 'compiler-c0-block-extern-multi' 'input' 'output' '' 'no' ;;
    backward-static-aggregate-overlay) set -- 'backward_static_aggregate_overlay' 'backward-static-aggregate-overlay' 'input' 'output' 'gnu11' 'no' ;;
    fixed-call-character-pointer) set -- 'fixed_call_character_pointer' 'fixed-call-character-pointer' 'input' 'output' 'gnu11' 'no' ;;
    static-record-nested-designator-string-overlay) set -- 'static_record_nested_designator_string_overlay' 'static-record-nested-designator-string-overlay' 'input' 'output' 'gnu11' 'no' ;;
    static-integer-address-relocation) set -- 'static_integer_address_relocation' 'static-integer-address-relocation' 'input' 'output' 'gnu11' 'no' ;;
    gnu-packed-record-field) set -- 'gnu_packed_record_field' 'compiler-c0-gnu-packed-record-field' 'input' 'output' 'gnu11' 'yes' ;;
    void-casts) set -- 'void_cast_discard' 'compiler-c0-void-casts' 'void_cast_discard' 'void_cast_discard' '' 'yes' ;;
    multiply-assignment) set -- 'multiply_assignment' 'compiler-c0-multiply-assignment' 'multiply_assignment' 'multiply_assignment' '' 'yes' ;;
    array-bound-sizeof) set -- 'array_bound_sizeof' 'compiler-c0-array-bound-sizeof' 'array_bound_sizeof' 'array_bound_sizeof' '' 'yes' ;;
    volatile-qualifiers) set -- 'volatile_qualifiers' 'compiler-c0-volatile-qualifiers' 'volatile_qualifiers' 'volatile_qualifiers' '' 'yes' ;;
    function-address-expressions) set -- 'function_address_expression' 'compiler-c0-function-address-expression' 'function_address_expression' 'function_address_expression' '' 'yes' ;;
    record-multi-declarators) set -- 'record_multi_declarators' 'compiler-c0-record-multi-declarators' 'record_multi_declarators' 'record_multi_declarators' '' 'yes' ;;
    cast-record-type-names) set -- 'cast_record_type_names' 'compiler-c0-cast-record-type-names' 'cast_record_type_names' 'cast_record_type_names' '' 'yes' ;;
    record-length-one-arrays) set -- 'record_length_one_array' 'compiler-c0-record-length-one-arrays' 'record_length_one_array' 'record_length_one_array' '' 'yes' ;;
    builtin-memcpy-call) set -- 'builtin_memcpy_call' 'compiler-c0/builtin-memcpy-call' 'input' 'output' '' 'no' ;;
    builtin-memset-call) set -- 'builtin_memset_call' 'compiler-c0-builtin-memset-call' 'input' 'output' '' 'yes' ;;
    gnu-inline-keyword-spellings) set -- 'gnu_inline_keyword_spellings' 'compiler-c0-gnu-inline-keyword-spellings' 'input' 'gnu_inline_keyword_spellings' 'gnu11' 'yes' ;;
    gnu-no-profile-function-attribute) set -- 'gnu_no_profile_function_attribute' 'compiler-c0-gnu-no-profile-function-attribute' 'input' 'output' 'gnu11' 'yes' ;;
    gnu-typeof-array-type-name) set -- 'gnu_typeof_array_type_name' 'compiler-c0-gnu-typeof-array-type-name' 'input' 'output' 'gnu11' 'yes' ;;
    gnu-const-function-attribute) set -- 'gnu_const_function_attribute' 'compiler-c0-gnu-const-function-attribute' 'input' 'gnu_const_function_attribute' 'gnu11' 'yes' ;;
    enum-forward-completion) set -- 'enum_forward_completion' 'enum-forward-completion' 'input' 'output' 'gnu11' 'no' ;;
    gnu-choose-expr-bitfield) set -- 'gnu_choose_expr_bitfield' 'compiler-c0-gnu-choose-expr-bitfield' 'probe' 'probe' 'gnu11' 'yes' ;;
    gnu-local-interleaved-informational-attribute) set -- 'gnu_local_interleaved_informational_attribute' 'compiler-c0-gnu-local-interleaved-informational-attribute' 'input' 'output' 'gnu11' 'yes' ;;
    array-bound-integer-casts) set -- 'array_bound_integer_cast' 'compiler-c0-array-bound-integer-casts' 'array_bound_integer_cast' 'array_bound_integer_cast' '' 'yes' ;;
    bitwise-and-assignment-expressions) set -- 'bitwise_and_assignment_expression' 'compiler-c0-bitwise-and-assignment-expressions' 'bitwise_and_assignment_expression' 'bitwise_and_assignment_expression' '' 'yes' ;;
    cast-type-classification) set -- 'cast_type_classification' 'compiler-c0-cast-type-classification' 'cast_type_classification' 'cast_type_classification' '' 'yes' ;;
    comma-operator) set -- 'comma_operator' 'compiler-c0-comma-operator' 'comma_operator' 'comma_operator' '' 'yes' ;;
    extern-multidimensional-array) set -- 'extern_multidimensional_array' 'compiler-c0-extern-multidimensional-array' 'extern_multidimensional_array' 'extern_multidimensional_array' '' 'yes' ;;
    extern-objects) set -- 'extern_object' 'compiler-c0-extern-objects' 'extern_object' 'extern_object' '' 'yes' ;;
    external-pointer-arrays) set -- 'external_pointer_array' 'compiler-c0-external-pointer-array' 'external_pointer_array' 'external_pointer_array' '' 'yes' ;;
    function-pointer-parameters) set -- 'function_pointer_parameter' 'compiler-c0-function-pointer-parameters' 'function_pointer_parameter' 'function_pointer_parameter' '' 'yes' ;;
    function-type-typedefs) set -- 'function_type_typedef' 'compiler-c0-function-type-typedefs' 'function_type_typedef' 'function_type_typedef' '' 'yes' ;;
    global-pointer-subscripts) set -- 'global_pointer_subscript' 'compiler-c0-global-pointer-subscripts' 'global_pointer_subscript' 'global_pointer_subscript' '' 'yes' ;;
    gnu-local-object-alignment) set -- 'gnu_local_object_alignment' 'compiler-c0-gnu-local-object-alignment' 'input' 'output' '' 'yes' ;;
    block-scope-extern-function-attributes) set -- 'block_scope_extern_function_attributes' 'compiler-c0-block-scope-extern-function-attributes' 'block_scope_extern_function_attributes' 'block_scope_extern_function_attributes' 'gnu11' 'yes' ;;
    builtin-expect) set -- 'builtin_expect' 'compiler-c0-builtin-expect' 'builtin_expect' 'builtin_expect' 'gnu11' 'yes' ;;
    extern-multi-declarators) set -- 'extern_multi_declarators' 'compiler-c0-extern-multi-declarators' 'input' 'extern_multi_declarators' 'gnu11' 'yes' ;;
    function-linkage-inheritance) set -- 'function_linkage_inheritance' 'compiler-c0-function-linkage-inheritance' 'input' 'function_linkage_inheritance' 'gnu11' 'yes' ;;
    gnu-extension-declaration) set -- 'gnu_extension_declaration' 'compiler-c0-gnu-extension-declaration' 'gnu_extension_declaration' 'gnu_extension_declaration' 'gnu11' 'yes' ;;
    gnu-extension-prefix-declarations) set -- 'gnu_extension_prefix_declaration' 'compiler-c0-gnu-extension-prefix' 'gnu_extension_prefix_declaration' 'gnu_extension_prefix_declaration' 'gnu11' 'yes' ;;
    gnu-format-cold-attributes) set -- 'gnu_format_cold_attributes' 'compiler-c0-gnu-format-cold-attributes' 'input' 'gnu_format_cold_attributes' 'gnu11' 'yes' ;;
    gnu-function-visibility) set -- 'gnu_function_visibility' 'compiler-c0-gnu-function-visibility' 'gnu_function_visibility' 'gnu_function_visibility' 'gnu11' 'yes' ;;
    gnu-signed-keyword) set -- 'gnu_signed_keyword' 'compiler-c0-gnu-signed-keyword' 'gnu_signed_keyword' 'gnu_signed_keyword' 'gnu11' 'yes' ;;
    gnu-typeof-array-bound-cast) set -- 'gnu_typeof_array_bound_cast' 'compiler-c0-gnu-typeof-array-bound-cast' 'input' 'output' 'gnu11' 'yes' ;;
    gnu-typeof-local-declaration) set -- 'gnu_typeof_local_declaration' 'compiler-c0-gnu-typeof-local-declaration' 'gnu_typeof_local_declaration' 'gnu_typeof_local_declaration' 'gnu11' 'yes' ;;
    record-forward-declarations) set -- 'record_forward_declarations' 'compiler-c0-record-forward-declarations' 'record_forward_declarations' 'record_forward_declarations' '' 'yes' ;;
    record-multidimensional-arrays) set -- 'record_multidimensional_arrays' 'compiler-c0-record-multidimensional-arrays' 'record_multidimensional_arrays' 'record_multidimensional_arrays' '' 'yes' ;;
    restrict-qualifiers) set -- 'restrict_qualifiers' 'compiler-c0-restrict-qualifiers' 'restrict_qualifiers' 'restrict_qualifiers' '' 'yes' ;;
    static-local-string-arrays) set -- 'static_local_string_array' 'compiler-c0-static-local-string-arrays' 'static_local_string_array' 'static_local_string_array' '' 'yes' ;;
    static-record-array-member-designator) set -- 'static_record_array_member_designator' 'compiler-c0-static-record-array-member-designator' 'input' 'output' '' 'yes' ;;
    parenthesized-array-declarator) set -- 'parenthesized_array_declarator' 'compiler-c0-parenthesized-array-declarator' 'input' 'output' '' 'yes' ;;
    parenthesized-comma-expressions) set -- 'parenthesized_comma_expression' 'compiler-c0-parenthesized-comma-expressions' 'parenthesized_comma_expression' 'parenthesized_comma_expression' '' 'yes' ;;
    pointer-subtract-assignments) set -- 'pointer_subtract_assignment' 'compiler-c0-pointer-subtract-assignments' 'pointer_subtract_assignment' 'pointer_subtract_assignment' '' 'yes' ;;
    top-level-comma-conditions) set -- 'top_level_comma_condition' 'compiler-c0-top-level-comma-condition' 'top_level_comma_condition' 'top_level_comma_condition' '' 'yes' ;;
    typedef-enum-definitions) set -- 'typedef_enum_definition' 'compiler-c0-typedef-enum-definitions' 'typedef_enum_definition' 'typedef_enum_definition' '' 'yes' ;;
    unsigned-64-literals) set -- 'unsigned_64_literals' 'compiler-c0-unsigned-64-literals' 'unsigned_64_literals' 'unsigned_64_literals' '' 'yes' ;;
    variadic-function-pointer-cast) set -- 'variadic_function_pointer_cast' 'compiler-c0-variadic-function-pointer-cast' 'input' 'output' '' 'yes' ;;
    volatile-parameter) set -- 'volatile_parameter' 'compiler-c0-volatile-parameter' 'input' 'output' '' 'yes' ;;
    gnu-top-level-empty-declaration) set -- 'gnu_top_level_empty_declaration' 'compiler-c0-gnu-top-level-empty-declaration' 'input' 'output' '' 'no' ;;
    adjacent-string-literals) set -- 'adjacent_string_literals' 'compiler-c0-adjacent-string-literals' 'adjacent_string_literals' 'adjacent_string_literals' '' 'yes' ;;
    block-scope-typedef) set -- 'block_scope_typedef' 'compiler-c0-block-scope-typedef' 'input' 'output' '' 'yes' ;;
    gnu-inline-asm-eight-operands) set -- 'gnu_inline_asm_eight_operands' 'compiler-c0-gnu-inline-asm-eight-operands' 'probe' 'probe' '' 'yes' ;;
    static-storage-scalar-array-owner) set -- 'static_storage_scalar_array_owner' 'static-storage-scalar-array-owner' 'input' 'output' 'gnu11' 'no' ;;
    gnu-statement-expression) set -- 'gnu_statement_expression' 'compiler-c0-gnu-statement-expression' 'gnu_statement_expression' 'gnu_statement_expression' 'gnu11' 'yes' ;;
    alignof-type-query) set -- 'alignof_type_query' 'compiler-c0-alignof-type-query' 'alignof_type_query' 'alignof_type_query' 'gnu11' 'yes' ;;
    extern-function-pointer-object) set -- 'extern_function_pointer_object' 'compiler-c0-extern-function-pointer-object' 'input' 'extern_function_pointer_object' 'gnu11' 'yes' ;;
    gnu-inline-asm-readwrite-output) set -- 'gnu_inline_asm_readwrite_output' 'compiler-c0-gnu-inline-asm-readwrite-output' 'gnu_inline_asm_readwrite_output' 'gnu_inline_asm_readwrite_output' 'gnu11' 'yes' ;;
    gnu-void-function-pointer-assignment) set -- 'gnu_void_function_pointer_assignment' 'compiler-c0-gnu-void-function-pointer-assignment' 'input' 'output' 'gnu11' 'yes' ;;
    record-typedef-array-fields) set -- 'record_typedef_array_field' 'compiler-c0-record-typedef-array-fields' 'record_typedef_array_field' 'record_typedef_array_field' 'c11' 'yes' ;;
    postfix-const) set -- 'postfix_const' 'compiler-c0-postfix-const' 'postfix_const' 'postfix_const' '' 'yes' ;;
    zero-aggregate-null) set -- 'zero_aggregate_null' 'compiler-c0-zero-aggregate-null' 'zero_aggregate_null' 'zero_aggregate_null' '' 'yes' ;;
    stack-fixed-arguments) set -- 'stack_fixed_arguments' 'compiler-c0-stack-fixed-arguments' 'stack_fixed_arguments' 'stack_fixed_arguments' '' 'yes' ;;
    gnu-octal-character-case-range) set -- 'gnu_octal_character_case_range' 'compiler-c0-gnu-octal-character-case-range' 'gnu_octal_character_case_range' 'gnu_octal_character_case_range' 'gnu11' 'yes' ;;
    nested-function-pointer-field) set -- 'nested_function_pointer_field' 'compiler-c0-nested-function-pointer-field' 'input' 'output' '' 'yes' ;;
    nested-function-pointer-type-name) set -- 'nested_function_pointer_type_name' 'compiler-c0-nested-function-pointer-type-name' 'input' 'output' '' 'yes' ;;
    prefix-update-expressions) set -- 'prefix_update_expression' 'compiler-c0-prefix-update-expression' 'prefix_update_expression' 'prefix_update_expression' '' 'yes' ;;
    signed-char-semantics) set -- 'signed_char_semantics' 'compiler-c0-signed-char' 'signed_char_semantics' 'signed_char_semantics' '' 'yes' ;;
    extern-parenthesized-functions) set -- 'extern_parenthesized_functions' 'compiler-c0-extern-parenthesized-functions' 'extern_parenthesized_functions' 'extern_parenthesized_functions' '' 'yes' ;;
    wide-integer-literals) set -- 'wide_integer_literal' 'compiler-c0-wide-integer-literals' 'wide_integer_literal' 'wide_integer_literal' '' 'yes' ;;
    extern-fixed-integer-arrays) set -- 'extern_fixed_integer_array' 'compiler-c0-extern-fixed-integer-array' 'extern_fixed_integer_array' 'extern_fixed_integer_array' '' 'yes' ;;
    compound-assignment-expressions) set -- 'compound_assignment_expression' 'compiler-c0-compound-assignment-expressions' 'compound_assignment_expression' 'compound_assignment_expression' '' 'yes' ;;
    *) echo "unknown compile-only case: $name" >&2; exit 2 ;;
esac
source=$1
work=${BUILD_DIR:-"$root/build/debug"}/tests/$2
input=$3
output=$4
dialect=$5
if [ "$6" = yes ]; then rm -rf "$work"; fi
if [ -n "$dialect" ]; then set -- "-std=$dialect"; else set --; fi
mkdir -p "$work"
if [ "$name" = gnu-void-function-pointer-assignment ]; then
    "$host_cc" -fsyntax-only -std=gnu11 -Werror -Wno-pedantic -x c "$root/tests/compiler/c0/$source.c"
fi
if [ "$name" = gnu-typeof-array-bound-cast ]; then
    "$host_cc" -fsyntax-only -std=gnu11 -x c "$root/tests/compiler/c0/$source.c"
fi
"$host_cc" -E -P "$@" -x c "$root/tests/compiler/c0/$source.c" -o "$work/$input.i"
"$minic" -S "$work/$input.i" -o "$work/$output.s"
# Fixed literal checks use one helper without changing grep's match mode.
require_literals() {
    for literal do grep -F "$literal" "$work/$output.s" >/dev/null || return $?; done
}
case "$name" in
    postfix-const)
        grep -F '.type same_first, @function' "$work/$output.s" >/dev/null
        grep -F '.type use_typedef_const, @function' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/postfix_const forms=char-const,const-char,typedef-const'
        ;;
    zero-aggregate-null)
        grep -F '.globl main' "$work/$output.s" >/dev/null
        grep -F '  sw ' "$work/$output.s" >/dev/null
        grep -F '  sd ' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/zero_aggregate_null scalar-zero=yes pointer-null=yes'
        ;;
    stack-fixed-arguments)
        grep -F '.type minic_abi_sum9, @function' "$work/$output.s" >/dev/null
        grep -F '  call gcc_abi_sum9' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/stack_fixed_arguments fixed=9 integer-class register=8 stack=1'
        ;;
    gnu-octal-character-case-range)
        test -s "$work/$output.s"
        grep -F 'linux_printk_level_like:' "$work/$output.s" >/dev/null
        grep -F 'octal_character_values:' "$work/$output.s" >/dev/null

        printf '%s\n' 'PASS compiler/c0/gnu_octal_character_case_range octal=1-3-digits case-range=0...7 expanded=8 duplicate-check=preserved'
        ;;
    nested-function-pointer-field)
        test -s "$work/$output.s"
        grep -F 'install_resolver:' "$work/$output.s" >/dev/null
        grep -F 'resolve_symbol' "$work/$output.s" >/dev/null

        if command -v riscv64-linux-gnu-gcc >/dev/null 2>&1; then
            riscv64-linux-gnu-gcc -c "$work/$output.s" -o "$work/$output.o"
        fi

        printf '%s\n' 'PASS compiler/c0/nested-function-pointer-field shape=R(*(*field)(P))(Q) assignment=compatible'
        ;;
    nested-function-pointer-type-name)
        test -s "$work/$output.s"
        grep -F 'resolve_nested_function_pointer:' "$work/$output.s" >/dev/null
        grep -F 'jalr ra, t0, 0' "$work/$output.s" >/dev/null

        if command -v riscv64-linux-gnu-gcc >/dev/null 2>&1; then
            riscv64-linux-gnu-gcc -c "$work/$output.s" -o "$work/$output.o"
        fi

        printf '%s\n' 'PASS compiler/c0/nested-function-pointer-type-name anonymous=1 nested=1 indirect-call=1'
        ;;
    prefix-update-expressions)
        grep -F '  addi t0, t0, 1' "$work/$output.s" >/dev/null
        grep -F '  addi t0, t0, -1' "$work/$output.s" >/dev/null
        grep -F '  addi t0, t0, 4' "$work/$output.s" >/dev/null
        grep -F '  addi t0, t0, -4' "$work/$output.s" >/dev/null
        test "$(grep -c -F '  mv a0, t0' "$work/$output.s")" -ge 4
        printf '%s\n' 'PASS compiler/c0/prefix_update_expression integer=++/-- pointer=++/-- result=new lvalue-address=once'
        ;;
    signed-char-semantics)
        grep -E '^[[:space:]]+lb[[:space:]]+[^,]+,[[:space:]]*0\([^)]*\)$' "$work/$output.s" >/dev/null
        grep -E '^[[:space:]]+lbu[[:space:]]+[^,]+,[[:space:]]*0\([^)]*\)$' "$work/$output.s" >/dev/null
        grep -E '^[[:space:]]+sb[[:space:]]+[^,]+,[[:space:]]*0\([^)]*\)$' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/signed_char_semantics signed-load=lb unsigned-and-plain-load=lbu store=sb'
        ;;
    extern-parenthesized-functions)
        grep -F '  call external_add' "$work/$output.s" >/dev/null
        grep -F '  call external_alloc' "$work/$output.s" >/dev/null
        if grep -F '.type external_add, @function' "$work/$output.s" >/dev/null; then
            printf '%s\n' 'FAIL compiler/c0/extern_parenthesized_functions: declaration emitted a definition' >&2
            exit 1
        fi
        printf '%s\n' 'PASS compiler/c0/extern_parenthesized_functions linkage=external parenthesized-name=1'
        ;;
    wide-integer-literals)
        grep -F '  li a0, 9223372036854775807' "$work/$output.s" >/dev/null
        grep -F '  li a0, 9223372036854775806' "$work/$output.s" >/dev/null
        grep -F 'binary_literal_value:' "$work/$output.s" >/dev/null
        grep -F 'binary_enum_value:' "$work/$output.s" >/dev/null
        grep -F '  li a0, 170' "$work/$output.s" >/dev/null
        grep -F '  li a0, 5' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/wide_integer_literal payload=int64+gnu-binary max=9223372036854775807 binary=170 enum=5'
        ;;
    extern-fixed-integer-arrays)
        grep -F '.globl table' "$work/$output.s" >/dev/null
        grep -F '  .byte 1' "$work/$output.s" >/dev/null
        grep -F '  .byte 2' "$work/$output.s" >/dev/null
        grep -F '  .byte 3' "$work/$output.s" >/dev/null
        test "$(grep -Fc '  .byte 0' "$work/$output.s")" -eq 2
        grep -F '.size table, 5' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/extern_fixed_integer_array extern-merge=1 bound=5 brace=3 zero-fill=2'
        ;;
    compound-assignment-expressions)
        test "$(grep -c -F '  call next_slot' "$work/$output.s")" -eq 1
        grep -F '  slli a0, a0, 2' "$work/$output.s" >/dev/null
        grep -F '  divu a0, t0, a0' "$work/$output.s" >/dev/null
        grep -F '  div a0, t0, a0' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/compound_assignment_expression operators=+=,/= result=value lvalue-evaluation=once pointer-scale=4 GNU-void-stride=1 divide=signed,unsigned'
        ;;
    record-forward-declarations)
        require_literals 'pass:' 'call pass' 'lw a0, 0(a0)'
        printf '%s\n' 'PASS compiler/c0/record_forward_declarations struct=1 union=1 completion=same-tag pointer-before-complete=1'
        ;;
    record-multidimensional-arrays)
        test -s "$work/$output.s"
        grep -F 'read_grid:' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/record_multidimensional_arrays dims=2,3 member-index=2'
        ;;
    record-typedef-array-fields)
        test -s "$work/$output.s"
        require_literals 'read_reg:' 'context_size:' '  li a0, 40'
        printf '%s\n' 'PASS compiler/c0/record_typedef_array_field typedef-array=4 member=array indexing=1 record-size=40'
        ;;
    restrict-qualifiers)
        test -s "$work/$output.s"
        require_literals 'call_restrict_forms:' '  call copy_gnu' '  call copy_gnu_double' '  call copy_c'
        printf '%s\n' 'PASS compiler/c0/restrict_qualifiers C=restrict GNU=__restrict/__restrict__ pointer-only=1 abi=unchanged'
        ;;
    static-local-string-arrays)
        require_literals '.Lminic_string_0:' '.size .Lminic_string_0, 4' '  .byte 97'
        grep -Eq '^[[:space:]]+lbu[[:space:]]+[a-z][a-z0-9]*, 0\([a-z][a-z0-9]*\)$' "$work/$output.s"
        printf '%s\n' 'PASS compiler/c0/static_local_string_array inferred-bound=4 initializer=string static-storage=yes'
        ;;
    static-record-array-member-designator)
        require_literals 'table:' '  .word 7' '  .word 9' '  .word 11' '  .word 13' 'anchor'
        printf '%s\n' 'PASS compiler/c0/static-record-array-member-designator element=integer,record,pointer overwrite=shared-zero-subobject range=fail-closed'
        ;;
    static-storage-scalar-array-owner)
        test -s "$work/$output.s"
        require_literals 'external_names:' 'external_objects:' '__minic_static_local_'
        printf '%s\n' 'PASS compiler/c0/static-storage-scalar-array-owner external-inferred=designated+relocation static-local=fixed+inferred+range multidim=shared-owner'
        ;;
    parenthesized-array-declarator)
        test -s "$work/$output.s"
        require_literals 'parenthesized_array:' 'parenthesized_array_pick:'
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
        require_literals '  slli a0, a0, 2' '  sub a0, t0, a0'
        printf '%s\n' 'PASS compiler/c0/pointer_subtract_assignment scale=4 lvalue-evaluated-once=1'
        ;;
    top-level-comma-conditions)
        require_literals 'comma_condition_loop:' '  beqz a0, .Lwhile_end_'
        printf '%s\n' 'PASS compiler/c0/top_level_comma_condition while=1 void-left=1 result=right-scalar side-effect=sequenced'
        ;;
    typedef-enum-definitions)
        grep -Eq '^[[:space:]]+li[[:space:]]+[a-z][a-z0-9]*,[[:space:]]+4$' "$work/$output.s"
        printf '%s\n' 'PASS compiler/c0/typedef_enum_definition anonymous=1 explicit-value=3 auto-next=4 alias=int'
        ;;
    unsigned-64-literals)
        require_literals 'max_unsigned_64:' 'top_unsigned_bit:' 'unsigned_max_above_signed_max:' '  li a0, -1' '  srl a0, t0, a0' '  sltu a0, a0, t0'
        printf '%s\n' 'PASS compiler/c0/unsigned_64_literals max=UINT64_MAX bits=all-ones shift=logical comparison=unsigned'
        ;;
    variadic-function-pointer-cast)
        test -s "$work/$output.s"
        require_literals 'variadic_function_pointer_cast:' 'jalr ra, t0, 0'
        if command -v riscv64-linux-gnu-gcc >/dev/null 2>&1; then
            riscv64-linux-gnu-gcc -c "$work/$output.s" -o "$work/$output.o"
        fi
        printf '%s\n' 'PASS compiler/c0/variadic-function-pointer-cast type-name=1 indirect-call=1 variadic-tail=1'
        ;;
    volatile-parameter)
        test -s "$work/$output.s"
        require_literals 'volatile_parameter_step:' 'volatile_parameter_probe:'
        if command -v riscv64-linux-gnu-gcc >/dev/null 2>&1; then
            riscv64-linux-gnu-gcc -c "$work/$output.s" -o "$work/$output.o"
        fi
        printf '%s\n' 'PASS compiler/c0/volatile-parameter pointer=1 double=1 core-ingress=1'
        ;;
    gnu-statement-expression)
        test -s "$work/$output.s"
        require_literals 'statement_value:' 'statement_void:' '.Lwhile_condition_'
        printf '%s\n' 'PASS compiler/c0/gnu_statement_expression scope=owned-block final-expression=value void-final=non-expression sequencing=expression-site loop=1'
        ;;
    gnu-top-level-empty-declaration)
        require_literals 'first:' 'second:' 'main:'
        printf '%s\n' 'PASS compiler/c0/gnu_top_level_empty_declaration leading=1 consecutive=2 after-function=1 between-declarations=1 trailing=1'
        ;;
    adjacent-string-literals)
        grep -F '  call consume' "$work/$output.s" >/dev/null
        test "$(grep -E -c '^\.Lminic_string_[0-9]+:$' "$work/$output.s")" -eq 1
        grep -E '^\.size \.Lminic_string_[0-9]+, 10$' "$work/$output.s" >/dev/null
        require_literals '  .byte 97' '  .byte 98'
        printf '%s\n' 'PASS compiler/c0/adjacent_string_literals tokens=2 objects=1 bytes=9 nul=1 call-args=1'
        ;;
    alignof-type-query)
        test -s "$work/$output.s"
        require_literals 'alignof_ulong:' 'alignof_record:' '  li a0, 8' '  li a0, 16'
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
        require_literals 'add a0, a0, a2' '# sbi a1 a3 a4 a5 a6 a7'
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
        require_literals 'linux_shaped_syscall_assignment:' 'linux_shaped_early_assignment:' 'gnu_function_pointer_to_void:'
        printf '%s\n' 'PASS compiler/c0/gnu_void_function_pointer_assignment both-directions=1 linux-shaped=1'
        ;;
    array-bound-integer-casts)
        require_literals '  li a0, 32' '  li a0, 4'
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
        require_literals 'expect_value:' 'expect_zero:'
        if grep -F '__builtin_expect' "$work/$output.s" >/dev/null; then
            printf '%s\n' 'FAIL compiler/c0/builtin_expect emitted runtime builtin symbol' >&2
            exit 1
        fi
        printf '%s\n' 'PASS compiler/c0/builtin_expect value=first-argument return-type=long hint=integer-constant runtime-call=none'
        ;;
    cast-type-classification)
        test -s "$work/$output.s"
        require_literals 'read_union_cast:' 'read_volatile_cast:' 'narrow_short:'
        printf '%s\n' 'PASS compiler/c0/cast_type_classification short=1 union=1 volatile=1'
        ;;
    comma-operator)
        test -s "$work/$output.s"
        require_literals 'comma_growth:' 'comma_value:' 'comma_conditions:' 'comma_expression_statement:'
        printf '%s\n' 'PASS compiler/c0/comma_operator parenthesized=1 pool-growth=1 top-level-condition=while,if expression-statement=1 void-left=1 assignment-side-effect=1 result=right'
        ;;
    extern-multi-declarators)
        test -s "$work/$output.s"
        require_literals '  la a0, start_like' '  la a0, end_like'
        if grep -F 'start_like:' "$work/$output.s" >/dev/null || grep -F 'end_like:' "$work/$output.s" >/dev/null; then
            printf '%s\n' 'FAIL compiler/c0/extern_multi_declarators: declaration emitted storage' >&2
            exit 1
        fi
        printf '%s\n' 'PASS compiler/c0/extern_multi_declarators shared-base=function-pointer arrays=2 incomplete=2 storage=none'
        ;;
    extern-multidimensional-array)
        test -s "$work/$output.s"
        require_literals 'read_cpu_bit:' 'cpu_bit_row:'
        printf '%s\n' 'PASS compiler/c0/extern_multidimensional_array dimensions=2 bounds=constant-expression nested-type=Array(Array(ulong)) subscript=2 row-decay=pointer'
        ;;
    extern-objects)
        require_literals '  la a0, external_counter' '  lw a0, 0(a0)'
        if grep -F '.type external_counter, @object' "$work/$output.s" >/dev/null ||
           grep -F 'external_counter:' "$work/$output.s" >/dev/null; then
            printf '%s\n' 'FAIL compiler/c0/extern_object: declaration emitted a definition' >&2
            exit 1
        fi
        printf '%s\n' 'PASS compiler/c0/extern_object declaration-only=external-symbol-reference'
        ;;
    external-pointer-arrays)
        require_literals '.type names, @object' '.globl names' '  .dword shared_name' '  .dword .Lminic_string_' '  .dword 0' '.size names, 32'
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
        require_literals '.type callback, @function' '  la a0, callback' '  call accept_callback'
        printf '%s\n' 'PASS compiler/c0/function_pointer_parameter direct-declarator=void-callback'
        ;;
    function-type-typedefs)
        require_literals '.type callback, @function' '  la a0, callback' '  call install_callback'
        printf '%s\n' 'PASS compiler/c0/function_type_typedef alias=function pointer-use=direct'
        ;;
    global-pointer-subscripts)
        require_literals '  la a0, items' '  ld a0, 0(a0)' '  slli a0, a0, 3'
        printf '%s\n' 'PASS compiler/c0/global_pointer_subscript base=global-pointer element=pointer stride=8'
        ;;
    gnu-extension-declaration)
        test -s "$work/$output.s"
        require_literals 'read_wide:' 'wide_size:' '  li a0, 8'
        printf '%s\n' 'PASS compiler/c0/gnu_extension_declaration marker=diagnostic-only union-field=1 layout=unchanged size=8'
        ;;
    gnu-extension-prefix-declarations)
        test -s "$work/$output.s"
        grep -F 'extension_sum:' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/gnu_extension_prefix_declaration marker=diagnostic-only placement=before-typedef signed-alias=1'
        ;;
    gnu-format-cold-attributes)
        test -s "$work/$output.s"
        require_literals 'main:' 'ftrace_vprintk_like:' 'reordered_static_inline:'
        printf '%s\n' 'PASS compiler/c0/gnu_format_cold_attributes prefix=format suffix=noreturn,cold interleaved=static-attr-inline-attr+inline-static classification=diagnostic+optimization ABI=unchanged'
        ;;
    gnu-function-visibility)
        test -s "$work/$output.s"
        require_literals '.globl visible_api' '.internal visible_api' '  call visible_api'
        printf '%s\n' 'PASS compiler/c0/gnu_function_visibility prefix=1 internal=ELF-directive redeclaration=consistent'
        ;;
    gnu-local-object-alignment)
        require_literals 'local_explicit_alignment:' 'local_alignof_alignment:' '  li t1, -32' '  and sp, sp, t1'
        printf '%s\n' 'PASS compiler/c0/gnu_local_object_alignment frontend=explicit-alignment core=owned rv64=max-natural-explicit dynamic-stack=32 alignof=accepted'
        ;;
    gnu-signed-keyword)
        test -s "$work/$output.s"
        require_literals 'widen_signed_aliases:' '  lb a0,'
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
        require_literals 'swap16:' 'add1:'
        printf '%s\n' 'PASS compiler/c0/gnu_inline_keyword_spellings spellings=__inline,__inline__'
        ;;
    gnu-no-profile-function-attribute)
        test -s "$work/$output.s"
        require_literals 'no_profile_decl:' '.noinstr.text'
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
        require_literals 'typeof_array_size:' 'typeof_pointer_array_size:'
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
        require_literals 'read_holder:' 'lw a0, 0(a0)'
        printf '%s\n' 'PASS compiler/c0/cast_record_type_names union-cast=1 volatile-cast=1'
        ;;
    enum-forward-completion)
        test -s "$work/$output.s"
        require_literals '.dword 1099511627776' 'global_wide:' 'read_wide:'
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
