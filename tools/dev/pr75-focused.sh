#!/bin/sh
set -eu

run_focused() {
    sh "tests/compiler/c0/$1"
}

for test_script in \
    run-signed-char-semantics.sh \
    run-extern-incomplete-arrays.sh \
    run-extern-fixed-integer-arrays.sh \
    run-external-pointer-arrays.sh \
    run-extern-parenthesized-functions.sh \
    run-anonymous-record-field-types.sh \
    run-typedef-enum-definitions.sh \
    run-enum-constant-expressions.sh \
    run-integer-constant-bitwise.sh \
    run-unsigned-64-literals.sh \
    run-builtin-va-list.sh \
    run-restrict-qualifiers.sh \
    run-gnu-function-attributes.sh \
    run-gnu-function-asm-labels.sh \
    run-gnu-function-visibility.sh \
    run-gnu-visible-extern-arrays.sh \
    run-gnu-aligned-record-fields.sh \
    run-record-forward-declarations.sh \
    run-record-multi-declarators.sh \
    run-volatile-qualifiers.sh \
    run-array-bound-sizeof.sh \
    run-array-bound-integer-casts.sh \
    run-record-multidimensional-arrays.sh \
    run-record-length-one-arrays.sh \
    run-pointer-compound-subtraction.sh \
    run-builtin-offsetof.sh \
    run-pointer-integer-casts.sh \
    run-record-assignment-expressions.sh \
    run-bitwise-and-assignment-expressions.sh \
    run-compound-assignment-full.sh \
    run-for-expression-initializers.sh \
    run-mixed-double-arithmetic.sh \
    run-conditional-pointer-qualifiers.sh \
    run-comma-operator.sh \
    run-top-level-comma-conditions.sh \
    run-cast-type-classification.sh \
    run-stack-fixed-arguments.sh \
    run-static-local-record-initializers.sh \
    run-static-record-arrays.sh \
    run-static-nested-record-initializers.sh \
    run-static-local-scalars.sh \
    run-static-local-inferred-arrays.sh \
    run-static-local-pointer-arrays.sh \
    run-static-local-fixed-arrays.sh \
    run-static-inferred-char-arrays.sh \
    run-prefix-update-expressions.sh \
    run-function-designator-calls.sh \
    run-function-address-expressions.sh
do
    run_focused "$test_script"
done
