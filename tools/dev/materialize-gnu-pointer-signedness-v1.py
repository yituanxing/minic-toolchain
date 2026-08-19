#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]
type_path = root / "src/frontend/type.c"
global_path = root / "src/frontend/parser_global.c"
run_path = root / "tests/compiler/c0/run.sh"

# Keep the core MinicType assignment contract strict. GNU C's
# incompatible-pointer-types continuation belongs at the language/parser
# boundary, not in the target-independent type relation.
type_text = type_path.read_text()
core_helper = r'''static bool minic_type_gnu_integer_pointer_signedness_compatible(MinicType target,
                                                                  MinicType source) {
    MinicType target_pointee;
    MinicType source_pointee;
    MinicType target_unqualified;
    MinicType source_unqualified;

    if (target.pointer_depth != 1U || source.pointer_depth != 1U ||
        !minic_type_pointee(target, &target_pointee) ||
        !minic_type_pointee(source, &source_pointee) ||
        !minic_type_unqualified(target_pointee, &target_unqualified) ||
        !minic_type_unqualified(source_pointee, &source_unqualified) ||
        !minic_type_is_integer(target_unqualified) ||
        !minic_type_is_integer(source_unqualified) ||
        minic_type_is_enum(target_unqualified) || minic_type_is_enum(source_unqualified) ||
        target_unqualified.base_kind != MINIC_TYPE_BASE_INT ||
        source_unqualified.base_kind != MINIC_TYPE_BASE_INT ||
        target_unqualified.integer_rank != source_unqualified.integer_rank ||
        target_unqualified.is_plain_char != source_unqualified.is_plain_char ||
        target_unqualified.explicit_alignment != source_unqualified.explicit_alignment) {
        return false;
    }
    if (minic_type_is_const(source_pointee) && !minic_type_is_const(target_pointee)) {
        return false;
    }
    if (minic_type_is_volatile(source_pointee) && !minic_type_is_volatile(target_pointee)) {
        return false;
    }
    return true;
}

'''
if core_helper in type_text:
    type_text = type_text.replace(core_helper, "", 1)
wide_return = r'''    return minic_type_equal(unqualified_target, unqualified_source) ||
           minic_type_pointer_qualification_compatible(unqualified_target, unqualified_source) ||
           minic_type_gnu_integer_pointer_signedness_compatible(unqualified_target,
                                                                 unqualified_source) ||
           minic_type_void_object_pointer_compatible(unqualified_target, unqualified_source);
'''
strict_return = r'''    return minic_type_equal(unqualified_target, unqualified_source) ||
           minic_type_pointer_qualification_compatible(unqualified_target, unqualified_source) ||
           minic_type_void_object_pointer_compatible(unqualified_target, unqualified_source);
'''
if wide_return in type_text:
    type_text = type_text.replace(wide_return, strict_return, 1)
elif strict_return not in type_text:
    raise SystemExit("core assignment compatibility return changed")
type_path.write_text(type_text)

# GNU C accepts an incompatible integer-pointer signedness initialization with
# a diagnostic. MiniC does not have warning severity yet, so model the bounded
# continuation here while preserving qualifier and rank safety.
global_text = global_path.read_text()
helper = r'''static bool static_pointer_initializer_gnu_signedness_compatible(
    const MinicC0Program *program, MinicType target_type, MinicExpressionId expression_id) {
    const MinicExpression *expression;
    MinicType source_type;
    MinicType target_pointee;
    MinicType source_pointee;
    MinicType target_unqualified;
    MinicType source_unqualified;

    if (program == NULL || target_type.pointer_depth != 1U) {
        return false;
    }
    expression = minic_c0_program_expression(program, expression_id);
    if (expression == NULL) {
        return false;
    }
    source_type = expression->type;
    if (source_type.pointer_depth != 1U ||
        !minic_type_pointee(target_type, &target_pointee) ||
        !minic_type_pointee(source_type, &source_pointee) ||
        !minic_type_unqualified(target_pointee, &target_unqualified) ||
        !minic_type_unqualified(source_pointee, &source_unqualified) ||
        !minic_type_is_integer(target_unqualified) ||
        !minic_type_is_integer(source_unqualified) || minic_type_is_enum(target_unqualified) ||
        minic_type_is_enum(source_unqualified) ||
        target_unqualified.base_kind != MINIC_TYPE_BASE_INT ||
        source_unqualified.base_kind != MINIC_TYPE_BASE_INT ||
        target_unqualified.integer_rank != source_unqualified.integer_rank ||
        target_unqualified.is_plain_char != source_unqualified.is_plain_char ||
        target_unqualified.explicit_alignment != source_unqualified.explicit_alignment ||
        (minic_type_is_const(source_pointee) && !minic_type_is_const(target_pointee)) ||
        (minic_type_is_volatile(source_pointee) && !minic_type_is_volatile(target_pointee))) {
        return false;
    }
    return target_unqualified.integer_sign != source_unqualified.integer_sign;
}

'''
parse_anchor = "static bool parse_static_pointer_initializer(MinicParser *parser,\n"
if helper not in global_text:
    if global_text.count(parse_anchor) != 1:
        raise SystemExit("static pointer initializer anchor changed")
    global_text = global_text.replace(parse_anchor, helper + parse_anchor, 1)
old_check = r'''    if (!minic_c0_assignment_compatible(parser->program, target_type, expression_id)) {
        minic_parser_error(parser, "static pointer initializer type mismatch");
        return false;
    }
'''
intermediate_check = r'''    if (!minic_c0_assignment_compatible(parser->program, target_type, expression_id) &&
        !static_pointer_initializer_gnu_signedness_compatible(
            parser->program, target_type, expression_id)) {
        minic_parser_error(parser, "static pointer initializer type mismatch");
        return false;
    }
'''
new_check = r'''    {
        bool strict_compatible;
        bool gnu_signedness_compatible;

        strict_compatible =
            minic_c0_assignment_compatible(parser->program, target_type, expression_id);
        gnu_signedness_compatible =
            !strict_compatible && static_pointer_initializer_gnu_signedness_compatible(
                                      parser->program, target_type, expression_id);
        if (!strict_compatible && !gnu_signedness_compatible) {
            minic_parser_error(parser, "static pointer initializer type mismatch");
            return false;
        }
        if (gnu_signedness_compatible) {
            /* Both an explicit pointer cast and GNU's incompatible-pointer-types
             * continuation authorize the same address-bit reinterpretation in
             * the persisted relocation contract. The source-level acceptance
             * remains bounded by the predicate above. */
            initializer->has_explicit_pointer_cast = true;
        }
    }
'''
if new_check not in global_text:
    if intermediate_check in global_text:
        global_text = global_text.replace(intermediate_check, new_check, 1)
    elif old_check in global_text:
        global_text = global_text.replace(old_check, new_check, 1)
    else:
        raise SystemExit("static pointer assignment check changed")
global_path.write_text(global_text)

gate = r'''
MINIC="$minic" HOST_CC="$host_cc" BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
  sh "$root/tests/compiler/c0/run-gnu-pointer-signedness.sh"
'''
run = run_path.read_text()
if gate.strip() not in run:
    if not run.endswith("\n"):
        run += "\n"
    run += gate
run_path.write_text(run)

print("materialized bounded GNU static pointer signedness continuation")
