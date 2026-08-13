from pathlib import Path

parser = Path("src/frontend/parser_global.c")
text = parser.read_text()
insert_at = text.find("static bool parse_static_scalar(MinicParser *parser, MinicType type, MinicSourceSpan name_span) {")
if insert_at < 0:
    raise SystemExit("parse_static_scalar anchor missing")
helper = '''static bool static_object_address_relocation_target(const MinicC0Program *program,
                                                    MinicExpressionId expression_id,
                                                    MinicGlobalObjectId *target_object_id) {
    const MinicExpression *expression;

    if (program == NULL || target_object_id == NULL) {
        return false;
    }
    expression = minic_c0_program_expression(program, expression_id);
    while (expression != NULL &&
           (expression->kind == MINIC_EXPRESSION_CAST ||
            expression->kind == MINIC_EXPRESSION_BITCAST ||
            expression->kind == MINIC_EXPRESSION_CONVERSION)) {
        expression_id = expression->value.unary.operand;
        expression = minic_c0_program_expression(program, expression_id);
    }
    if (expression == NULL || expression->kind != MINIC_EXPRESSION_ADDRESS_OF) {
        return false;
    }
    expression = minic_c0_program_expression(program, expression->value.unary.operand);
    if (expression == NULL || expression->kind != MINIC_EXPRESSION_GLOBAL_OBJECT ||
        expression->value.global_object_id == MINIC_GLOBAL_OBJECT_INVALID ||
        minic_c0_program_global_object(program, expression->value.global_object_id) == NULL) {
        return false;
    }
    *target_object_id = expression->value.global_object_id;
    return true;
}

'''
if "static_object_address_relocation_target" in text:
    raise SystemExit("address relocation helper already present")
text = text[:insert_at] + helper + text[insert_at:]
old = '''    } else if (minic_type_is_pointer(type)) {
        if (type_is_function_pointer(type) && parser->current.kind == MINIC_TOKEN_IDENTIFIER) {
            MinicFunctionId function_id;
            MinicType designator_type;

            function_id = minic_parser_find_function(parser, parser->current.span);
            if (function_id == MINIC_FUNCTION_INVALID ||
                !function_designator_type(parser, function_id, &designator_type) ||
                !minic_type_assignment_compatible(type, designator_type)) {
                minic_parser_error(parser, "static function pointer initializer type mismatch");
                return false;
            }
            if (!minic_parser_advance(parser) ||
                !minic_c0_global_object_add_function_relocation(
                    parser->program, object_id, 0U, function_id) ||
                !minic_c0_global_object_set_zero_initialized(parser->program, object_id)) {
                minic_parser_error(parser, "cannot record static function pointer initializer");
                return false;
            }
        } else if (!minic_parser_parse_zero_pointer_constant(parser) ||
                   !minic_c0_global_object_set_zero_initialized(parser->program, object_id)) {
            return false;
        }
'''
new = '''    } else if (minic_type_is_pointer(type)) {
        if (type_is_function_pointer(type) && parser->current.kind == MINIC_TOKEN_IDENTIFIER) {
            MinicFunctionId function_id;
            MinicType designator_type;

            function_id = minic_parser_find_function(parser, parser->current.span);
            if (function_id == MINIC_FUNCTION_INVALID ||
                !function_designator_type(parser, function_id, &designator_type) ||
                !minic_type_assignment_compatible(type, designator_type)) {
                minic_parser_error(parser, "static function pointer initializer type mismatch");
                return false;
            }
            if (!minic_parser_advance(parser) ||
                !minic_c0_global_object_add_function_relocation(
                    parser->program, object_id, 0U, function_id) ||
                !minic_c0_global_object_set_zero_initialized(parser->program, object_id)) {
                minic_parser_error(parser, "cannot record static function pointer initializer");
                return false;
            }
        } else {
            MinicExpressionId initializer_id;
            MinicGlobalObjectId target_object_id;

            if (!minic_parser_parse_expression(parser, &initializer_id, 0U)) {
                return false;
            }
            if (!minic_c0_assignment_compatible(parser->program, type, initializer_id)) {
                minic_parser_error(parser, "static pointer initializer type mismatch");
                return false;
            }
            if (minic_c0_expression_is_null_pointer_constant_v0(parser->program, initializer_id)) {
                if (!minic_c0_global_object_set_zero_initialized(parser->program, object_id)) {
                    minic_parser_error(parser, "cannot record static null-pointer initializer");
                    return false;
                }
            } else if (static_object_address_relocation_target(
                           parser->program, initializer_id, &target_object_id)) {
                if (!minic_c0_global_object_set_zero_initialized(parser->program, object_id) ||
                    !minic_c0_global_object_add_object_relocation(
                        parser->program, object_id, 0U, target_object_id)) {
                    minic_parser_error(parser, "cannot record static object-address relocation");
                    return false;
                }
            } else {
                minic_parser_error(
                    parser,
                    "static pointer initializer requires a null or zero-addend object address constant");
                return false;
            }
        }
'''
if text.count(old) != 1:
    raise SystemExit(f"static pointer initializer anchor mismatch: {text.count(old)}")
parser.write_text(text.replace(old, new, 1))

Path("tests/compiler/c0/static_object_address_relocation.c").write_text(r'''int external_address_target;
static int internal_address_target = 7;

static void *external_address = (void *)&external_address_target;
static int *internal_address = &internal_address_target;
static void *parenthesized_address = (void *)(&internal_address_target);

int read_static_object_addresses(void) {
    return external_address != (void *)0 && internal_address != (void *)0 &&
           parenthesized_address != (void *)0;
}
''')
Path("tests/compiler/c0/invalid_static_object_address_type.c").write_text(r'''int address_type_target;
static long *invalid_address_type = &address_type_target;
''')
Path("tests/compiler/c0/invalid_static_object_address_addend.c").write_text(r'''int address_addend_target;
static int *invalid_address_addend = &address_addend_target + 1;
''')
Path("tests/compiler/c0/run-static-object-address-relocation.sh").write_text(r'''#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-static-object-address

rm -rf "$work"
mkdir -p "$work"

"$minic" -S "$root/tests/compiler/c0/static_object_address_relocation.c" \
    -o "$work/static_object_address_relocation.s"
test -s "$work/static_object_address_relocation.s"
grep -F 'external_address:' "$work/static_object_address_relocation.s" >/dev/null
grep -F '.dword external_address_target' "$work/static_object_address_relocation.s" >/dev/null
grep -F 'internal_address:' "$work/static_object_address_relocation.s" >/dev/null
count=$(grep -F -c '.dword internal_address_target' "$work/static_object_address_relocation.s")
test "$count" -eq 2

if "$minic" -S "$root/tests/compiler/c0/invalid_static_object_address_type.c" \
    -o "$work/invalid-type.s" >"$work/invalid-type.stdout" 2>"$work/invalid-type.stderr"; then
    printf '%s\n' 'FAIL compiler/c0/static-object-address: incompatible pointer type accepted' >&2
    exit 1
fi
grep -F 'static pointer initializer type mismatch' "$work/invalid-type.stderr" >/dev/null

if "$minic" -S "$root/tests/compiler/c0/invalid_static_object_address_addend.c" \
    -o "$work/invalid-addend.s" >"$work/invalid-addend.stdout" 2>"$work/invalid-addend.stderr"; then
    printf '%s\n' 'FAIL compiler/c0/static-object-address: relocation addend accepted without schema support' >&2
    exit 1
fi
grep -F 'static pointer initializer requires a null or zero-addend object address constant' \
    "$work/invalid-addend.stderr" >/dev/null

printf '%s\n' 'PASS compiler/c0/static-object-address relocation=scalar-global-address cast+direct+parenthesized null=shared addend=fail-closed type=checked'
''')

gate = Path(".github/scripts/compiler-c0-full-gate.sh")
text = gate.read_text()
old = '''static_global_section_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \\
    BUILD_DIR="$root/build/ci-static-global-section" \\
        sh tests/compiler/c0/run-static-global-object-section.sh
}

external_cjson_frontier() {
'''
new = '''static_global_section_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \\
    BUILD_DIR="$root/build/ci-static-global-section" \\
        sh tests/compiler/c0/run-static-global-object-section.sh
}

static_object_address_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \\
    BUILD_DIR="$root/build/ci-static-object-address" \\
        sh tests/compiler/c0/run-static-object-address-relocation.sh
}

external_cjson_frontier() {
'''
if text.count(old) != 1:
    raise SystemExit(f"gate helper anchor mismatch: {text.count(old)}")
text = text.replace(old, new, 1)
old = '''start_gate static-global-section-focused static_global_section_focused
start_gate wide-string-focused wide_string_focused
'''
new = '''start_gate static-global-section-focused static_global_section_focused
start_gate static-object-address-focused static_object_address_focused
start_gate wide-string-focused wide_string_focused
'''
if text.count(old) != 1:
    raise SystemExit(f"gate start anchor mismatch: {text.count(old)}")
gate.write_text(text.replace(old, new, 1))
