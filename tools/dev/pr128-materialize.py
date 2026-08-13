from pathlib import Path

internal = Path("src/frontend/parser_internal.h")
text = internal.read_text()
old = '''typedef struct MinicParsedFunctionDeclarator {
    MinicSourceSpan name_span;
    MinicType parameter_types[MINIC_MAX_FUNCTION_PARAMETERS];
'''
new = '''typedef struct MinicParsedFunctionDeclarator {
    MinicSourceSpan name_span;
    MinicParsedAttributeList attributes;
    MinicType parameter_types[MINIC_MAX_FUNCTION_PARAMETERS];
'''
if text.count(old) != 1:
    raise SystemExit(f"function declarator carrier anchor mismatch: {text.count(old)}")
internal.write_text(text.replace(old, new, 1))

# Syntax owner: collect GNU attributes after the complete pointer sequence and
# before the direct declarator name.  Do not assign semantics here; callers
# classify the final entity and route the collected list.
decl = Path("src/frontend/parser_declarator.c")
text = decl.read_text()
old = '''    if (require_pointer && declarator->pointer_depth == 0U) {
        minic_parser_error(parser, "function declarator requires pointer indirection");
        return false;
    }

    if (parser->current.kind == MINIC_TOKEN_IDENTIFIER) {
'''
new = '''    if (require_pointer && declarator->pointer_depth == 0U) {
        minic_parser_error(parser, "function declarator requires pointer indirection");
        return false;
    }
    if (!minic_parser_collect_gnu_attribute_lists(parser, &declarator->attributes)) {
        return false;
    }

    if (parser->current.kind == MINIC_TOKEN_IDENTIFIER) {
'''
if text.count(old) != 1:
    raise SystemExit(f"function declarator collection anchor mismatch: {text.count(old)}")
decl.write_text(text.replace(old, new, 1))

# File-scope function-pointer objects consume the newly collected declarator
# attributes through the exact same object-attribute owner used by declaration
# prefix/postfix placements.
func = Path("src/frontend/parser_function.c")
text = func.read_text()
old = '''    MinicParsedAttributeList deferred_attributes;
    MinicParsedDeclarationPrefix declaration_prefix;
'''
new = '''    MinicParsedAttributeList deferred_attributes;
    MinicParsedAttributeList declarator_attributes;
    MinicParsedDeclarationPrefix declaration_prefix;
'''
if text.count(old) != 1:
    raise SystemExit(f"parse_function attribute state anchor mismatch: {text.count(old)}")
text = text.replace(old, new, 1)
old = '''    (void)memset(&deferred_attributes, 0, sizeof(deferred_attributes));
    (void)memset(&declaration_prefix, 0, sizeof(declaration_prefix));
'''
new = '''    (void)memset(&deferred_attributes, 0, sizeof(deferred_attributes));
    (void)memset(&declarator_attributes, 0, sizeof(declarator_attributes));
    (void)memset(&declaration_prefix, 0, sizeof(declaration_prefix));
'''
if text.count(old) != 1:
    raise SystemExit(f"parse_function attribute init anchor mismatch: {text.count(old)}")
text = text.replace(old, new, 1)
old = '''            name_span = declarator.name_span;
            is_function_pointer_object = true;
'''
new = '''            name_span = declarator.name_span;
            declarator_attributes = declarator.attributes;
            is_function_pointer_object = true;
'''
if text.count(old) != 1:
    raise SystemExit(f"function pointer object carrier anchor mismatch: {text.count(old)}")
text = text.replace(old, new, 1)
old = '''        if (!minic_parser_apply_object_attribute_list(parser,
                                                      &deferred_attributes,
                                                      section_name,
                                                      sizeof(section_name),
                                                      &section_name_length,
                                                      &has_section,
                                                      &object_explicit_alignment)) {
            return false;
        }
        if (has_visibility || object_explicit_alignment != 0U) {
'''
new = '''        if (!minic_parser_apply_object_attribute_list(parser,
                                                      &deferred_attributes,
                                                      section_name,
                                                      sizeof(section_name),
                                                      &section_name_length,
                                                      &has_section,
                                                      &object_explicit_alignment) ||
            !minic_parser_apply_object_attribute_list(parser,
                                                      &declarator_attributes,
                                                      section_name,
                                                      sizeof(section_name),
                                                      &section_name_length,
                                                      &has_section,
                                                      &object_explicit_alignment)) {
            return false;
        }
        if (has_visibility || object_explicit_alignment != 0U) {
'''
# Two object branches have the same first apply block; replace both deliberately.
if text.count(old) != 2:
    raise SystemExit(f"object attribute routing anchor mismatch: {text.count(old)}")
text = text.replace(old, new, 2)
func.write_text(text)

# Typedef caller classifies the final entity as a type alias.  Keep this new
# placement explicitly fail-closed until type/declarator attribute semantics are
# modeled, rather than leaking object-only attributes into a type.
typedef = Path("src/frontend/parser_typedef.c")
text = typedef.read_text()
old = '''    if (declarator.is_variadic) {
        minic_parser_error(parser, "variadic function pointer typedefs are not supported yet");
        return false;
    }
    if (!minic_parser_build_function_declarator_type(
'''
new = '''    if (declarator.is_variadic) {
        minic_parser_error(parser, "variadic function pointer typedefs are not supported yet");
        return false;
    }
    if (declarator.attributes.count != 0U) {
        minic_parser_error(
            parser,
            "GNU attributes inside function pointer typedef declarators are not implemented yet");
        return false;
    }
    if (!minic_parser_build_function_declarator_type(
'''
if text.count(old) != 1:
    raise SystemExit(f"typedef boundary anchor mismatch: {text.count(old)}")
typedef.write_text(text.replace(old, new, 1))

fixture = Path("tests/compiler/c0/deferred_declarator_attributes.c")
text = fixture.read_text()
append = r'''

void (*__attribute__((__section__(".init.fp-object"))) late_time_init_shape)(void);
'''
if "late_time_init_shape" in text:
    raise SystemExit("function pointer object fixture already present")
fixture.write_text(text.rstrip() + append)

Path("tests/compiler/c0/invalid_function_pointer_object_function_attribute.c").write_text(
    'void (*__attribute__((noclone)) invalid_function_pointer_object)(void);\n'
)
Path("tests/compiler/c0/invalid_function_pointer_typedef_interleaved_attribute.c").write_text(
    'typedef void (*__attribute__((section(".bad.type"))) invalid_interleaved_type)(void);\n'
)

runner = Path("tests/compiler/c0/run-deferred-declarator-attributes.sh")
text = runner.read_text()
old = '''grep -F 'used_function_shape:' "$work/deferred_declarator_attributes.s" >/dev/null

if "$minic" -S "$root/tests/compiler/c0/invalid_function_attribute_on_pointer_object.c" \\
'''
new = '''grep -F 'used_function_shape:' "$work/deferred_declarator_attributes.s" >/dev/null
grep -F '.section .init.fp-object' "$work/deferred_declarator_attributes.s" >/dev/null
grep -F 'late_time_init_shape:' "$work/deferred_declarator_attributes.s" >/dev/null

if "$minic" -S "$root/tests/compiler/c0/invalid_function_attribute_on_pointer_object.c" \\
'''
if text.count(old) != 1:
    raise SystemExit(f"focused positive anchor mismatch: {text.count(old)}")
text = text.replace(old, new, 1)
old = '''grep -F 'unsupported GNU record field attribute' "$work/invalid-used-field.stderr" >/dev/null

printf '%s\\n' 'PASS compiler/c0/deferred_declarator_attributes pre-pointer=generic post-pointer=generic function-target=late object-target=late section=preserved noinline=parse-only noclone=parse-only+function-only+zero-arg used=parse-only+function-object+zero-arg'
'''
new = '''grep -F 'unsupported GNU record field attribute' "$work/invalid-used-field.stderr" >/dev/null

if "$minic" -S "$root/tests/compiler/c0/invalid_function_pointer_object_function_attribute.c" \\
    -o "$work/invalid-fp-object-function-attr.s" \\
    >"$work/invalid-fp-object-function-attr.stdout" \\
    2>"$work/invalid-fp-object-function-attr.stderr"; then
    printf '%s\\n' 'FAIL compiler/c0/deferred_declarator_attributes: function attribute leaked through function-pointer object declarator' >&2
    exit 1
fi
grep -F 'unsupported GNU object attribute' "$work/invalid-fp-object-function-attr.stderr" >/dev/null

if "$minic" -S "$root/tests/compiler/c0/invalid_function_pointer_typedef_interleaved_attribute.c" \\
    -o "$work/invalid-fp-typedef-attr.s" \\
    >"$work/invalid-fp-typedef-attr.stdout" 2>"$work/invalid-fp-typedef-attr.stderr"; then
    printf '%s\\n' 'FAIL compiler/c0/deferred_declarator_attributes: interleaved function-pointer typedef attribute widened silently' >&2
    exit 1
fi
grep -F 'GNU attributes inside function pointer typedef declarators are not implemented yet' \\
    "$work/invalid-fp-typedef-attr.stderr" >/dev/null

printf '%s\\n' 'PASS compiler/c0/deferred_declarator_attributes pre-pointer=generic post-pointer=generic function-target=late object-target=late section=preserved noinline=parse-only noclone=parse-only+function-only+zero-arg used=parse-only+function-object+zero-arg fp-object-interleaved=collected+object-routed typedef-interleaved=fail-closed'
'''
if text.count(old) != 1:
    raise SystemExit(f"focused negative anchor mismatch: {text.count(old)}")
runner.write_text(text.replace(old, new, 1))
