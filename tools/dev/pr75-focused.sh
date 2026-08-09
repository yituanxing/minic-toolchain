#!/bin/sh
set -eu

run_focused() {
    sh "tests/compiler/c0/$1"
}

for test_script in \
    run-signed-char-semantics.sh \
    run-extern-incomplete-arrays.sh \
    run-extern-fixed-integer-arrays.sh \
    run-extern-parenthesized-functions.sh \
    run-anonymous-record-field-types.sh \
    run-typedef-enum-definitions.sh \
    run-enum-constant-expressions.sh \
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
    run-mixed-double-arithmetic.sh \
    run-conditional-pointer-qualifiers.sh \
    run-comma-operator.sh \
    run-cast-type-classification.sh \
    run-stack-fixed-arguments.sh \
    run-static-local-record-initializers.sh \
    run-static-local-scalars.sh \
    run-static-local-inferred-arrays.sh \
    run-static-inferred-char-arrays.sh \
    run-prefix-update-expressions.sh \
    run-function-designator-calls.sh \
    run-function-address-expressions.sh
do
    run_focused "$test_script"
done
