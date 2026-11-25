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
    run-enum-constant-scopes.sh \
    compile:integer-constant-bitwise \
    compile:unsigned-64-literals \
    run-builtin-va-list.sh \
    run-aarch64-builtin-va-list-declaration.sh \
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
    run-xgene-parenthesized-pointer-record-field.sh \
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
    run-static-nested-record-initializers.sh \
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
    run-static-multidimensional-array-relocation.sh \
    run-static-integer-floating-const-initializer.sh \
    run-incomplete-function-pointer-array-lifetime.sh \
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
    run-gnu-is-constexpr-bitfield.sh \
    strict:record-array-range-once \
    run-x86-security-function-attributes.sh \
    compile:gnu-format-cold-attributes \
    native:run-anonymous-record-members \
    run-block-scope-record-tags.sh \
    compile:block-scope-extern-function-attributes \
    run-gnu-void-pointer-arithmetic.sh \
    compile:gnu-statement-expression \
    compile:gnu-void-function-pointer-assignment \
    compile:gnu-no-profile-function-attribute \
    compile:gnu-typeof-array-type-name \
    compile:gnu-typeof-array-bound-cast \
    compile:function-arena-borrow-lifetime \
    run-gnu-statement-record-value.sh \
    run-gnu-array-range-initializer.sh \
    compile:gnu-record-compound-literal \
    run-gnu-static-local-implicit-zero.sh \
    compile:gnu-top-level-empty-declaration \
    compile:gnu-object-alignment-attribute \
    compile:gnu-auto-type-local \
    compile:gnu-local-interleaved-informational-attribute \
    run-gnu-array-object-identity.sh \
    run-gnu-local-label-address.sh \
    run-gnu-inline-asm-fence.sh \
    compile:gnu-typeof-local-declaration \
    compile:gnu-inline-asm-readwrite-output \
    run-gnu-inline-asm-operands.sh \
    run-gnu-inline-asm-named-operands.sh \
    run-gnu-inline-asm-goto.sh \
    run-gnu-fixed-register-bindings.sh \
    compile:gnu-compiletime-selection-builtins \
    compile:gnu-builtin-constant-p \
    run-gnu-builtin-object-size.sh \
    run-gnu-call-frame-address.sh \
    run-gnu-builtin-unreachable.sh \
    run-gnu-pointer-sign-call-conversion.sh \
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
    run-enum-tag-type-references.sh \
    run-gnu-enum-entity.sh \
    compile:extern-multi-declarators \
    compile:extern-multidimensional-array \
    run-gnu-extern-object-redeclaration.sh \
    compile:gnu-section-symbol-attribute \
    compile:gnu-const-function-attribute \
    run-driver-gnu-predefines.sh \
    run-driver-include-precedence.sh \
    compile:gnu-inline-keyword-spellings \
    compile:gnu-octal-character-case-range
do
    run_focused "$test_script"
done
