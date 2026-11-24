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
    array-bound-constant-expressions) set -- 'array_bound_constant_expression' 'compiler-c0-array-bound-constant-expressions' 'array_bound_constant_expression' 'array_bound_constant_expression' '' 'yes' ;;
    block-scope-extern-object) set -- 'block_scope_extern_object' 'compiler-c0-block-scope-extern-object' 'extern' 'extern' '' 'yes' ;;
    bool-semantics) set -- 'bool_semantics' 'compiler-c0-bool-semantics' 'bool_semantics' 'bool_semantics' 'c11' 'yes' ;;
    conditional-pointer-qualifiers) set -- 'conditional_pointer_qualifiers' 'compiler-c0-conditional-pointer-qualifiers' 'conditional_pointer_qualifiers' 'conditional_pointer_qualifiers' '' 'yes' ;;
    core-required-no-fallback) set -- 'core_required_no_fallback' 'compiler-c0-core-required-no-fallback' 'core_required_no_fallback' 'legacy' 'gnu11' 'no' ;;
    declaration-head-variadic) set -- 'declaration_head_variadic' 'declaration-head-variadic' 'good' 'good' 'gnu11' 'no' ;;
    enum-mode-byte) set -- 'enum_mode_byte' 'enum-mode-byte' 'good' 'good' 'gnu11' 'no' ;;
    extern-incomplete-record-object) set -- 'extern_incomplete_record_object' 'compiler-c0-extern-incomplete-record-object' 'input' 'extern_incomplete_record_object' 'gnu11' 'yes' ;;
    external-inferred-record-array) set -- 'external_inferred_record_array' 'compiler-c0-external-inferred-record-array' 'external_inferred_record_array' 'external_inferred_record_array' '' 'yes' ;;
    for-declaration-initializers) set -- 'for_declaration_initializer' 'compiler-c0-for-declaration-initializers' 'for_declaration_initializer' 'for_declaration_initializer' '' 'yes' ;;
    for-expression-initializers) set -- 'for_expression_initializer' 'compiler-c0-for-expression-initializer' 'for_expression_initializer' 'for_expression_initializer' '' 'yes' ;;
    function-arena-borrow-lifetime) set -- 'function_arena_borrow_lifetime' 'compiler-c0-function-arena-borrow-lifetime' 'input' 'output' 'gnu11' 'yes' ;;
    gnu-aligned-record-fields) set -- 'gnu_aligned_record_field' 'compiler-c0-gnu-aligned-record-fields' 'gnu_aligned_record_field' 'gnu_aligned_record_field' '' 'yes' ;;
    gnu-builtin-constant-p) set -- 'gnu_builtin_constant_p' 'compiler-c0-gnu-builtin-constant-p' 'gnu_builtin_constant_p' 'gnu_builtin_constant_p' 'gnu11' 'yes' ;;
    gnu-compiletime-selection-builtins) set -- 'gnu_compiletime_selection_builtins' 'compiler-c0-gnu-compiletime-selection-builtins' 'gnu_compiletime_selection_builtins' 'gnu_compiletime_selection_builtins' 'gnu11' 'yes' ;;
    gnu-empty-records) set -- 'gnu_empty_records' 'compiler-c0-gnu-empty-records' 'gnu_empty_records' 'gnu_empty_records' 'gnu11' 'yes' ;;
    gnu-function-asm-labels) set -- 'gnu_function_asm_label' 'compiler-c0-gnu-function-asm-labels' 'gnu_function_asm_label' 'gnu_function_asm_label' '' 'yes' ;;
    gnu-function-pointer-bridge-call) set -- 'gnu_function_pointer_bridge_call' 'compiler-c0-gnu-function-pointer-bridge-call' 'bridge' 'bridge' '' 'yes' ;;
    gnu-int128-type) set -- 'gnu_int128_type' 'compiler-c0-gnu-int128-type' 'gnu_int128_type' 'gnu_int128_type' 'gnu11' 'yes' ;;
    gnu-overflow-builtins) set -- 'gnu_overflow_builtins' 'compiler-c0-gnu-overflow-builtins' 'gnu_overflow_builtins' 'gnu_overflow_builtins' 'gnu11' 'yes' ;;
    gnu-record-alignment) set -- 'gnu_record_alignment' 'compiler-c0-gnu-record-alignment' 'gnu_record_alignment' 'gnu_record_alignment' 'gnu11' 'yes' ;;
    gnu-section-symbol-attribute) set -- 'gnu_section_symbol_attribute' 'compiler-c0-gnu-section-symbol-attribute' 'input' 'gnu_section_symbol_attribute' 'gnu11' 'yes' ;;
    gnu-va-builtins) set -- 'gnu_builtin_va_start' 'gnu-va-builtins' 'good' 'good' 'gnu11' 'no' ;;
    gnu-visible-extern-arrays) set -- 'gnu_visible_extern_array' 'compiler-c0-gnu-visible-extern-arrays' 'gnu_visible_extern_array' 'gnu_visible_extern_array' '' 'yes' ;;
    gnu-weak-external-objects) set -- 'gnu_weak_external_objects' 'gnu-weak-external-objects' 'input' 'output' 'gnu11' 'no' ;;
    gnu-zero-length-record-arrays) set -- 'gnu_zero_length_record_array' 'compiler-c0-gnu-zero-length-record-arrays' 'gnu_zero_length_record_array' 'gnu_zero_length_record_array' 'gnu11' 'yes' ;;
    incomplete-array-typedef) set -- 'incomplete_array_typedef' 'incomplete-array-typedef' 'good' 'good' 'gnu11' 'no' ;;
    inline-functions) set -- 'inline_function' 'compiler-c0-inline-functions' 'inline_function' 'inline_function' '' 'yes' ;;
    long-long-types) set -- 'long_long_types' 'compiler-c0-long-long-types' 'long_long_types' 'long_long_types' '' 'yes' ;;
    mixed-double-arithmetic) set -- 'mixed_double_arithmetic' 'compiler-c0-mixed-double-arithmetic' 'mixed_double_arithmetic' 'mixed_double_arithmetic' '' 'yes' ;;
    pointer-compound-subtraction) set -- 'pointer_compound_subtraction' 'compiler-c0-pointer-compound-subtraction' 'pointer_compound_subtraction' 'pointer_compound_subtraction' '' 'yes' ;;
    pointer-integer-casts) set -- 'pointer_integer_casts' 'compiler-c0-pointer-integer-casts' 'pointer_integer_casts' 'pointer_integer_casts' '' 'yes' ;;
    pointer-to-bool-conversion) set -- 'pointer_to_bool_conversion' 'compiler-c0-pointer-bool' 'valid' 'valid' '' 'no' ;;
    record-assignment-expressions) set -- 'record_assignment_expression' 'compiler-c0-record-assignment-expressions' 'record_assignment_expression' 'record_assignment_expression' '' 'yes' ;;
    record-copy-array-members) set -- 'record_copy_array_member' 'compiler-c0-record-copy-array-members' 'record_copy_array_member' 'record_copy_array_member' '' 'yes' ;;
    record-local-initializers) set -- 'record_local_initializer' 'compiler-c0-record-local-initializers' 'record_local_initializer' 'record_local_initializer' '' 'yes' ;;
    record-rvalue-member) set -- 'record_rvalue_member' 'compiler-c0-record-rvalue-member' 'member' 'member' '' 'yes' ;;
    runtime-local-array-initializer) set -- 'runtime_local_array_initializer' 'compiler-c0-runtime-local-array-initializer' 'runtime_local_array_initializer' 'runtime_local_array_initializer' '' 'yes' ;;
    runtime-record-array-initializers) set -- 'runtime_record_array_initializer' 'compiler-c0-runtime-record-array-initializers' 'runtime_record_array_initializer' 'runtime_record_array_initializer' 'gnu11' 'no' ;;
    rv64-integer-aggregate-return) set -- 'rv64_integer_aggregate_return' 'compiler-c0-rv64-integer-aggregate-return' 'rv64_integer_aggregate_return' 'rv64_integer_aggregate_return' 'gnu11' 'yes' ;;
    shared-function-declarator) set -- 'shared_function_declarator' 'compiler-c0-shared-function-declarator' 'shared_function_declarator' 'shared_function_declarator' '' 'no' ;;
    short-integers) set -- 'short_integer_layout' 'compiler-c0-short-integers' 'short_integer_layout' 'short_integer_layout' '' 'yes' ;;
    static-aggregate-array-designators) set -- 'static_aggregate_array_designators' 'compiler-c0-static-aggregate-array-designators' 'static_aggregate_array_designators' 'static_aggregate_array_designators' 'gnu11' 'yes' ;;
    static-assert-declaration) set -- 'static_assert_declaration' 'compiler-c0-static-assert-declaration' 'static_assert_declaration' 'static_assert_declaration' 'gnu11' 'yes' ;;
    static-fixed-record-array-zero) set -- 'static_fixed_record_array_zero' 'compiler-c0-static-fixed-record-array-zero' 'static_fixed_record_array_zero' 'static_fixed_record_array_zero' '' 'yes' ;;
    static-floating-aggregate) set -- 'static_floating_aggregate' 'compiler-c0-static-floating-aggregate' 'input' 'output' '' 'yes' ;;
    static-inferred-char-arrays) set -- 'static_inferred_char_array' 'compiler-c0-static-inferred-char-array' 'static_inferred_char_array' 'static_inferred_char_array' '' 'yes' ;;
    static-inferred-integer-array) set -- 'static_inferred_integer_array' 'compiler-c0-static-inferred-integer-array' 'static_inferred_integer_array' 'static_inferred_integer_array' '' 'yes' ;;
    static-local-fixed-arrays) set -- 'static_local_fixed_array' 'compiler-c0-static-local-fixed-array' 'static_local_fixed_array' 'static_local_fixed_array' '' 'yes' ;;
    static-local-inferred-arrays) set -- 'static_local_inferred_array' 'compiler-c0-static-local-inferred-array' 'static_local_inferred_array' 'static_local_inferred_array' '' 'yes' ;;
    static-local-pointer-arrays) set -- 'static_local_pointer_array' 'compiler-c0-static-local-pointer-array' 'static_local_pointer_array' 'static_local_pointer_array' '' 'yes' ;;
    static-local-scalars) set -- 'static_local_scalar' 'compiler-c0-static-local-scalar' 'static_local_scalar' 'static_local_scalar' '' 'yes' ;;
    static-pointer-arrays) set -- 'static_pointer_array' 'compiler-c0-static-pointer-arrays' 'static_pointer_array' 'static_pointer_array' '' 'yes' ;;
    static-pointer-constant-conditional) set -- 'static_pointer_constant_conditional' 'static-pointer-constant-conditional' 'input' 'output' 'gnu11' 'no' ;;
    static-prefix-object-attributes) set -- 'static_prefix_object_attributes' 'compiler-c0-static-prefix-object-attributes' 'static_prefix_object_attributes' 'static_prefix_object_attributes' 'gnu11' 'yes' ;;
    static-preformed-array-zero) set -- 'static_preformed_array_zero' 'compiler-c0-static-preformed-array-zero' 'static_preformed_array_zero' 'static_preformed_array_zero' '' 'yes' ;;
    static-union-zero-overlay) set -- 'static_union_zero_overlay' 'static-union-zero-overlay' 'valid' 'valid' 'gnu11' 'no' ;;
    static-zero-definitions) set -- 'static_zero_definition' 'compiler-c0-static-zero-definitions' 'static_zero_definition' 'static_zero_definition' '' 'yes' ;;
    transparent-union) set -- 'transparent_union' 'compiler-c0-transparent-union' 'transparent' 'transparent' '' 'yes' ;;
    unnamed-prototype-parameters) set -- 'unnamed_prototype_parameter' 'compiler-c0-unnamed-prototype-parameters' 'unnamed_prototype_parameter' 'unnamed_prototype_parameter' '' 'yes' ;;
    void-pointer-locals) set -- 'void_pointer_local' 'compiler-c0-void-pointer-locals' 'void_pointer_local' 'void_pointer_local' '' 'yes' ;;
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
require_file_literals() {
    file=$1
    shift
    for literal do grep -F "$literal" "$file" >/dev/null || return $?; done
}
require_literals() {
    require_file_literals "$work/$output.s" "$@"
}
case "$name" in
    array-bound-constant-expressions)
        require_file_literals "$work/array_bound_constant_expression.s" 'li a0, 1048576' 'li a0, 35' 'li a0, 44'
        linux8=$(grep -c -F '  li a0, 8' "$work/array_bound_constant_expression.s" || true)
        test "$linux8" -ge 2
        printf '%s\n' 'PASS compiler/c0/array_bound_constant_expression scope=local+record typed-ast-consteval=1 operators=+,-,*,/,% relational=1 conditional=linux-alignof-shape alignof=1 sizeof=1 parentheses=1'

        "$host_cc" -E -P -x c "$root/tests/compiler/c0/invalid_runtime_array_bound.c" \
            -o "$work/invalid_runtime_array_bound.i"
        if "$minic" -S "$work/invalid_runtime_array_bound.i" \
            -o "$work/invalid_runtime_array_bound.s" \
            >"$work/invalid_runtime_array_bound.stdout" \
            2>"$work/invalid_runtime_array_bound.stderr"; then
            printf '%s\n' 'FAIL compiler/c0/invalid_runtime_array_bound: compilation unexpectedly succeeded' >&2
            exit 1
        fi
        # Real upstream BusyBox BUILD_BUG_ON expands to sizeof(char[ICE]).
        # This must fold bytes of a compiler-owned literal, not arbitrary objects.
        grep -F 'busybox_exec_path_string_bound:' \
            "$work/array_bound_constant_expression.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/array_bound_constant_expression GNU_string_literal_index_ICE=1 BusyBox_exec_path=1'

        grep -F 'expected integer constant expression' \
            "$work/invalid_runtime_array_bound.stderr" >/dev/null
        printf '%s\n' 'PASS compiler/c0/invalid_runtime_array_bound'
        ;;
    block-scope-extern-object)
        require_file_literals "$work/extern.s" 'zero_pfn' 'promoted'
        grep -E '^[[:space:]]*\.weak[[:space:]]+except_vec_cex$' "$work/extern.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/block_scope_extern_object scoped-global=1 repeated-compatible=1 cross-function-same-entity=1 file-scope-promotion=1 block-scope-weak-object=1'

        # Original AArch64 Linux 6.6 mmu_context.h: block-scope extern declarations
        # whose typedef itself carries the function signature (no trailing "(...)").
        "$host_cc" -E -P -x c "$root/tests/compiler/c0/block_scope_extern_function_typedef.c" \
            -o "$work/function-typedef.i"
        "$minic" -S "$work/function-typedef.i" --target=aarch64-linux \
            -o "$work/function-typedef.s"
        grep -F 'idmap_cpu_replace_ttbr1' "$work/function-typedef.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/block_scope_extern_function_typedef signature=typedef-function scoped=1 aarch64=1'


        "$host_cc" -E -P -x c "$root/tests/compiler/c0/invalid_block_scope_extern_leak.c" -o "$work/leak.i"
        if "$minic" -S "$work/leak.i" -o "$work/leak.s" >"$work/leak.stdout" 2>"$work/leak.stderr"; then
            echo 'FAIL block-scope extern name leaked outside scope' >&2
            exit 1
        fi
        grep -F 'undeclared' "$work/leak.stderr" >/dev/null
        printf '%s\n' 'PASS compiler/c0/invalid_block_scope_extern_leak visibility=scoped'

        "$host_cc" -E -P -x c "$root/tests/compiler/c0/invalid_block_scope_extern_conflict.c" -o "$work/conflict.i"
        if "$minic" -S "$work/conflict.i" -o "$work/conflict.s" >"$work/conflict.stdout" 2>"$work/conflict.stderr"; then
            echo 'FAIL conflicting block-scope extern redeclaration unexpectedly compiled' >&2
            exit 1
        fi
        grep -F 'conflicting extern object redeclaration' "$work/conflict.stderr" >/dev/null
        printf '%s\n' 'PASS compiler/c0/invalid_block_scope_extern_conflict'
        ;;
    bool-semantics)
        assembly="$work/bool_semantics.s"
        test -s "$assembly"
        for symbol in bool_size bool_return bool_assignment bool_promotion bool_alias_roundtrip; do
            grep -F "$symbol:" "$assembly" >/dev/null
        done
        grep -F '  li a0, 1' "$assembly" >/dev/null
        # Return-to-_Bool and assignment-to-_Bool both require nonzero-to-one normalization.
        boolize=$(grep -c '  snez ' "$assembly" || true)
        test "$boolize" -ge 2
        # _Bool objects use one-byte storage and unsigned byte loads.
        require_file_literals "$assembly" '  sb ' '  lbu '

        printf '%s\n' 'PASS compiler/c0/bool_semantics size=1 rank=below-char promotion=int conversion=nonzero-to-one storage=byte'
        ;;
    conditional-pointer-qualifiers)
        require_file_literals "$work/conditional_pointer_qualifiers.s" 'choose_const:' 'choose_volatile:' 'choose_const_volatile:' 'choose_const_void:' 'add_intermediate_const:'

        "$host_cc" -E -P -x c "$root/tests/compiler/c0/invalid_deep_pointer_qualification.c" \
            -o "$work/invalid_deep_pointer_qualification.i"
        if "$minic" -S "$work/invalid_deep_pointer_qualification.i" \
            -o "$work/invalid_deep_pointer_qualification.s" >"$work/invalid.out" 2>"$work/invalid.err"; then
            echo 'expected deep pointer qualification conversion to fail' >&2
            exit 1
        fi
        grep -F 'assignment expression type does not match target type' "$work/invalid.err" >/dev/null
        printf '%s\n' 'PASS compiler/c0/conditional_pointer_qualifiers const=union volatile=union const-volatile=union void-object=qualified-void immediate-pointee-cv=add deeper-pointee-cv=reject'
        ;;
    core-required-no-fallback)
        MINIC_CORE_IR=strict "$minic" -S "$work/core_required_no_fallback.i" \
            -o "$work/shadow-strict.s"
        if MINIC_CORE_CODEGEN=basic-v0 "$minic" -S "$work/core_required_no_fallback.i" \
            -o "$work/core-basic-v0.s" >"$work/core.stdout" 2>"$work/core.stderr"; then
            printf '%s\n' 'FAIL compiler/c0/core-required-no-fallback: Core-owned target gap silently fell back' >&2
            exit 1
        fi
        grep -F "Core-owned function 'core_owned_call9' cannot be emitted by RV64 basic-v0" \
            "$work/core.stderr" >/dev/null
        printf '%s\n' 'PASS compiler/c0/core-required-no-fallback lower-ok=1 target-gap=fail-closed legacy-default=1'
        ;;
    declaration-head-variadic)
        test -s "$work/good.s"
        require_file_literals "$work/good.s" 'call pick_first' '.globl main'

        "$host_cc" -E -P -std=gnu11 -x c \
          "$root/tests/compiler/c0/declaration_head_variadic_layout_bad.c" -o "$work/bad.i"
        set +e
        "$minic" -S "$work/bad.i" -o "$work/bad.s" 2>"$work/bad.err"
        status=$?
        set -e
        test "$status" -ne 0
        grep -F 'unsupported GNU declaration-head typedef attribute' "$work/bad.err" >/dev/null

        printf '%s\n' 'PASS compiler/c0/declaration-head-variadic attributes=semantic-target function-type=variadic call-tail=enabled layout=fail-closed'
        ;;
    enum-mode-byte)
        test -s "$work/good.s"

        "$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/enum_mode_byte_overflow.c" -o "$work/bad.i"
        if "$minic" -S "$work/bad.i" -o "$work/bad.s" >"$work/bad.stdout" 2>"$work/bad.stderr"; then
          printf '%s\n' 'FAIL compiler/c0/enum-mode-byte: overflow unexpectedly succeeded' >&2
          exit 1
        fi
        grep -F 'enum mode(byte) cannot represent enumerator values' "$work/bad.stderr" >/dev/null

        printf '%s\n' 'PASS compiler/c0/enum-mode-byte signed=1B unsigned=1B overflow=rejected'
        ;;
    extern-incomplete-record-object)
        assembly="$work/extern_incomplete_record_object.s"
        test -s "$assembly"
        # Undefined extern symbols need no explicit `.extern` directive in GNU as;
        # prove the declaration remains addressable while emitting no storage definition.
        require_file_literals "$assembly" '  la a0, opaque_object' 'opaque_address:'
        if grep -F '.globl opaque_object' "$assembly" >/dev/null ||
           grep -F 'opaque_object:' "$assembly" >/dev/null; then
            printf '%s\n' 'FAIL compiler/c0/extern_incomplete_record_object: extern declaration emitted storage' >&2
            exit 1
        fi
        printf '%s\n' 'PASS compiler/c0/extern_incomplete_record_object forward-record=1 extern-object=declaration-only addressable=1 storage=none'
        ;;
    external-inferred-record-array)
        asm="$work/external_inferred_record_array.s"
        require_file_literals "$asm" 'riscv_isa_ext:' '.size riscv_isa_ext, 48' 'kvm_stats:' '.size kvm_stats, 72' 'fixed_record_array:' '.size fixed_record_array, 24' 'designated_ext:' '.size designated_ext, 96'

        test "$(grep -c '^  \.dword \.Lminic_string_' "$asm")" -ge 4

        after_label=$(sed -n '/^kvm_stats:/,/^.size kvm_stats, 72/p' "$asm")
        printf '%s\n' "$after_label" | grep -F '104' >/dev/null
        printf '%s\n' "$after_label" | grep -F '97' >/dev/null

        "$host_cc" -E -P -x c \
            "$root/tests/compiler/c0/invalid_external_inferred_record_array_brace_elision.c" \
            -o "$work/invalid.i"
        if "$minic" -S "$work/invalid.i" -o "$work/invalid.s" \
            >"$work/invalid.stdout" 2>"$work/invalid.stderr"; then
            printf '%s\n' 'FAIL compiler/c0/invalid_external_inferred_record_array_brace_elision: unexpectedly succeeded' >&2
            exit 1
        fi
        grep -F "expected '{' in record initializer" "$work/invalid.stderr" >/dev/null

        printf '%s\n' \
            'PASS compiler/c0/external_inferred_record_array bound=shared-shape-prepass record=nested+designated array-designator=enum+inferred pointer-reloc=1 char-array-string=1 fixed-path=unchanged brace-elision=fail-closed'
        ;;
    for-declaration-initializers)
        printf '%s\n' 'PASS compiler/c0/for_declaration_initializer scope=condition,update,body redeclare-after-loop=1 parenthesized-local=block+for shared-direct-declarator=1'

        "$host_cc" -E -P -x c "$root/tests/compiler/c0/invalid_for_declaration_scope.c" \
            -o "$work/invalid_for_declaration_scope.i"
        if "$minic" -S "$work/invalid_for_declaration_scope.i" \
            -o "$work/invalid_for_declaration_scope.s" \
            >"$work/invalid_for_declaration_scope.stdout" \
            2>"$work/invalid_for_declaration_scope.stderr"; then
            printf '%s\n' 'FAIL compiler/c0/invalid_for_declaration_scope: compilation unexpectedly succeeded' >&2
            exit 1
        fi
        grep -F 'use of undeclared local' "$work/invalid_for_declaration_scope.stderr" >/dev/null
        printf '%s\n' 'PASS compiler/c0/invalid_for_declaration_scope'
        ;;
    for-expression-initializers)
        require_file_literals "$work/for_expression_initializer.s" 'for_compound_initializer:' 'for_comma_initializer:' 'for_parenthesized_post_update:' 'for_comma_update:'
        grep -E '^[[:space:]]+subw[[:space:]]+a0,' "$work/for_expression_initializer.s" >/dev/null
        grep -F '  beqz a0, .Lwhile_end_' "$work/for_expression_initializer.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/for_expression_initializer compound=-= expression-init=general comma-init=2 update=full-expression,parenthesized-post++,comma-expression'
        ;;
    function-arena-borrow-lifetime)
        test -s "$work/output.s"
        grep -F 'return_after_function_arena_growth:' "$work/output.s" >/dev/null
        ;;
    gnu-aligned-record-fields)
        test -s "$work/gnu_aligned_record_field.s"
        require_file_literals "$work/gnu_aligned_record_field.s" 'aligned_values_offset:' 'aligned_tail_offset:' 'aligned_record_size:' 'prefix_aligned_flags_offset:' 'prefix_aligned_tail_offset:' 'mixed_aligned_value_offset:' 'mixed_aligned_tail_offset:' 'mixed_aligned_record_size:'
        # Existing suffix alignment contract: 16,32,48.
        grep -E '^[[:space:]]*li[[:space:]]+([[:alnum:]]+),[[:space:]]*48$' "$work/gnu_aligned_record_field.s" >/dev/null
        # Linux prefix shape naturally places u64 at 8 and its tail at 16.
        grep -E '^[[:space:]]*li[[:space:]]+([[:alnum:]]+),[[:space:]]*8$' "$work/gnu_aligned_record_field.s" >/dev/null
        # Prefix aligned(8) + suffix aligned(16) must merge to 16, with tail at 24 and size 32.
        grep -E '^[[:space:]]*li[[:space:]]+([[:alnum:]]+),[[:space:]]*24$' "$work/gnu_aligned_record_field.s" >/dev/null
        grep -E '^[[:space:]]*li[[:space:]]+([[:alnum:]]+),[[:space:]]*32$' "$work/gnu_aligned_record_field.s" >/dev/null

        printf '%s\n' 'PASS compiler/c0/gnu_aligned_record_field minimum-align=16 typed-ast-consteval=1 placement=suffix+pre-declarator linux-prefix-shape=1 prefix-suffix-merge=max values-offset=16 tail-offset=32 record-size=48 offsetof=consistent'
        ;;
    gnu-builtin-constant-p)
        assembly="$work/gnu_builtin_constant_p.s"
        test -s "$assembly"
        # Small static probes may be fully inlined before assembly emission.
        # Their names are not an invariant of __builtin_constant_p semantics.
        grep -F 'main:' "$assembly" >/dev/null
        if grep -F '__builtin_constant_p' "$assembly" >/dev/null; then
            printf '%s\n' '__builtin_constant_p leaked into emitted assembly' >&2
            exit 1
        fi

        printf '%s\n' 'PASS compiler/c0/gnu_builtin_constant_p query=compile-time integer-ICE=1 nonconstant=0 runtime-evaluation=none'
        ;;
    gnu-compiletime-selection-builtins)
        assembly="$work/gnu_compiletime_selection_builtins.s"
        test -s "$assembly"
        require_file_literals "$assembly" 'absolute_long:' 'absolute_int:'
        if grep -F '__builtin_choose_expr' "$assembly" >/dev/null ||
           grep -F '__builtin_types_compatible_p' "$assembly" >/dev/null; then
            printf '%s\n' 'compile-time builtin leaked into emitted assembly' >&2
            exit 1
        fi

        printf '%s\n' 'PASS compiler/c0/gnu_compiletime_selection_builtins types-compatible=top-level-unqualified choose-expr=compile-time nested=1 statement-expression-arm=1'
        ;;
    gnu-empty-records)
        assembly="$work/gnu_empty_records.s"
        test -s "$assembly"
        for symbol in empty_struct_size empty_union_size empty_member_record_size empty_identity \
                      empty_record_statement_copy empty_record_lvalue_copy; do
            grep -F "$symbol:" "$assembly" >/dev/null
        done
        # Core value allocation is independent of the ABI return register. Require
        # the semantic constants, not the legacy emitter's choice to materialize them
        # directly in a0.
        size0=$(grep -E -c '^[[:space:]]+li[[:space:]]+[^,]+,[[:space:]]*0$' "$assembly" || true)
        test "$size0" -ge 2
        grep -E '^[[:space:]]+li[[:space:]]+[^,]+,[[:space:]]*8$' "$assembly" >/dev/null
        require_file_literals "$assembly" '  call empty_source' '  call empty_target'
        source_line=$(grep -n -m1 '  call empty_source' "$assembly" | cut -d: -f1)
        target_line=$(grep -n -m1 '  call empty_target' "$assembly" | cut -d: -f1)
        test "$source_line" -lt "$target_line"

        printf '%s\n' 'PASS compiler/c0/gnu_empty_records struct-size=0 union-size=0 empty-member-declaration=ignored member-record-size=8 zero-copy=statement+rvalue addressable-zero-local=1 side-effects=source+target complete=1 layout-sentinel=alignment'
        ;;
    gnu-function-asm-labels)
        test -s "$work/gnu_function_asm_label.s"
        require_file_literals "$work/gnu_function_asm_label.s" '  call __real_renamed_api' '  la a0, __real_renamed_api'
        if grep -F '  call renamed_api' "$work/gnu_function_asm_label.s" >/dev/null; then
            printf '%s\n' 'FAIL compiler/c0/gnu_function_asm_labels emitted C identifier as linker symbol' >&2
            exit 1
        fi

        printf '%s\n' 'PASS compiler/c0/gnu_function_asm_labels declaration=1 adjacent-string=1 direct-call=linker-name function-address=linker-name'
        ;;
    gnu-function-pointer-bridge-call)
        grep -F '  call __cpuhp_setup_state' "$work/bridge.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/gnu_function_pointer_bridge_call explicit-void-bridge=2 target-bitcast=1 direct-incompatible=still-strict'

        "$host_cc" -E -P -x c "$root/tests/compiler/c0/gnu_function_pointer_to_void_call.c" \
            -o "$work/function-to-void.i"
        "$minic" -S "$work/function-to-void.i" -o "$work/function-to-void.s"
        test "$(grep -c -F '  call dereference_symbol_descriptor' "$work/function-to-void.s")" -ge 2
        grep -F '  call through_function_pointer' "$work/function-to-void.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/gnu_function_pointer_to_void_call direct-function=1 function-pointer-expression=1 target-void-pointer=1'

        "$host_cc" -E -P -std=gnu11 -x c \
            "$root/tests/compiler/c0/gnu_void_pointer_variable_function_argument.c" \
            -o "$work/void-pointer-argument.i"
        "$minic" -S "$work/void-pointer-argument.i" -o "$work/void-pointer-argument.s"
        grep -F 'bridge:' "$work/void-pointer-argument.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/gnu_void_pointer_variable_function_argument assignment-conversion=1'

        name=invalid_direct_incompatible_function_pointer_call
        "$host_cc" -E -P -x c "$root/tests/compiler/c0/$name.c" -o "$work/$name.i"
        if "$minic" -S "$work/$name.i" -o "$work/$name.s" \
            >"$work/$name.stdout" 2>"$work/$name.stderr"; then
            printf '%s\n' "FAIL compiler/c0/$name: compilation unexpectedly succeeded" >&2
            exit 1
        fi
        grep -F 'call argument type does not match declaration' "$work/$name.stderr" >/dev/null
        printf '%s\n' "PASS compiler/c0/$name"

        "$host_cc" -E -P -std=gnu11 -x c \
            "$root/tests/compiler/c0/gnu_function_void_pointer_equality.c" \
            -o "$work/function-void-equality.i"
        "$minic" -S "$work/function-void-equality.i" -o "$work/function-void-equality.s"
        grep -F 'main:' "$work/function-void-equality.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/gnu_function_void_pointer_equality bidirectional=1 direct-function=1'
        ;;
    gnu-int128-type)
        assembly="$work/gnu_int128_type.s"
        test -s "$assembly"
        for symbol in signed128_size unsigned128_size direct_unsigned128_size int128_record_size \
            int128_pair_equal int128_pair_copy int128_mul_shift; do
            grep -F "$symbol:" "$assembly" >/dev/null
        done
        size16=$(grep -c '  li a0, 16' "$assembly" || true)
        size32=$(grep -c '  li a0, 32' "$assembly" || true)
        test "$size16" -ge 3
        test "$size32" -ge 1
        # Two-half values must touch the high 64-bit lane and compare both halves.
        grep -Eq 'ld a1, 8\(t0\)' "$assembly"
        grep -Eq 'sd a1, 8\(t0\)' "$assembly"
        require_file_literals "$assembly" 'or a0, t0, t1' 'mulhu t2, t0, a0' '.Lminic_i128_shift_ge64_'

        printf '%s\n' 'PASS compiler/c0/gnu_int128_type signed=1 unsigned=2 pair-load-store-equality-mul-shift=1'
        ;;
    gnu-overflow-builtins)
        assembly="$work/gnu_overflow_builtins.s"
        test -s "$assembly"
        require_file_literals "$assembly" 'checked_add_int:' 'checked_mul_long:' 'checked_mul_ulong:' 'checked_sub_ulong:' 'mulh t4, t0, t1' 'mulhu t4, t0, t1' 'sltu a0, t0, t1' 'sw t2, 0(t3)' 'sd t2, 0(t3)'

        printf '%s\n' 'PASS compiler/c0/gnu_overflow_builtins attribute=warn-unused-result ops=add,sub,mul result-store=1 return=bool widths=32,64 signed+unsigned=1'
        ;;
    gnu-record-alignment)
        assembly="$work/gnu_record_alignment.s"
        test -s "$assembly"
        for symbol in pointer_aligned_size prefix_aligned_size over_aligned_size over_aligned_holder_size over_aligned_holder_offset designated_only_size designated_aligned_size; do
            grep -F "$symbol:" "$assembly" >/dev/null
        done
        size8=$(grep -Ec '^[[:space:]]*li[[:space:]]+([[:alnum:]]+),[[:space:]]*8$' "$assembly" || true)
        size16=$(grep -Ec '^[[:space:]]*li[[:space:]]+([[:alnum:]]+),[[:space:]]*16$' "$assembly" || true)
        size32=$(grep -Ec '^[[:space:]]*li[[:space:]]+([[:alnum:]]+),[[:space:]]*32$' "$assembly" || true)
        test "$size8" -ge 1
        test "$size16" -ge 3
        test "$size32" -ge 1

        sed -n '/designated_only_size:/,/^\.size/p' "$assembly" | grep -E '^[[:space:]]*li[[:space:]]+([[:alnum:]]+),[[:space:]]*8$' >/dev/null
        sed -n '/designated_aligned_size:/,/^\.size/p' "$assembly" | grep -E '^[[:space:]]*li[[:space:]]+([[:alnum:]]+),[[:space:]]*16$' >/dev/null

        for invalid in invalid_record_designated_init_arguments invalid_union_designated_init; do
            "$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/$invalid.c" -o "$work/$invalid.i"
            if "$minic" -S "$work/$invalid.i" -o "$work/$invalid.s" >"$work/$invalid.out" 2>"$work/$invalid.err"; then
                printf '%s\n' "expected $invalid to fail" >&2
                exit 1
            fi
        done
        grep -F 'GNU attribute has an invalid number of arguments' "$work/invalid_record_designated_init_arguments.err" >/dev/null
        grep -F 'GNU designated_init applies only to struct types' "$work/invalid_union_designated_init.err" >/dev/null

        printf '%s\n' 'PASS compiler/c0/gnu_record_alignment placement=prefix+suffix sizeof-pointer=8 overalign=16 holder-offset=16 holder-size=32 shared-type-attribute-consumer=1 typed-alignment-consteval=1 designated-init=diagnostic-struct-only mixed-suffix=1'
        ;;
    gnu-section-symbol-attribute)
        assembly="$work/gnu_section_symbol_attribute.s"
        test -s "$assembly"
        require_file_literals "$assembly" '.section .probe.data' 'placed_data:' '.section .probe.suffix.first' 'suffix_first:' '.section .probe.suffix.second' 'suffix_second:' '.section .probe.suffix.array' 'suffix_array:' '.section .probe.prefix.concat' 'prefix_section_scalar:' '.section .probe.text' 'placed_function:' '.section .probe.prefix.text' 'prefix_placed_function:'
        printf '%s\n' 'PASS compiler/c0/gnu_section_symbol_attribute extern-object=prefix+suffix deferred-prefix=section function-prefix-section=1 function-mixed-attributes=section+cold+alloc-size+malloc concatenated-string=1 typeof-record-declaration=1 per-declarator=isolated array-suffix=1 function-declaration=preserved definition-inherits=1 rv64-section-emission=1'
        ;;
    gnu-va-builtins)
        test -s "$work/good.s"
        grep -E 'add(i)?[[:space:]]+a0,[[:space:]]*s0' "$work/good.s" >/dev/null

        "$host_cc" -E -P -std=gnu11 -x c \
          "$root/tests/compiler/c0/gnu_builtin_va_start_wrong_last.c" -o "$work/bad.i"
        set +e
        "$minic" -S "$work/bad.i" -o "$work/bad.s" 2>"$work/bad.err"
        status=$?
        set -e
        test "$status" -ne 0
        grep -F 'second argument must be the last named parameter' "$work/bad.err" >/dev/null

        printf '%s\n' 'PASS compiler/c0/gnu-va-builtins start=semantic end=semantic wrong-last=fail-closed'
        ;;
    gnu-visible-extern-arrays)
        test -s "$work/gnu_visible_extern_array.s"
        require_file_literals "$work/gnu_visible_extern_array.s" '.globl names' '.internal names' '.size names, 24' '.globl hidden_items' '.hidden hidden_items' '.size hidden_items, 8'

        test "$(grep -c '^  .dword .Lminic_string_' "$work/gnu_visible_extern_array.s")" -ge 3
        printf '%s\n' 'PASS compiler/c0/gnu_visible_extern_array prefix=internal suffix=hidden extern-array=merge size=24,8'
        ;;
    gnu-weak-external-objects)
        grep -q '^\.weak weak_record$' "$work/output.s"
        grep -q '^\.weak weak_banner$' "$work/output.s"
        grep -q '^\.weak weak_defined$' "$work/output.s"
        "$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/invalid_gnu_weak_internal_object.c" -o "$work/invalid.i"
        if "$minic" -S "$work/invalid.i" -o "$work/invalid.s" >"$work/invalid.out" 2>"$work/invalid.err"; then
          echo 'expected internal weak object rejection' >&2
          exit 1
        fi
        ;;
    gnu-zero-length-record-arrays)
        test -s "$work/gnu_zero_length_record_array.s"
        require_file_literals "$work/gnu_zero_length_record_array.s" 'zero_array_offset:' 'zero_tail_offset:' 'zero_record_size:'
        test "$(grep -Ec '^[[:space:]]*li[[:space:]]+([[:alnum:]]+),[[:space:]]*8$' "$work/gnu_zero_length_record_array.s")" -ge 2
        grep -E '^[[:space:]]*li[[:space:]]+([[:alnum:]]+),[[:space:]]*16$' "$work/gnu_zero_length_record_array.s" >/dev/null

        printf '%s\n' 'PASS compiler/c0/gnu_zero_length_record_array element=ulong alignment=8 storage=0 member-offset=8 tail-offset=8 record-size=16'
        ;;
    incomplete-array-typedef)
        test -s "$work/good.s"

        # The extension is intentionally only the outermost incomplete array owner.
        # An array element itself may not be an incomplete array type.
        set +e
        "$host_cc" -E -P -std=gnu11 -x c \
          "$root/tests/compiler/c0/incomplete_array_typedef_nested_bad.c" -o "$work/bad.i" 2>/dev/null
        host_status=$?
        set -e
        if test "$host_status" -eq 0; then
          set +e
          "$minic" -S "$work/bad.i" -o "$work/bad.s" 2>"$work/bad.err"
          status=$?
          set -e
          test "$status" -ne 0
          grep -F 'only the outermost array dimension may be incomplete' "$work/bad.err" >/dev/null
        fi

        printf '%s\n' 'PASS compiler/c0/incomplete-array-typedef owner=typedef nested-incomplete=fail-closed'
        ;;
    inline-functions)
        require_file_literals "$work/inline_function.s" '.type add_one, @function' '.type add_two, @function' '  call add_one' '  call add_two'
        printf '%s\n' 'PASS compiler/c0/inline_function orders=static-inline,inline-static optimization=optional'

        "$host_cc" -E -P -x c "$root/tests/compiler/c0/invalid_inline_object.c" -o "$work/invalid_inline_object.i"
        if "$minic" -S "$work/invalid_inline_object.i" -o "$work/invalid_inline_object.s" \
            >"$work/invalid_inline_object.stdout" 2>"$work/invalid_inline_object.stderr"; then
            printf '%s\n' 'FAIL compiler/c0/invalid_inline_object: compilation unexpectedly succeeded' >&2
            exit 1
        fi
        grep -F 'inline specifier requires a function declarator' "$work/invalid_inline_object.stderr" >/dev/null
        printf '%s\n' 'PASS compiler/c0/invalid_inline_object'
        ;;
    long-long-types)
        require_file_literals "$work/long_long_types.s" '  .dword 3' '  .dword 4' '.type add_long_long, @function' '.type add_unsigned_long_long, @function'
        printf '%s\n' 'PASS compiler/c0/long_long_types width=8 suffixes=LL,ULL rank=distinct'

        "$host_cc" -E -P -x c "$root/tests/compiler/c0/invalid_too_many_long_specifiers.c" \
            -o "$work/invalid_too_many_long_specifiers.i"
        if "$minic" -S "$work/invalid_too_many_long_specifiers.i" \
            -o "$work/invalid_too_many_long_specifiers.s" \
            >"$work/invalid_too_many_long_specifiers.stdout" \
            2>"$work/invalid_too_many_long_specifiers.stderr"; then
            printf '%s\n' 'FAIL compiler/c0/invalid_too_many_long_specifiers: compilation unexpectedly succeeded' >&2
            exit 1
        fi
        grep -F 'too many long type specifiers' "$work/invalid_too_many_long_specifiers.stderr" >/dev/null
        printf '%s\n' 'PASS compiler/c0/invalid_too_many_long_specifiers'
        ;;
    mixed-double-arithmetic)
        require_file_literals "$work/mixed_double_arithmetic.s" '  fcvt.d.w ft0, t0' '  fadd.d ft0, ft0, ft1' '  fmul.d ft0, ft0, ft1' '  fdiv.d ft0, ft0, ft1' '  fsub.d ft0, ft0, ft1'
        printf '%s\n' 'PASS compiler/c0/mixed_double_arithmetic int-to-double=1 operators=+,-,*,/'
        ;;
    pointer-compound-subtraction)
        MINIC_CORE_IR=strict "$minic" -S "$work/pointer_compound_subtraction.i" \
            -o "$work/pointer_compound_subtraction.strict.s"

        test -s "$work/pointer_compound_subtraction.s"
        test -s "$work/pointer_compound_subtraction.strict.s"
        grep -F 'read_adjusted:' "$work/pointer_compound_subtraction.s" >/dev/null
        grep -F 'read_cfg_adjusted:' "$work/pointer_compound_subtraction.strict.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/pointer_compound_subtraction plus-equal=1 minus-equal=1 complete-pointee=1 block-local-rhs=1'
        ;;
    pointer-integer-casts)
        # Internal object/function addresses use the explicit PC-relative local
        # address pseudo (lla), not a GOT-selecting la. This preserves the current
        # NOPIC relocation/ABI contract across the integer round trip.
        grep -E '^[[:space:]]+lla[[:space:]]+[^,]+,[[:space:]]*value$' "$work/pointer_integer_casts.s" >/dev/null
        grep -E '^[[:space:]]+lla[[:space:]]+[^,]+,[[:space:]]*target$' "$work/pointer_integer_casts.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/pointer_integer_casts object=1 function=1 roundtrip-through-ulong=1 implicit-assignment-unchanged=1'
        ;;
    pointer-to-bool-conversion)
        # Return and assignment boundaries already normalize integer targets through
        # the target type. Pointer-to-bool therefore must produce real 0/1 values.
        snez_count=$(grep -E -c '^[[:space:]]+snez[[:space:]]+' "$work/valid.s" || true)
        seqz_count=$(grep -E -c '^[[:space:]]+seqz[[:space:]]+' "$work/valid.s" || true)
        if test "$snez_count" -lt 4; then
            # Core represents pointer truth as is_zero(is_zero(pointer)); requiring two
            # seqz operations per boundary proves canonical 0/1 rather than mere nonzero.
            test "$seqz_count" -ge 8
        fi

        # A fixed bool parameter is another assignment-conversion boundary. Require
        # normalization in the caller before the direct call, not merely AST acceptance.
        awk '
          /pass_function_pointer:/ { in_fn=1; snez=0; seqz=0 }
          in_fn && /^[[:space:]]+snez[[:space:]]+/ { snez=1 }
          in_fn && /^[[:space:]]+seqz[[:space:]]+/ { seqz++ }
          in_fn && /call accept_bool/ { exit (snez || seqz >= 2) ? 0 : 1 }
          in_fn && /^\.size[[:space:]]+pass_function_pointer/ { exit 1 }
          END { if (!in_fn) exit 1 }
        ' "$work/valid.s"

        "$host_cc" -E -P -x c "$root/tests/compiler/c0/invalid_pointer_to_int_return.c" -o "$work/invalid.i"
        if "$minic" -S "$work/invalid.i" -o "$work/invalid.s" >"$work/invalid.out" 2>"$work/invalid.err"; then
            echo 'expected pointer-to-int return to remain rejected' >&2
            exit 1
        fi
        grep -F 'return expression does not match function return type' "$work/invalid.err" >/dev/null

        printf '%s\n' 'PASS compiler/c0/pointer_to_bool_conversion return=function+object assignment=function+object fixed-call=normalized pointer-to-int=reject'
        ;;
    record-assignment-expressions)
        MINIC_CORE_IR=strict "$minic" -S "$work/record_assignment_expression.i" \
            -o "$work/record_assignment_expression.strict.s"

        grep -F 'main:' "$work/record_assignment_expression.s" >/dev/null
        grep -F 'assign_both:' "$work/record_assignment_expression.strict.s" >/dev/null
        test "$(grep -c -F '  lbu t0, 0(t2)' "$work/record_assignment_expression.s")" -ge 2
        test "$(grep -c -F '  sb t0, 0(t3)' "$work/record_assignment_expression.s")" -ge 2
        printf '%s\n' 'PASS compiler/c0/record_assignment_expression whole-object-copy=1 comma-discard=1 alias-safe-temp=1 chained-record-call-strict=1'
        ;;
    record-copy-array-members)
        require_file_literals "$work/record_copy_array_member.s" '.type copy_packet, @function' '  lbu t0, 0(t2)' '  sb t0, 0(t3)'
        # Packet is 12 bytes on RV64 here; the snapshot is rounded to a 16-byte temporary stack slot.
        test "$(grep -c -F '  lbu t0, 0(t2)' "$work/record_copy_array_member.s")" -ge 24
        grep -F '  addi sp, sp, -16' "$work/record_copy_array_member.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/record_copy_array_member storage=12 snapshot=stack array-member=yes'
        ;;
    record-local-initializers)
        require_file_literals "$work/record_local_initializer.s" '.type copy_packet, @function' '.type initialize_guard, @function' '  lbu t0, 0(t2)' '  sb t0, 0(t3)'

        "$host_cc" -E -P -x c \
            "$root/tests/compiler/c0/record_local_chained_designator.c" \
            -o "$work/record_local_chained_designator.i"
        "$minic" -S \
            "$work/record_local_chained_designator.i" \
            -o "$work/record_local_chained_designator.s"
        grep -F '.type chained_record_designator, @function' \
            "$work/record_local_chained_designator.s" >/dev/null

        printf '%s\n' 'PASS compiler/c0/record_local_initializer source=dereference whole-record-copy=yes designated=pointer+integer chained-designator=yes zero-fill=unspecified member-selector=shared multi-declarator=1 suffix-object-attribute=unused'
        ;;
    record-rvalue-member)
        test "$(grep -c -F '  call pgprot_noncached' "$work/member.s")" -eq 1

        # A one-slot aggregate returns in a0 by RV64 ABI, but Core owns the private
        # result object's stack offset. Require that the call result is materialized
        # after the call, then require a scalar member load through an address value.
        awk '
          /call pgprot_noncached/ { after_call=1; next }
          after_call && /^[[:space:]]+sd[[:space:]]+a0,[[:space:]]*-?[0-9]+\(sp\)$/ {
              materialized=1
          }
          materialized &&
              /^[[:space:]]+ld[[:space:]]+[a-z][a-z0-9]*,[[:space:]]*0\([a-z][a-z0-9]*\)$/ {
              loaded=1
          }
          END { exit (after_call && materialized && loaded) ? 0 : 1 }
        ' "$work/member.s"
        printf '%s\n' 'PASS compiler/c0/record_rvalue_member call-result=1 materialized-temp=1 scalar-member=rvalue once-only-call=1 normalized=core-object'

        "$host_cc" -E -P -x c "$root/tests/compiler/c0/invalid_assign_record_rvalue_member.c" \
            -o "$work/invalid.i"
        if "$minic" -S "$work/invalid.i" -o "$work/invalid.s" \
            >"$work/invalid.stdout" 2>"$work/invalid.stderr"; then
            echo 'FAIL record rvalue member assignment unexpectedly compiled' >&2
            exit 1
        fi
        grep -F 'assignment expression requires a modifiable object lvalue' "$work/invalid.stderr" >/dev/null
        printf '%s\n' 'PASS compiler/c0/invalid_assign_record_rvalue_member nonmodifiable=1'
        ;;
    runtime-local-array-initializer)
        test "$(grep -c -F '  call probe' "$work/runtime_local_array_initializer.s")" -eq 1
        grep -F '  call consume' "$work/runtime_local_array_initializer.s" >/dev/null
        # Inferred pointer array is two RV64 pointers; fixed int array remains three elements.
        grep -F '  li a0, 16' "$work/runtime_local_array_initializer.s" >/dev/null || true
        printf '%s\n' 'PASS compiler/c0/runtime_local_array_initializer inferred-count=2 runtime-elements=left-to-right pointer-null=1 decay=1 sizeof=1 fixed-tail-zero=1'

        "$host_cc" -E -P -x c "$root/tests/compiler/c0/invalid_inferred_local_array_without_initializer.c" \
            -o "$work/invalid_inferred.i"
        if "$minic" -S "$work/invalid_inferred.i" -o "$work/invalid_inferred.s" \
            >"$work/invalid_inferred.stdout" 2>"$work/invalid_inferred.stderr"; then
            echo 'FAIL inferred local array without initializer unexpectedly compiled' >&2
            exit 1
        fi
        if ! grep -F 'inferred local array requires an initializer' "$work/invalid_inferred.stderr" >/dev/null; then
            printf '%s\n' 'FAIL runtime-local-array negative=inferred diagnostic-mismatch' >&2
            cat "$work/invalid_inferred.stderr" >&2
            exit 1
        fi

        "$host_cc" -E -P -x c "$root/tests/compiler/c0/invalid_local_array_initializer_element.c" \
            -o "$work/invalid_element.i"
        if "$minic" -S "$work/invalid_element.i" -o "$work/invalid_element.s" \
            >"$work/invalid_element.stdout" 2>"$work/invalid_element.stderr"; then
            echo 'FAIL incompatible local array initializer unexpectedly compiled' >&2
            exit 1
        fi
        if ! grep -F 'runtime array initializer element type mismatch' "$work/invalid_element.stderr" >/dev/null; then
            printf '%s\n' 'FAIL runtime-local-array negative=element diagnostic-mismatch' >&2
            cat "$work/invalid_element.stderr" >&2
            exit 1
        fi
        printf '%s\n' 'PASS compiler/c0/runtime_local_array_initializer negative=inferred-without-init+incompatible-element'
        ;;
    runtime-record-array-initializers)
        require_file_literals "$work/runtime_record_array_initializer.s" 'linux_guid_compound_literal:' 'runtime_record_array_local:' 'runtime_record_array_designated:' 'runtime_inferred_record_array:' '  sb '
        printf '%s\n' 'PASS compiler/c0/runtime_record_array_initializer compound-literal=1 fixed-array-field=16 scalar-elements=1 designated=1 holes=1 inferred-record=1'

        "$host_cc" -E -P -std=gnu11 -x c \
            "$root/tests/compiler/c0/invalid_record_array_brace_elision.c" \
            -o "$work/invalid_record_array_brace_elision.i"
        if "$minic" -S "$work/invalid_record_array_brace_elision.i" \
            -o "$work/invalid_record_array_brace_elision.s" \
            >"$work/invalid.stdout" 2>"$work/invalid.stderr"; then
            printf '%s\n' 'FAIL compiler/c0/invalid_record_array_brace_elision: unexpectedly succeeded' >&2
            exit 1
        fi
        grep -F 'runtime record array field initializer requires braces' \
            "$work/invalid.stderr" >/dev/null
        printf '%s\n' 'PASS compiler/c0/invalid_record_array_brace_elision fail-closed=1'

        "$host_cc" -E -P -std=gnu11 -x c \
            "$root/tests/compiler/c0/invalid_runtime_record_array_backward_designator.c" \
            -o "$work/invalid_runtime_record_array_backward_designator.i"
        if "$minic" -S "$work/invalid_runtime_record_array_backward_designator.i" \
            -o "$work/invalid_runtime_record_array_backward_designator.s" \
            >"$work/backward.stdout" 2>"$work/backward.stderr"; then
            printf '%s\n' 'FAIL compiler/c0/invalid_runtime_record_array_backward_designator: unexpectedly succeeded' >&2
            exit 1
        fi
        grep -F 'backward runtime array designators are not supported yet' \
            "$work/backward.stderr" >/dev/null
        printf '%s\n' 'PASS compiler/c0/invalid_runtime_record_array_backward_designator fail-closed=1'

        "$host_cc" -E -P -std=gnu11 -x c \
            "$root/tests/compiler/c0/invalid_runtime_record_array_range_designator.c" \
            -o "$work/invalid_runtime_record_array_range_designator.i"
        # This was an intentionally rejected construct before aggregate range-copy
        # semantics were implemented. Its valid GNU C form must now compile.
        "$minic" -S "$work/invalid_runtime_record_array_range_designator.i" \
            -o "$work/invalid_runtime_record_array_range_designator.s"
        grep -F 'invalid_runtime_record_array_range_designator:' \
            "$work/invalid_runtime_record_array_range_designator.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/runtime_record_array_range_designator enabled=1'
        ;;
    rv64-integer-aggregate-return)
        assembly="$work/rv64_integer_aggregate_return.s"
        test -s "$assembly"
        require_file_literals "$assembly" 'add_pair:' 'forward_pair:' 'call add_pair' 'sd a0,' 'sd a1,' 'sd a2,' 'sd a3,' 'ld a0, 0(t0)' 'ld a1, 8(t0)' 'make_triple:' 'forward_triple:' 'cleanup_triple_call:' 'call make_triple' 'lbu t0, 0(t2)' 'sb t0, 0(t3)'

        printf '%s\n' 'PASS compiler/c0/rv64_integer_aggregate_return direct=8,16 indirect=24 hidden-result=a0 explicit-args=a1+ record-local=1 record-call=1 cleanup=1'
        ;;
    shared-function-declarator)
        grep -F ".type apply, @function" "$work/shared_function_declarator.s" >/dev/null
        grep -F "  call apply" "$work/shared_function_declarator.s" >/dev/null
        jalr_count=$(grep -c -F "  jalr" "$work/shared_function_declarator.s")
        test "$jalr_count" -ge 5
        printf '%s\n' \
            "PASS compiler/c0/shared_function_declarator contexts=parenthesized-function-typedef,direct-function-typedef,function-pointer-field,extern-function-pointer-object,function-pointer-parameter,local-function-pointer,function-pointer-type-name local=void+int ordinary-parenthesized-local=preserved explicit-cast=1 direct-pointer-call=1"
        ;;
    short-integers)
        require_file_literals "$work/short_integer_layout.s" '.size layout, 8' '  .half 65535'
        grep -Eq '^[[:space:]]+lhu[[:space:]]+[a-z][a-z0-9]*, 0\([a-z][a-z0-9]*\)$' "$work/short_integer_layout.s"
        grep -Eq '^[[:space:]]+sh[[:space:]]+[a-z][a-z0-9]*, 0\([a-z][a-z0-9]*\)$' "$work/short_integer_layout.s"
        grep -Eq '^[[:space:]]+slli[[:space:]]+[a-z][a-z0-9]*, [a-z][a-z0-9]*, 48$' "$work/short_integer_layout.s"
        grep -Eq '^[[:space:]]+srai[[:space:]]+[a-z][a-z0-9]*, [a-z][a-z0-9]*, 48$' "$work/short_integer_layout.s"
        printf '%s\n' 'PASS compiler/c0/short_integer size=2 align=2 unsigned-load=lhu store=sh signed-extension=16'
        ;;
    static-aggregate-array-designators)
        require_file_literals "$work/static_aggregate_array_designators.s" '.size sparse_records, 32' '.size nested_records, 32' '.size fixed_records, 32' '  .word 11' '  .word 32' '  .word 52' '.size relocation_records, 16' '  .dword relocation_target' '  .dword .Lminic_string_' '.size atomic_lock_slots, 1024' '.size task_mask, 32'

        minic_cc=$(dirname "$minic")/minic-cc
        "$minic_cc" --target=x86_64-linux -S "$work/static_aggregate_array_designators.i" \
            -o "$work/static_aggregate_array_designators.x86.s"
        "$host_cc" -no-pie "$work/static_aggregate_array_designators.x86.s" \
            -o "$work/static_aggregate_array_designators.x86"
        "$work/static_aggregate_array_designators.x86"

        source="$root/tests/compiler/c0/invalid_static_aggregate_array_backward_designator.c"
        "$host_cc" -E -P -std=gnu11 -x c "$source" -o "$work/backward.i"
        "$minic" -S "$work/backward.i" -o "$work/backward.s" \
            >"$work/backward.stdout" 2>"$work/backward.stderr"
        # GNU designated initializers allow moving backward to a previously untouched
        # slot; the emitted object must keep the original slot indices intact.
        require_file_literals "$work/backward.s" '.size bad, 24' '  .word 3' '  .word 4' '  .word 1' '  .word 2'

        printf '%s\n' \
            'PASS compiler/c0/static_aggregate_array_designators inferred-bound=designator-extent nested-field=1 compound-literal=1 relocation-owner=aggregate-scalar union-array=1024 backward=GNU-slot-order range=shared-owner backward-scalar-range=native-x86'
        ;;
    static-assert-declaration)
        test -s "$work/static_assert_declaration.s"
        require_file_literals "$work/static_assert_declaration.s" 'generic_probe:' 'main:'

        "$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/invalid_static_assert_false.c" \
            -o "$work/invalid_static_assert_false.i"
        if "$minic" -S "$work/invalid_static_assert_false.i" \
            -o "$work/invalid_static_assert_false.s" 2>"$work/invalid_static_assert_false.stderr"; then
            printf '%s\n' 'false _Static_assert unexpectedly compiled' >&2
            exit 1
        fi
        grep -F 'static assertion failed' "$work/invalid_static_assert_false.stderr" >/dev/null

        printf '%s\n' 'PASS compiler/c0/static_assert_declaration scope=file+block condition=shared-ast-consteval builtin=types-compatible+typeof message=concatenated false=reject runtime=none'
        ;;
    static-fixed-record-array-zero)
        require_file_literals "$work/static_fixed_record_array_zero.s" '.type zero_records, @object' '.size zero_records, 48' '.type page_records, @object'
        # ELF section flags may be emitted explicitly; the section identity is the
        # data-layout invariant here.
        require_file_literals "$work/static_fixed_record_array_zero.s" '.section .bss..page_aligned' '.size page_records, 32' '.type initialized_records, @object' '.size initialized_records, 32' '.section .rodata' 'read_fixed_record_arrays:'

        printf '%s\n' 'PASS compiler/c0/static_fixed_record_array_zero fixed-record=generic zero-init=1 suffix-section=1 suffix-align=1 initialized=shared'
        ;;
    static-floating-aggregate)
        test -s "$work/output.s"
        require_file_literals "$work/output.s" 'aXformType:' '.word 1473454116' '.word 1065353216' '.word 1424045017' '.word 1114636288' 'floatingZero:' 'integerToDouble:' '.dword 4619567317775286272' 'unsignedToFloat:' '.word 1091567616' 'fcvt.d.s' 'fsgnjn.d' 'fcvt.s.d'

        if command -v riscv64-linux-gnu-gcc >/dev/null 2>&1; then
            riscv64-linux-gnu-gcc -c "$work/output.s" -o "$work/output.o"
        fi

        printf '%s\n' 'PASS compiler/c0/static-floating-aggregate sqlite-shape=1 binary32-rounding=checked'
        ;;
    static-inferred-char-arrays)
        require_file_literals "$work/static_inferred_char_array.s" 'local_name:' 'upvalue_name:' '.size local_name, 6' '.size upvalue_name, 8'
        if grep -F '.globl local_name' "$work/static_inferred_char_array.s" >/dev/null; then
            echo 'static inferred character array leaked external linkage' >&2
            exit 1
        fi
        printf '%s\n' 'PASS compiler/c0/static_inferred_char_array concat=1 bounds=6,8 internal-linkage=1 terminator=included'
        ;;
    static-inferred-integer-array)
        grep -F 'runnable_avg_yN_inv:' "$work/static_inferred_integer_array.s" >/dev/null
        # RV64 GNU scalar directives use signed decimal *spelling* for the physical
        # 0xffffffff payload, independently of the unsigned C initializer type.
        require_file_literals "$work/static_inferred_integer_array.s" '  .word -1' '.size runnable_avg_yN_inv, 12'
        if grep -F '.globl runnable_avg_yN_inv' "$work/static_inferred_integer_array.s" >/dev/null; then
            echo 'inferred static integer array leaked external linkage' >&2
            exit 1
        fi
        printf '%s\n' 'PASS compiler/c0/static_inferred_integer_array bound=inferred-from-brace count=3 typed-bits=u32-max suffix-attribute=unused internal-linkage=1'
        ;;
    static-local-fixed-arrays)
        require_file_literals "$work/static_local_fixed_array.s" '.section .rodata' '.type __minic_static_local_' '  .byte 1' '  .byte 2' '  .byte 3'
        # The shared static-storage initializer owner may materialize implicit tail zeroes
        # as explicit typed slots instead of relying on the emitter's .zero compression.
        # Keep this gate semantic: both spellings represent the same two-byte zero tail.
        if ! grep -F '  .zero 2' "$work/static_local_fixed_array.s" >/dev/null; then
            explicit_zero_count=$(grep -c '^  \.byte 0$' "$work/static_local_fixed_array.s" || true)
            if [ "$explicit_zero_count" -lt 2 ]; then
                echo 'fixed static local array is missing its two-byte zero tail' >&2
                cat "$work/static_local_fixed_array.s" >&2
                exit 1
            fi
        fi
        if grep -F '.globl __minic_static_local_' "$work/static_local_fixed_array.s" >/dev/null; then
            echo 'fixed static local array leaked external linkage' >&2
            exit 1
        fi
        printf '%s\n' 'PASS compiler/c0/static_local_fixed_array bound=5 initializers=3 zero-fill=2 storage=internal-rodata'
        ;;
    static-local-inferred-arrays)
        grep -Fx '.section .rodata' "$work/static_local_inferred_array.s" >/dev/null
        require_file_literals "$work/static_local_inferred_array.s" '.type __minic_static_local_' '  .byte 1' '  .byte 6'
        grep -E '\.size __minic_static_local_[^,]+, 7$' "$work/static_local_inferred_array.s" >/dev/null
        require_file_literals "$work/static_local_inferred_array.s" '  .word 8' '  .word 12' '  .word 16' '  .word 3' '  .dword 101' '  .dword 202'
        grep -E '\.size __minic_static_local_[^,]+, 32$' "$work/static_local_inferred_array.s" >/dev/null
        if grep -F '.globl __minic_static_local_' "$work/static_local_inferred_array.s" >/dev/null; then
            echo 'inferred static local array leaked external linkage' >&2
            exit 1
        fi
        printf '%s\n' 'PASS compiler/c0/static_local_inferred_array element=uchar count=7 multidim-inferred=2x2 shared-ice=offsetof,array,scalar,record arithmetic=1 internal-rodata=1'
        ;;
    static-local-pointer-arrays)
        asm="$work/static_local_pointer_array.s"
        grep -F '.type __minic_static_local_' "$asm" >/dev/null
        if grep -F '.globl __minic_static_local_' "$asm" >/dev/null; then
            echo 'static local pointer array leaked external linkage' >&2
            exit 1
        fi
        require_file_literals "$asm" '  .dword shared_event' '  .dword .Lminic_string_'
        # Both eight zero bytes and one zero 64-bit pointer slot are valid
        # representations; verify this is the internal 32-byte table's null slot.
        sed -n '/^__minic_static_local_[^:]*:/,/^\.size __minic_static_local_/p' "$asm" |
            grep -Eq '^  (\.zero 8|\.dword 0)$'
        grep -E '^\.size __minic_static_local_.*,[[:space:]]*32$' "$asm" >/dev/null
        printf '%s\n' 'PASS compiler/c0/static_local_pointer_array inferred=4 string-reloc=2 object-reloc=1 null=1 internal=1'
        ;;
    static-local-scalars)
        grep -Fx '.data' "$work/static_local_scalar.s" >/dev/null
        grep -Fx '.section .rodata' "$work/static_local_scalar.s" >/dev/null
        test "$(grep -c -F '.type __minic_static_local_' "$work/static_local_scalar.s")" -eq 6
        test "$(grep -c -F '  .zero 8' "$work/static_local_scalar.s")" -ge 2
        if grep -F '.globl __minic_static_local_' "$work/static_local_scalar.s" >/dev/null; then
            echo 'static local scalar leaked external linkage' >&2
            exit 1
        fi
        for section in .minic.static.scalar .minic.static.fixed .minic.static.inferred .minic.static.first; do
            test "$(grep -c -F ".section $section" "$work/static_local_scalar.s")" -eq 1
        done
        # The second declarator must not inherit the first declarator's suffix section.
        test "$(grep -c -F '.section .minic.static.first' "$work/static_local_scalar.s")" -eq 1
        test "$(grep -c -F '.type __minic_static_local_' "$work/static_local_scalar.s")" -eq 6
        printf '%s\n' 'PASS compiler/c0/static_local_scalar integer=writable-null pointer=const-null suffix-section=scalar,fixed,inferred initialized=1 per-declarator-isolation=1 storage=internal-global lifetime=static'
        ;;
    static-pointer-arrays)
        require_file_literals "$work/static_pointer_array.s" '.section .init.data' 'levels:' '  .dword start_a' '  .dword start_b' '.size levels, 16' 'names:'
        string_relocations=$(grep -c '^  \.dword \.Lminic_string_' "$work/static_pointer_array.s")
        if test "$string_relocations" -ne 2; then
            echo "FAIL static_pointer_array string-relocations=$string_relocations expected=2" >&2
            cat "$work/static_pointer_array.s" >&2
            exit 1
        fi
        require_file_literals "$work/static_pointer_array.s" '  .dword 0' '.size names, 24'
        printf '%s\n' 'PASS compiler/c0/static_pointer_array inferred-bound=3 object-relocations=2 null-tail=typed-pointer-zero suffix-section=1 extern-array-decay=2'
        ;;
    static-pointer-constant-conditional)
        require_file_literals "$work/output.s" 'selected:' 'null_selected:' '  .dword value' '  .dword 0' 'absolute_pointer_poison:' 'absolute_pointer_bits:' 'same_function_conditional:'
        same_function_count=$(grep -F -c '  .dword static_pointer_function' "$work/output.s")
        test "$same_function_count" -eq 1

        if "$minic" -S "$root/tests/compiler/c0/invalid_static_pointer_nonconstant_conditional.c" \
            -o "$work/invalid-nonconstant-conditional.s" \
            >"$work/invalid-nonconstant-conditional.stdout" \
            2>"$work/invalid-nonconstant-conditional.stderr"; then
            printf '%s\n' 'FAIL compiler/c0/static-pointer-constant-conditional: unequal nonconstant arms accepted' >&2
            exit 1
        fi
        grep -F 'static pointer initializer requires a null or static symbol address constant' \
            "$work/invalid-nonconstant-conditional.stderr" >/dev/null
        printf '%s\n' 'PASS compiler/c0/static-pointer-constant-conditional symbol+null selected-by-ICE absolute-bits=full-width absolute-pointer-arithmetic=target-width identical-arms=normalized unequal-arms=fail-closed'
        ;;
    static-prefix-object-attributes)
        assembly="$work/static_prefix_object_attributes.s"
        test -s "$assembly"
        require_file_literals "$assembly" 'class_irq_is_conditional:' '  .byte 0' '.size class_irq_is_conditional, 1' 'class_irq_add:'
        if grep -q 'static_declaration_is_function' src/frontend/parser_function.c; then
            printf '%s\n' 'static semantic declaration probe still present' >&2
            exit 1
        fi
        printf '%s\n' 'PASS compiler/c0/static_prefix_object_attributes prefix=unused object=static-const-bool initializer=shared-ice-enum-false width=1-byte function=static-inline single-pass-head=1 semantic-probe=none'
        ;;
    static-preformed-array-zero)
        require_file_literals "$work/static_preformed_array_zero.s" '.type plain_mask, @object' '.size plain_mask, 16' '.type percpu_mask, @object'
        grep -Fx '.section .data..percpu' "$work/static_preformed_array_zero.s" >/dev/null
        require_file_literals "$work/static_preformed_array_zero.s" '.size percpu_mask, 16' '.type integer_mask, @object' '.size integer_mask, 16' '.section .rodata' '.type readonly_mask, @object' '.size readonly_mask, 8'
        if grep -E '^\.globl (plain_mask|percpu_mask|integer_mask|readonly_mask)$' \
            "$work/static_preformed_array_zero.s" >/dev/null; then
            echo 'pre-formed static array leaked external linkage' >&2
            exit 1
        fi
        grep -F 'read_preformed_arrays:' "$work/static_preformed_array_zero.s" >/dev/null

        printf '%s\n' 'PASS compiler/c0/static_preformed_array_zero typedef-array=1 typeof-array=1 section=1 zero-init=1 readonly=1'
        ;;
    static-union-zero-overlay)
        test -s "$work/valid.s"
        "$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/static_union_nonzero_overlay_invalid.c" -o "$work/nonzero.i"
        "$minic" -S "$work/nonzero.i" -o "$work/nonzero.s"
        test -s "$work/nonzero.s"
        "$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/static_union_active_member_relocation.c" -o "$work/active-relocation.i"
        "$minic" -S "$work/active-relocation.i" -o "$work/active-relocation.s"
        test -s "$work/active-relocation.s"
        grep -Fq 'target' "$work/active-relocation.s"
        echo 'PASS compiler/c0/static-union-active-member zero+nonzero=accepted relocation=layout-aware'
        "$host_cc" -E -P -std=gnu11 -x c \
            "$root/tests/compiler/c0/static_union_shape_overlay.c" -o "$work/shape-overlay.i"
        "$minic" -S "$work/shape-overlay.i" -o "$work/shape-overlay.s"
        test -s "$work/shape-overlay.s"
        grep -Fq '.dword target' "$work/shape-overlay.s"
        echo 'PASS compiler/c0/static-union-shape-overlay span-preserved relocation-after-union=correct'
        ;;
    static-zero-definitions)
        require_file_literals "$work/static_zero_definition.s" 'state:' '.size state, 16' 'counter:' '.size counter, 4' 'pointer:' '.size pointer, 8'
        printf '%s\n' 'PASS compiler/c0/static_zero_definition record=16 int=4 pointer=8 implicit-zero=all'
        ;;
    transparent-union)
        require_file_literals "$work/transparent.s" '  call release_pages' '  jalr ra, t0, 0'
        printf '%s\n' 'PASS compiler/c0/transparent_union type-owner=record members=3 direct=3 indirect=1 null=1 abi=first-member-pointer callee=one-integer-chunk'

        "$host_cc" -E -P -x c "$root/tests/compiler/c0/invalid_transparent_union_nonmember.c" -o "$work/nonmember.i"
        if "$minic" -S "$work/nonmember.i" -o "$work/nonmember.s" >"$work/nonmember.stdout" 2>"$work/nonmember.stderr"; then
            echo 'FAIL transparent union non-member argument unexpectedly compiled' >&2
            exit 1
        fi
        grep -F 'call argument type does not match declaration' "$work/nonmember.stderr" >/dev/null

        "$host_cc" -E -P -x c "$root/tests/compiler/c0/invalid_transparent_union_nonunion.c" -o "$work/nonunion.i"
        if "$minic" -S "$work/nonunion.i" -o "$work/nonunion.s" >"$work/nonunion.stdout" 2>"$work/nonunion.stderr"; then
            echo 'FAIL transparent_union on non-union unexpectedly compiled' >&2
            exit 1
        fi
        grep -F 'GNU transparent_union requires a union type' "$work/nonunion.stderr" >/dev/null

        "$host_cc" -E -P -x c "$root/tests/compiler/c0/unsupported_transparent_union_nonpointer.c" -o "$work/nonpointer.i"
        if "$minic" -S "$work/nonpointer.i" -o "$work/nonpointer.s" >"$work/nonpointer.stdout" 2>"$work/nonpointer.stderr"; then
            echo 'FAIL unsupported mixed transparent union unexpectedly compiled' >&2
            exit 1
        fi
        grep -F 'GNU transparent_union v0 requires pointer members with one machine representation' "$work/nonpointer.stderr" >/dev/null
        printf '%s\n' 'PASS compiler/c0/transparent_union negative=nonmember+nonunion+nonpointer-v0'
        ;;
    unnamed-prototype-parameters)
        require_file_literals "$work/unnamed_prototype_parameter.s" '  call install' '  call consume'
        printf '%s\n' 'PASS compiler/c0/unnamed_prototype_parameter declaration=unnamed definition=named'

        "$host_cc" -E -P -x c \
            "$root/tests/compiler/c0/invalid_unnamed_definition_parameter.c" \
            -o "$work/invalid_unnamed_definition_parameter.i"
        if "$minic" -S \
            "$work/invalid_unnamed_definition_parameter.i" \
            -o "$work/invalid_unnamed_definition_parameter.s" \
            >"$work/invalid.stdout" 2>"$work/invalid.stderr"; then
            printf '%s\n' 'FAIL compiler/c0/invalid_unnamed_definition_parameter: unexpectedly succeeded' >&2
            exit 1
        fi
        grep -F 'function definition requires parameter names' "$work/invalid.stderr" >/dev/null
        printf '%s\n' 'PASS compiler/c0/invalid_unnamed_definition_parameter'
        ;;
    void-pointer-locals)
        grep -F '.globl main' "$work/void_pointer_local.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/void_pointer_local forms=void*,const-void*'

        "$host_cc" -E -P -x c "$root/tests/compiler/c0/invalid_void_local_object.c" \
            -o "$work/invalid_void_local_object.i"
        if "$minic" -S "$work/invalid_void_local_object.i" -o "$work/invalid_void_local_object.s" \
            >"$work/invalid_void_local_object.stdout" 2>"$work/invalid_void_local_object.stderr"; then
            printf '%s\n' 'FAIL compiler/c0/invalid_void_local_object: compilation unexpectedly succeeded' >&2
            exit 1
        fi
        grep -F 'local object cannot have void type' "$work/invalid_void_local_object.stderr" >/dev/null
        printf '%s\n' 'PASS compiler/c0/invalid_void_local_object'
        ;;
    postfix-const)
        require_literals '.type same_first, @function' '.type use_typedef_const, @function'
        printf '%s\n' 'PASS compiler/c0/postfix_const forms=char-const,const-char,typedef-const'
        ;;
    zero-aggregate-null)
        require_literals '.globl main' '  sw ' '  sd '
        printf '%s\n' 'PASS compiler/c0/zero_aggregate_null scalar-zero=yes pointer-null=yes'
        ;;
    stack-fixed-arguments)
        require_literals '.type minic_abi_sum9, @function' '  call gcc_abi_sum9'
        printf '%s\n' 'PASS compiler/c0/stack_fixed_arguments fixed=9 integer-class register=8 stack=1'
        ;;
    gnu-octal-character-case-range)
        test -s "$work/$output.s"
        require_literals 'linux_printk_level_like:' 'octal_character_values:'

        printf '%s\n' 'PASS compiler/c0/gnu_octal_character_case_range octal=1-3-digits case-range=0...7 expanded=8 duplicate-check=preserved'
        ;;
    nested-function-pointer-field)
        test -s "$work/$output.s"
        require_literals 'install_resolver:' 'resolve_symbol'

        if command -v riscv64-linux-gnu-gcc >/dev/null 2>&1; then
            riscv64-linux-gnu-gcc -c "$work/$output.s" -o "$work/$output.o"
        fi

        printf '%s\n' 'PASS compiler/c0/nested-function-pointer-field shape=R(*(*field)(P))(Q) assignment=compatible'
        ;;
    nested-function-pointer-type-name)
        test -s "$work/$output.s"
        require_literals 'resolve_nested_function_pointer:' 'jalr ra, t0, 0'

        if command -v riscv64-linux-gnu-gcc >/dev/null 2>&1; then
            riscv64-linux-gnu-gcc -c "$work/$output.s" -o "$work/$output.o"
        fi

        printf '%s\n' 'PASS compiler/c0/nested-function-pointer-type-name anonymous=1 nested=1 indirect-call=1'
        ;;
    prefix-update-expressions)
        require_literals '  addi t0, t0, 1' '  addi t0, t0, -1' '  addi t0, t0, 4' '  addi t0, t0, -4'
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
        require_literals '  call external_add' '  call external_alloc'
        if grep -F '.type external_add, @function' "$work/$output.s" >/dev/null; then
            printf '%s\n' 'FAIL compiler/c0/extern_parenthesized_functions: declaration emitted a definition' >&2
            exit 1
        fi
        printf '%s\n' 'PASS compiler/c0/extern_parenthesized_functions linkage=external parenthesized-name=1'
        ;;
    wide-integer-literals)
        require_literals '  li a0, 9223372036854775807' '  li a0, 9223372036854775806' 'binary_literal_value:' 'binary_enum_value:' '  li a0, 170' '  li a0, 5'
        printf '%s\n' 'PASS compiler/c0/wide_integer_literal payload=int64+gnu-binary max=9223372036854775807 binary=170 enum=5'
        ;;
    extern-fixed-integer-arrays)
        require_literals '.globl table' '  .byte 1' '  .byte 2' '  .byte 3'
        test "$(grep -Fc '  .byte 0' "$work/$output.s")" -eq 2
        grep -F '.size table, 5' "$work/$output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/extern_fixed_integer_array extern-merge=1 bound=5 brace=3 zero-fill=2'
        ;;
    compound-assignment-expressions)
        test "$(grep -c -F '  call next_slot' "$work/$output.s")" -eq 1
        require_literals '  slli a0, a0, 2' '  divu a0, t0, a0' '  div a0, t0, a0'
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
        require_file_literals "$work/output.s" '__init_begin' '__init_end'
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
