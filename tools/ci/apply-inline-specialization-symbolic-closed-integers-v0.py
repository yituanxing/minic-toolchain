#!/usr/bin/env python3
from pathlib import Path

p = Path("src/compiler/compiler.c")
text = p.read_text()

helper_anchor = '''static bool minic_inline_symbolic_variant_matches(
'''
helper = '''static bool minic_inline_symbolic_integer_inputs_are_closed(
    const MinicFunction *source,
    const bool *integer_known) {
    size_t i;

    if (source == NULL || integer_known == NULL) {
        return false;
    }
    for (i = 0U; i < source->parameter_count; ++i) {
        if (minic_type_is_integer(source->parameter_types[i]) && !integer_known[i]) {
            return false;
        }
    }
    return true;
}

'''
if text.count(helper_anchor) != 1:
    raise SystemExit("expected one symbolic variant helper anchor")
text = text.replace(helper_anchor, helper + helper_anchor, 1)

direct_anchor = '''        if (current.is_integer_specialization) {
            for (i = 0U; i < source.parameter_count; ++i) {
                integer_known[i] = current.specialization_integer_known[i];
                integer_bits[i] = current.specialization_integer_bits[i];
                symbolic_known[i] = current.specialization_symbolic_known[i];
                symbolic_expression[i] =
                    current.specialization_symbolic_expression[i];
            }
        }
        for (i = 0U; i < source.parameter_count; ++i) {
            MinicExpressionId argument_id = call->value.call.arguments[i];
'''
direct_replacement = '''        if (current.is_integer_specialization) {
            for (i = 0U; i < source.parameter_count; ++i) {
                integer_known[i] = current.specialization_integer_known[i];
                integer_bits[i] = current.specialization_integer_bits[i];
                symbolic_known[i] = current.specialization_symbolic_known[i];
                symbolic_expression[i] =
                    current.specialization_symbolic_expression[i];
            }
        }
        /* Do not manufacture a half-specialized variant for a helper whose
         * integer parameters are still runtime values.  GNU immediate-only
         * asm inside helpers such as arch_static_branch requires both the
         * symbolic key and the branch integer to be closed before Core lower. */
        if (!minic_inline_symbolic_integer_inputs_are_closed(&source, integer_known)) {
            continue;
        }
        for (i = 0U; i < source.parameter_count; ++i) {
            MinicExpressionId argument_id = call->value.call.arguments[i];
'''
if text.count(direct_anchor) != 1:
    raise SystemExit("expected one direct symbolic specialization anchor")
text = text.replace(direct_anchor, direct_replacement, 1)

transitive_anchor = '''                if (current.is_integer_specialization) {
                    for (i = 0U; i < source.parameter_count; ++i) {
                        integer_known[i] = current.specialization_integer_known[i];
                        integer_bits[i] = current.specialization_integer_bits[i];
                        symbolic_known[i] = current.specialization_symbolic_known[i];
                        symbolic_expression[i] =
                            current.specialization_symbolic_expression[i];
                    }
                }
                for (i = 0U; i < source.parameter_count; ++i) {
                    MinicExpressionId resolved_expression;
'''
transitive_replacement = '''                if (current.is_integer_specialization) {
                    for (i = 0U; i < source.parameter_count; ++i) {
                        integer_known[i] = current.specialization_integer_known[i];
                        integer_bits[i] = current.specialization_integer_bits[i];
                        symbolic_known[i] = current.specialization_symbolic_known[i];
                        symbolic_expression[i] =
                            current.specialization_symbolic_expression[i];
                    }
                }
                if (!minic_inline_symbolic_integer_inputs_are_closed(&source, integer_known)) {
                    continue;
                }
                for (i = 0U; i < source.parameter_count; ++i) {
                    MinicExpressionId resolved_expression;
'''
if text.count(transitive_anchor) != 1:
    raise SystemExit("expected one transitive symbolic specialization anchor")
text = text.replace(transitive_anchor, transitive_replacement, 1)

p.write_text(text)
print("MINIC_INLINE_SPECIALIZATION_SYMBOLIC_CLOSED_INTEGERS_V0=APPLIED")
