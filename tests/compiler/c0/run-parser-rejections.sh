#!/bin/sh
# Parser/sema negative regressions: generate independent translation units
# from grammar families, never concatenate invalid programs into one input.
# Case IDs and diagnostic contracts are stable across the old file migration.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

emit_main() {
    printf '%s\nint main(void) {\n%s\n}\n' "$1" "$2"
}

generate() {
    name=$1
    case "$name" in
        invalid_address_of_rvalue|invalid_pointer_initializer)
            case "$name" in
                invalid_address_of_rvalue) init='&7'; expected='address-of requires an lvalue object or function designator' ;;
                *) init=7; expected='initializer type does not match local type' ;;
            esac
            emit_main '' "int *pointer = $init; return 0;"
            ;;
        invalid_duplicate_case|invalid_duplicate_default)
            case "$name" in
                invalid_duplicate_case) label='case 1'; expected='duplicate or overlapping case value range' ;;
                *) label=default; expected='duplicate default label' ;;
            esac
            emit_main '' "switch (1) { $label: break; $label: break; } return 0;"
            ;;
        invalid_case_outside_switch|invalid_default_outside_switch)
            case "$name" in
                invalid_case_outside_switch) label='case 1'; expected='case label requires an enclosing switch' ;;
                *) label=default; expected='default label requires an enclosing switch' ;;
            esac
            emit_main '' "$label: return 0;"
            ;;
        invalid_signed_unsigned|invalid_long_char)
            case "$name" in
                invalid_signed_unsigned) type='signed unsigned int'; expected='conflicting signed and unsigned type specifiers' ;;
                *) type='unsigned long char'; expected='char cannot be combined with short, int, or long' ;;
            esac
            emit_main '' "$type value = 0; return value;"
            ;;
        invalid_switch_double_selector|invalid_case_nonconstant)
            case "$name" in
                invalid_switch_double_selector) declaration='double value = 1.0'; label=1; expected='switch selector requires an integer expression' ;;
                *) declaration='int value = 1'; label=value; expected='case label currently requires an integer constant expression' ;;
            esac
            emit_main '' "$declaration; switch (value) { case $label: return 1; default: return 0; }"
            ;;
        invalid_duplicate_block_local)
            expected='duplicate local declaration'
            emit_main '' 'if (1) { int value = 1; int value = 2; return value; } return 0;'
            ;;
        invalid_out_of_scope_local)
            expected='use of undeclared local'
            emit_main '' 'if (1) { int value = 7; value = value + 1; } return value;'
            ;;
        invalid_dereference_integer)
            expected='dereference requires a pointer operand'
            emit_main '' 'int value = 7; return *value;'
            ;;
        invalid_assignment_rvalue)
            expected='assignment expression requires a modifiable object lvalue'
            emit_main '' 'int value = 1; (value + 1) = 3; return value;'
            ;;
        invalid_pointer_assignment_type)
            expected='assignment expression type does not match target type'
            emit_main '' 'int value = 1; int *pointer = &value; *pointer = pointer; return value;'
            ;;
        invalid_pointer_add_pointer)
            expected='unsupported pointer arithmetic operands'
            emit_main '' 'int left_value = 1; int right_value = 2; int *left = &left_value; int *right = &right_value; return *(left + right);'
            ;;
        invalid_integer_subtract_pointer)
            expected='unsupported pointer arithmetic operands'
            emit_main '' 'int value = 7; int *pointer = &value; return *(1 - pointer);'
            ;;
        invalid_zero_length_array)
            expected='array bound must not be negative'
            emit_main '' 'int values[-1]; return 0;'
            ;;
        invalid_array_initializer)
            expected='array initializers are not supported yet'
            emit_main '' 'int values[2] = 1; return 0;'
            ;;
        invalid_bare_array_use)
            expected='return expression does not match function return type'
            emit_main '' 'int values[2]; return values;'
            ;;
        invalid_array_index_type)
            expected='array index must have integer type'
            emit_main '' 'int values[2]; int *pointer; return values[pointer];'
            ;;
        invalid_duplicate_record_field)
            expected='duplicate record field'
            emit_main 'struct Pair { int value; int value; };' 'return 0;'
            ;;
        invalid_duplicate_record_definition)
            expected='duplicate record definition'
            emit_main 'struct Item { int first; }; struct Item { int second; };' 'return 0;'
            ;;
        invalid_conflicting_const_parameter)
            expected='conflicting function declaration'
            emit_main 'void inspect(const int *value); void inspect(int *value);' 'return 0;'
            ;;
        invalid_named_void_parameter)
            expected='parameter type cannot be bare void'
            emit_main 'void reset(void value);' 'return 0;'
            ;;
        invalid_duplicate_typedef)
            expected='duplicate typedef name'
            emit_main 'typedef int value_t; typedef int value_t;' 'return 0;'
            ;;
        invalid_void_typedef)
            expected='local object cannot have void type'
            emit_main 'typedef void nothing_t;' 'nothing_t value; return 0;'
            ;;
        invalid_too_many_global_initializers)
            expected='too many nested static array initializers'
            emit_main 'static const int table[2] = {1, 2, 3};' 'return 0;'
            ;;
        invalid_flexible_array_not_last)
            expected='flexible array member must be the last record field'
            emit_main 'struct invalid_packet { int length; char payload[]; int trailing; };' 'return 0;'
            ;;
        invalid_union_flexible_array)
            expected='flexible array member is not allowed in union'
            emit_main 'union invalid_union { int value; char payload[]; };' 'return 0;'
            ;;
        invalid_call_assignment_target)
            expected='assignment expression requires a modifiable object lvalue'
            emit_main 'static int value(void) { return 1; }' 'value() = 3; return 0;'
            ;;
        invalid_expression_statement_semicolon)
            expected='expected '\'';'\'' after expression'
            emit_main 'static void touch(int *value) { *value = 1; }' 'int value; touch(&value)
return value;'
            ;;
        invalid_const_pointer_removal)
            expected='call argument type does not match declaration'
            emit_main 'int write_value(int *value) { *value = 2; return *value; }' 'const int value = 1; return write_value(&value);'
            ;;
        invalid_nested_const_pointer)
            expected='call argument type does not match declaration'
            emit_main 'int inspect_values(const int **values) { return 0; }' 'int value; int *pointer; value = 1; pointer = &value; return inspect_values(&pointer);'
            ;;
        invalid_function_linkage_conflict)
            expected='conflicting function declaration'
            emit_main 'int helper(void); static int helper(void);' 'return 0;'
            ;;
        invalid_void_return_value)
            expected='void function cannot return a value'
            emit_main 'static void touch(void) { return 1; }' 'return 0;'
            ;;
        invalid_int_return_without_value)
            expected='non-void function requires a return value'
            emit_main 'int value(void) { return; }' 'return 0;'
            ;;
        invalid_case_symbolic_pointer)
            expected='case label currently requires an integer constant expression'
            printf '%s\n' 'static int object; int probe(unsigned long value) { switch (value) { case (unsigned long)&object: return 1; default: return 0; } }'
            ;;
        invalid_fallthrough_outside_switch)
            expected='GNU fallthrough statement attribute requires an enclosing switch'
            printf '%s\n' 'int invalid_fallthrough_outside_switch(void) { __attribute__((__fallthrough__)); return 0; }'
            ;;
        invalid_statement_attribute_target)
            expected='GNU attribute is not valid on a statement'
            printf '%s\n' 'int invalid_statement_attribute_target(int value) { switch (value) { case 0: __attribute__((__always_inline__)); default: return value; } }'
            ;;
        *) echo "unknown parser rejection: $name" >&2; return 2 ;;
    esac
}

run_case() (
    name=$1
    expected=''
    if ! generate "$name" >"$work/$name.c"; then exit 2; fi
    if [ -z "$expected" ]; then echo "missing diagnostic: $name" >&2; exit 2; fi
    if ! "$host_cc" -E -P -x c "$work/$name.c" -o "$work/$name.i"; then exit 1; fi
    if "$minic" -S "$work/$name.i" -o "$work/$name.s" >"$work/$name.stdout" 2>"$work/$name.stderr"; then
        echo "FAIL compiler/c0/$name: compilation unexpectedly succeeded" >&2
        exit 1
    fi
    if ! grep -F "$expected" "$work/$name.stderr" >/dev/null; then
        echo "FAIL compiler/c0/$name: diagnostic mismatch: $expected" >&2
        cat "$work/$name.stderr" >&2
        exit 1
    fi
    printf '%s\n' "PASS compiler/c0/$name"
)

case "${1:-}" in
    --all)
        test "$#" -eq 1
        failed=0
        for name in \
            invalid_duplicate_block_local \
            invalid_out_of_scope_local \
            invalid_address_of_rvalue \
            invalid_dereference_integer \
            invalid_pointer_initializer \
            invalid_assignment_rvalue \
            invalid_pointer_assignment_type \
            invalid_pointer_add_pointer \
            invalid_integer_subtract_pointer \
            invalid_zero_length_array \
            invalid_array_initializer \
            invalid_bare_array_use \
            invalid_array_index_type \
            invalid_duplicate_record_field \
            invalid_duplicate_record_definition \
            invalid_conflicting_const_parameter \
            invalid_named_void_parameter \
            invalid_duplicate_typedef \
            invalid_void_typedef \
            invalid_too_many_global_initializers \
            invalid_flexible_array_not_last \
            invalid_union_flexible_array \
            invalid_call_assignment_target \
            invalid_expression_statement_semicolon \
            invalid_signed_unsigned \
            invalid_long_char \
            invalid_const_pointer_removal \
            invalid_nested_const_pointer \
            invalid_function_linkage_conflict \
            invalid_void_return_value \
            invalid_int_return_without_value \
            invalid_case_outside_switch \
            invalid_default_outside_switch \
            invalid_duplicate_case \
            invalid_duplicate_default \
            invalid_switch_double_selector \
            invalid_case_nonconstant \
            invalid_case_symbolic_pointer \
            invalid_fallthrough_outside_switch \
            invalid_statement_attribute_target; do
            run_case "$name" || failed=1
        done
        test "$failed" -eq 0
        printf '%s\n' 'PARSER_REJECTION_MATRIX=PASS cases=40'
        ;;
    --emit)
        test "$#" -eq 2
        generate "$2"
        ;;
    *)
        test "$#" -eq 1
        run_case "$1"
        ;;
esac
