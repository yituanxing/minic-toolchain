#!/bin/sh
set -eu

run_focused() {
    case "$1" in
        compile:*) sh tests/compiler/c0/run-compile-only-case.sh "${1#compile:}" ;;
        strict:*) sh tests/compiler/c0/run-core-strict-runtime-case.sh "${1#strict:}" ;;
        native:*) sh tests/compiler/c0/run-native-matrix.sh "${1#native:}" ;;
        *) sh "tests/compiler/c0/$1" ;;
    esac
}

# Permanent focused semantic coverage accumulated during the Foundation line.
# Keep this runner named by its long-term ownership rather than by historical PRs.
for test_script in \
    compile:signed-char-semantics \
    run-extern-incomplete-arrays.sh \
    compile:extern-fixed-integer-arrays \
    compile:external-pointer-arrays \
    native:run-external-array-declarator-routing \
    compile:extern-parenthesized-functions \
    compile:anonymous-record-field-types \
    compile:typedef-enum-definitions \
    compile:enum-constant-expressions \
    native:run-enum-constant-scopes \
    compile:integer-constant-bitwise \
    compile:unsigned-64-literals \
    run-builtin-va-list.sh \
    native:run-aarch64-builtin-va-list-declaration \
    compile:restrict-qualifiers \
    compile:gnu-extension-declaration \
    compile:builtin-expect \
    compile:builtin-memcpy-call \
    run-builtin-prefetch.sh \
    run-gnu-function-attributes.sh \
    compile:gnu-function-asm-labels \
    compile:gnu-function-visibility \
    compile:gnu-visible-extern-arrays \
    compile:gnu-aligned-record-fields \
    compile:gnu-zero-length-record-arrays \
    compile:record-forward-declarations \
    compile:record-multi-declarators \
    native:run-xgene-parenthesized-pointer-record-field \
    compile:record-typedef-array-fields \
    compile:volatile-qualifiers \
    compile:array-bound-sizeof \
    compile:array-bound-integer-casts \
    compile:record-multidimensional-arrays \
    compile:record-length-one-arrays \
    compile:pointer-compound-subtraction \
    run-builtin-offsetof.sh \
    compile:pointer-integer-casts \
    compile:record-assignment-expressions \
    compile:bitwise-and-assignment-expressions \
    compile:compound-assignment-full \
    compile:for-expression-initializers \
    compile:mixed-double-arithmetic \
    compile:conditional-pointer-qualifiers \
    compile:comma-operator \
    compile:top-level-comma-conditions \
    compile:cast-type-classification \
    compile:stack-fixed-arguments \
    compile:static-local-record-initializers \
    compile:static-record-arrays \
    native:run-static-nested-record-initializers \
    compile:static-mixed-symbol-relocations \
    compile:external-record-compound-literal \
    compile:static-local-scalars \
    compile:static-local-inferred-arrays \
    compile:static-local-pointer-arrays \
    compile:static-local-fixed-arrays \
    compile:static-inferred-char-arrays \
    compile:static-inferred-integer-array \
    native:run-static-mutable-arrays \
    compile:static-preformed-array-zero \
    compile:static-fixed-record-array-zero \
    native:run-static-multidimensional-array-relocation \
    native:run-static-integer-floating-const-initializer \
    native:run-incomplete-function-pointer-array-lifetime \
    native:run-static-integer-pointer-cast-comparison \
    compile:static-aggregate-array-designators \
    compile:prefix-update-expressions \
    compile:function-designator-calls \
    compile:function-address-expressions \
    compile:preprocessed-line-markers \
    compile:gnu-signed-keyword \
    compile:gnu-int128-type \
    compile:bool-semantics \
    run-typeof-generic.sh \
    run-gnu-typedef-redundant-aligned.sh \
    compile:gnu-record-alignment \
    compile:gnu-empty-records \
    compile:gnu-extension-prefix-declarations \
    run-gnu-prefix-function-attributes.sh \
    native:run-gnu-is-constexpr-bitfield \
    strict:record-array-range-once \
    run-x86-security-function-attributes.sh \
    compile:gnu-format-cold-attributes \
    native:run-anonymous-record-members \
    native:run-block-scope-record-tags \
    compile:block-scope-extern-function-attributes \
    native:run-gnu-void-pointer-arithmetic \
    compile:gnu-statement-expression \
    compile:gnu-void-function-pointer-assignment \
    compile:gnu-no-profile-function-attribute \
    compile:gnu-typeof-array-type-name \
    compile:gnu-typeof-array-bound-cast \
    compile:function-arena-borrow-lifetime \
    run-gnu-statement-record-value.sh \
    native:run-gnu-array-range-initializer \
    compile:gnu-record-compound-literal \
    native:run-gnu-static-local-implicit-zero \
    compile:gnu-top-level-empty-declaration \
    compile:gnu-object-alignment-attribute \
    compile:gnu-auto-type-local \
    compile:gnu-local-interleaved-informational-attribute \
    run-gnu-array-object-identity.sh \
    native:run-gnu-local-label-address \
    native:run-gnu-inline-asm-fence \
    compile:gnu-typeof-local-declaration \
    compile:gnu-inline-asm-readwrite-output \
    run-gnu-inline-asm-operands.sh \
    run-gnu-inline-asm-named-operands.sh \
    run-gnu-inline-asm-goto.sh \
    run-gnu-fixed-register-bindings.sh \
    compile:gnu-compiletime-selection-builtins \
    compile:gnu-builtin-constant-p \
    native:run-gnu-builtin-object-size \
    run-gnu-call-frame-address.sh \
    native:run-gnu-builtin-unreachable \
    native:run-gnu-pointer-sign-call-conversion \
    compile:static-assert-declaration \
    compile:gnu-choose-expr-bitfield \
    compile:builtin-clzll \
    compile:alignof-type-query \
    compile:rv64-integer-aggregate-return \
    run-unnamed-bit-fields.sh \
    compile:gnu-overflow-builtins \
    run-gnu-register-inline-asm-output.sh \
    compile:function-linkage-inheritance \
    compile:static-prefix-object-attributes \
    compile:extern-incomplete-record-object \
    compile:extern-function-pointer-object \
    run-extern-interleaved-function-attributes.sh \
    compile:shared-function-declarator \
    compile:gnu-incomplete-record-function-signature \
    native:run-enum-tag-type-references \
    native:run-gnu-enum-entity \
    compile:extern-multi-declarators \
    compile:extern-multidimensional-array \
    native:run-gnu-extern-object-redeclaration \
    compile:gnu-section-symbol-attribute \
    compile:gnu-const-function-attribute \
    run-driver-gnu-predefines.sh \
    run-driver-include-precedence.sh \
    compile:gnu-inline-keyword-spellings \
    compile:gnu-octal-character-case-range
do
    run_focused "$test_script"
done
