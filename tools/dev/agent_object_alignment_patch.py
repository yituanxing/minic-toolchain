from pathlib import Path

root = Path(__file__).resolve().parents[2]

def replace_once(path: Path, old: str, new: str, label: str) -> None:
    text = path.read_text()
    if text.count(old) != 1:
        raise SystemExit(f"{label}: expected one anchor, found {text.count(old)}")
    path.write_text(text.replace(old, new, 1))

# Attribute registry: aligned is valid on concrete objects as well as types/fields.
attribute = root / "src/frontend/attribute.c"
text = attribute.read_text()
old = '''    MINIC_ATTRIBUTE_ENTRY("aligned",
                          MINIC_ATTRIBUTE_ALIGNED,
                          MINIC_ATTRIBUTE_CLASS_LAYOUT,
                          MINIC_ATTRIBUTE_TARGET_TYPE | MINIC_ATTRIBUTE_TARGET_FIELD),
    MINIC_ATTRIBUTE_ENTRY("__aligned__",
                          MINIC_ATTRIBUTE_ALIGNED,
                          MINIC_ATTRIBUTE_CLASS_LAYOUT,
                          MINIC_ATTRIBUTE_TARGET_TYPE | MINIC_ATTRIBUTE_TARGET_FIELD),
'''
new = '''    MINIC_ATTRIBUTE_ENTRY("aligned",
                          MINIC_ATTRIBUTE_ALIGNED,
                          MINIC_ATTRIBUTE_CLASS_LAYOUT,
                          MINIC_ATTRIBUTE_TARGET_OBJECT | MINIC_ATTRIBUTE_TARGET_TYPE |
                              MINIC_ATTRIBUTE_TARGET_FIELD),
    MINIC_ATTRIBUTE_ENTRY("__aligned__",
                          MINIC_ATTRIBUTE_ALIGNED,
                          MINIC_ATTRIBUTE_CLASS_LAYOUT,
                          MINIC_ATTRIBUTE_TARGET_OBJECT | MINIC_ATTRIBUTE_TARGET_TYPE |
                              MINIC_ATTRIBUTE_TARGET_FIELD),
'''
if text.count(old) != 1:
    raise SystemExit("aligned registry anchor mismatch")
attribute.write_text(text.replace(old, new, 1))

# Global-object semantic side data and setter API.
ast = root / "src/frontend/ast.h"
replace_once(
    ast,
    '''    size_t object_relocation_capacity;
    size_t storage_size;
    size_t alignment;
''',
    '''    size_t object_relocation_capacity;
    size_t explicit_alignment;
    size_t storage_size;
    size_t alignment;
''',
    "global object alignment field",
)
replace_once(
    ast,
    '''bool minic_c0_global_object_set_section(MinicC0Program *program,
                                        MinicGlobalObjectId global_object_id,
                                        const char *name,
                                        size_t name_length);
''',
    '''bool minic_c0_global_object_set_section(MinicC0Program *program,
                                        MinicGlobalObjectId global_object_id,
                                        const char *name,
                                        size_t name_length);
bool minic_c0_global_object_set_explicit_alignment(MinicC0Program *program,
                                                   MinicGlobalObjectId global_object_id,
                                                   size_t alignment);
''',
    "global object alignment prototype",
)

ast_global = root / "src/frontend/ast_global.c"
text = ast_global.read_text()
anchor = '''bool minic_c0_global_object_set_section(MinicC0Program *program,
                                        MinicGlobalObjectId global_object_id,
                                        const char *name,
                                        size_t name_length) {
'''
idx = text.find(anchor)
if idx < 0:
    raise SystemExit("section setter anchor missing")
# Insert alignment setter immediately before section setter.
setter = '''bool minic_c0_global_object_set_explicit_alignment(MinicC0Program *program,
                                                   MinicGlobalObjectId global_object_id,
                                                   size_t alignment) {
    MinicGlobalObject *object;

    if (program == NULL || global_object_id >= program->global_object_count || alignment == 0U ||
        (alignment & (alignment - 1U)) != 0U) {
        return false;
    }
    object = &program->global_objects[global_object_id];
    if (alignment > object->explicit_alignment) {
        object->explicit_alignment = alignment;
    }
    return true;
}

'''
ast_global.write_text(text[:idx] + setter + text[idx:])

# Materialize per-object minimum alignment after canonical type layout.
layout = root / "src/target/riscv64/layout.c"
replace_once(
    layout,
    '''        if (!minic_riscv64_type_layout(program, object->type, &storage_size, &alignment)) {
            return false;
        }
        object->storage_size = storage_size;
        object->alignment = alignment;
''',
    '''        if (!minic_riscv64_type_layout(program, object->type, &storage_size, &alignment)) {
            return false;
        }
        if (object->explicit_alignment != 0U) {
            if ((object->explicit_alignment & (object->explicit_alignment - 1U)) != 0U) {
                return false;
            }
            if (object->explicit_alignment > alignment) {
                alignment = object->explicit_alignment;
            }
        }
        object->storage_size = storage_size;
        object->alignment = alignment;
''',
    "global object layout materialization",
)

# Parser declaration contract carries the deferred object alignment into extern object semantics.
internal = root / "src/frontend/parser_internal.h"
replace_once(
    internal,
    '''                                                 const char *section_name,
                                                 size_t section_name_length,
                                                 bool has_section,
                                                 MinicSymbolVisibility visibility,
''',
    '''                                                 const char *section_name,
                                                 size_t section_name_length,
                                                 bool has_section,
                                                 size_t explicit_alignment,
                                                 MinicSymbolVisibility visibility,
''',
    "extern after-head prototype",
)

# Decode deferred aligned(...) from its original source span and keep it object-local.
parser_function = root / "src/frontend/parser_function.c"
text = parser_function.read_text()
anchor = '''static bool apply_object_attribute_list(MinicParser *parser,
                                        const MinicParsedAttributeList *attributes,
                                        char *section_name,
                                        size_t section_capacity,
                                        size_t *section_name_length,
                                        bool *has_section) {
'''
if anchor not in text:
    raise SystemExit("apply object attribute anchor missing")
helper = '''static bool decode_deferred_alignment_argument(MinicParser *parser,
                                               const MinicParsedAttribute *attribute,
                                               size_t *explicit_alignment) {
    MinicParser probe;
    int64_t parsed_alignment;
    size_t alignment;

    if (parser == NULL || attribute == NULL || explicit_alignment == NULL ||
        !attribute->has_arguments ||
        attribute->arguments_span.end.offset <= attribute->arguments_span.begin.offset + 1U) {
        return false;
    }
    probe = *parser;
    minic_lexer_initialize(&probe.lexer, parser->path, parser->source, parser->lexer.length);
    probe.lexer.cursor = attribute->arguments_span.begin.offset + 1U;
    probe.lexer.line = attribute->arguments_span.begin.line;
    probe.lexer.column = attribute->arguments_span.begin.column + 1U;
    if (!minic_parser_advance(&probe) ||
        !minic_parser_parse_integer_constant_expression(&probe, &parsed_alignment) ||
        probe.current.kind != MINIC_TOKEN_RPAREN ||
        probe.current.span.end.offset != attribute->arguments_span.end.offset) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser,
                               "GNU object alignment requires one integer constant expression");
        }
        return false;
    }
    if (parsed_alignment <= 0 || (uint64_t)parsed_alignment > (uint64_t)SIZE_MAX) {
        minic_parser_error(parser, "GNU object alignment must be a positive target-size value");
        return false;
    }
    alignment = (size_t)parsed_alignment;
    if ((alignment & (alignment - 1U)) != 0U) {
        minic_parser_error(parser, "GNU object alignment must be a power of two");
        return false;
    }
    if (alignment > *explicit_alignment) {
        *explicit_alignment = alignment;
    }
    return true;
}

'''
text = text.replace(anchor, helper + anchor, 1)
text = text.replace(
    '''                                        size_t section_capacity,
                                        size_t *section_name_length,
                                        bool *has_section) {
''',
    '''                                        size_t section_capacity,
                                        size_t *section_name_length,
                                        bool *has_section,
                                        size_t *explicit_alignment) {
''',
    1,
)
text = text.replace(
    '''    if (parser == NULL || attributes == NULL || section_name == NULL ||
        section_name_length == NULL || has_section == NULL) {
''',
    '''    if (parser == NULL || attributes == NULL || section_name == NULL ||
        section_name_length == NULL || has_section == NULL || explicit_alignment == NULL) {
''',
    1,
)
old = '''        if (descriptor->kind == MINIC_ATTRIBUTE_SECTION) {
            if (!decode_deferred_section_argument(parser,
                                                  attribute,
                                                  section_name,
                                                  section_capacity,
                                                  section_name_length,
                                                  has_section)) {
                return false;
            }
            continue;
        }
'''
new = old + '''        if (descriptor->kind == MINIC_ATTRIBUTE_ALIGNED) {
            if (!decode_deferred_alignment_argument(parser, attribute, explicit_alignment)) {
                return false;
            }
            continue;
        }
'''
if text.count(old) != 1:
    raise SystemExit("object attribute dispatch anchor mismatch")
text = text.replace(old, new, 1)
# Add state.
text = text.replace(
    '''    bool has_section;
    MinicSymbolVisibility visibility;
''',
    '''    bool has_section;
    size_t object_explicit_alignment;
    MinicSymbolVisibility visibility;
''',
    1,
)
text = text.replace(
    '''    has_section = false;
    visibility = MINIC_SYMBOL_VISIBILITY_DEFAULT;
''',
    '''    has_section = false;
    object_explicit_alignment = 0U;
    visibility = MINIC_SYMBOL_VISIBILITY_DEFAULT;
''',
    1,
)
# Both object dispatch call sites get the new out-parameter.
call_old = '''                                         &section_name_length,
                                         &has_section)) {
'''
call_new = '''                                         &section_name_length,
                                         &has_section,
                                         &object_explicit_alignment)) {
'''
if text.count(call_old) != 2:
    raise SystemExit(f"expected two object attribute calls, found {text.count(call_old)}")
text = text.replace(call_old, call_new)
# Static objects do not silently drop this new semantic attribute yet.
text = text.replace(
    '''        if (has_section || has_visibility) {
            minic_parser_error(parser,
                               "static object symbol attributes require explicit object semantics");
''',
    '''        if (has_section || has_visibility || object_explicit_alignment != 0U) {
            minic_parser_error(parser,
                               "static object symbol/layout attributes require explicit object semantics");
''',
    1,
)
# Carry alignment into extern declaration path; reject direct definitions that would otherwise drop it.
text = text.replace(
    '''                                                               section_name_length,
                                                               has_section,
                                                               visibility,
''',
    '''                                                               section_name_length,
                                                               has_section,
                                                               object_explicit_alignment,
                                                               visibility,
''',
    1,
)
text = text.replace(
    '''        if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
            return parse_visible_external_array(
''',
    '''        if (object_explicit_alignment != 0U) {
            minic_parser_error(parser,
                               "GNU object alignment on a definition requires prior extern semantics");
            return false;
        }
        if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
            return parse_visible_external_array(
''',
    1,
)
parser_function.write_text(text)

# Extern object entity merge persists the object-specific minimum alignment.
parser_global = root / "src/frontend/parser_global.c"
text = parser_global.read_text()
text = text.replace(
    '''                                            bool has_section,
                                            MinicSymbolVisibility visibility,
''',
    '''                                            bool has_section,
                                            size_t explicit_alignment,
                                            MinicSymbolVisibility visibility,
''',
    1,
)
text = text.replace(
    '''    if ((has_section && !minic_c0_global_object_set_section(
                            parser->program, object_id, section_name, section_name_length)) ||
        (has_visibility &&
''',
    '''    if ((has_section && !minic_c0_global_object_set_section(
                            parser->program, object_id, section_name, section_name_length)) ||
        (explicit_alignment != 0U &&
         !minic_c0_global_object_set_explicit_alignment(
             parser->program, object_id, explicit_alignment)) ||
        (has_visibility &&
''',
    1,
)
# Public after-head signature.
old_sig = '''                                                 const char *section_name,
                                                 size_t section_name_length,
                                                 bool has_section,
                                                 MinicSymbolVisibility visibility,
'''
new_sig = '''                                                 const char *section_name,
                                                 size_t section_name_length,
                                                 bool has_section,
                                                 size_t explicit_alignment,
                                                 MinicSymbolVisibility visibility,
'''
if text.count(old_sig) != 1:
    raise SystemExit("extern after-head definition signature mismatch")
text = text.replace(old_sig, new_sig, 1)
# Existing merge call.
text = text.replace(
    '''                                                 declarator_section_name_length,
                                                 declarator_has_section,
                                                 visibility,
''',
    '''                                                 declarator_section_name_length,
                                                 declarator_has_section,
                                                 explicit_alignment,
                                                 visibility,
''',
    1,
)
# New object creation persists alignment.
text = text.replace(
    '''                   (declarator_has_section &&
                    !minic_c0_global_object_set_section(parser->program,
                                                        object_id,
                                                        declarator_section_name,
                                                        declarator_section_name_length)) ||
                   (has_visibility && !minic_c0_global_object_set_visibility(
''',
    '''                   (declarator_has_section &&
                    !minic_c0_global_object_set_section(parser->program,
                                                        object_id,
                                                        declarator_section_name,
                                                        declarator_section_name_length)) ||
                   (explicit_alignment != 0U &&
                    !minic_c0_global_object_set_explicit_alignment(
                        parser->program, object_id, explicit_alignment)) ||
                   (has_visibility && !minic_c0_global_object_set_visibility(
''',
    1,
)
# Legacy extern parser has no deferred object alignment.
text = text.replace(
    '''                                                       section_name_length,
                                                       has_section,
                                                       MINIC_SYMBOL_VISIBILITY_DEFAULT,
''',
    '''                                                       section_name_length,
                                                       has_section,
                                                       0U,
                                                       MINIC_SYMBOL_VISIBILITY_DEFAULT,
''',
    1,
)
parser_global.write_text(text)

# Focused Linux-shape contract.
fixture = root / "tests/compiler/c0/gnu_object_alignment_attribute.c"
fixture.write_text(
    '''typedef unsigned long long u64;\n\n'''
    '''extern u64 __attribute__((__aligned__((1 << 6)), __section__(".data..cacheline_aligned"))) jiffies_64;\n'''
    '''extern unsigned long volatile __attribute__((__aligned__((1 << 6)), __section__(".data..cacheline_aligned"))) jiffies;\n'''
    '''u64 ordinary = 1;\n'''
    '''u64 jiffies_64 = 0;\n'''
    '''unsigned long volatile jiffies = 0;\n\n'''
    '''int main(void)\n'''
    '''{\n'''
    '''    return ordinary == 1 ? 0 : 1;\n'''
    '''}\n'''
)
runner = root / "tests/compiler/c0/run-gnu-object-alignment-attribute.sh"
runner.write_text(
    '''#!/bin/sh\n'''
    '''set -eu\n\n'''
    '''root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)\n'''
    '''minic=${MINIC:-"$root/build/debug/bin/minic"}\n'''
    '''host_cc=${HOST_CC:-${CC:-cc}}\n'''
    '''build_dir=${BUILD_DIR:-"$root/build/debug"}\n'''
    '''work="$build_dir/tests/compiler-c0-gnu-object-alignment-attribute"\n\n'''
    '''mkdir -p "$work"\n'''
    '''"$host_cc" -E -P -x c "$root/tests/compiler/c0/gnu_object_alignment_attribute.c" -o "$work/input.i"\n'''
    '''"$minic" -S "$work/input.i" -o "$work/output.s"\n\n'''
    '''awk '\n'''
    '''    /^  \.align 6$/ { align = 6; next }\n'''
    '''    /^  \.align 3$/ { align = 3; next }\n'''
    '''    /^jiffies_64:$/ { if (align == 6) j64 = 1; next }\n'''
    '''    /^jiffies:$/ { if (align == 6) j = 1; next }\n'''
    '''    /^ordinary:$/ { if (align == 3) ordinary = 1; next }\n'''
    '''    END { exit !(j64 && j && ordinary) }\n'''
    '''' "$work/output.s"\n'''
    '''test "$(grep -c '^\.section \.data\.\.cacheline_aligned$' "$work/output.s")" -eq 2\n\n'''
    '''cat >"$work/invalid.c" <<'EOF'\n'''
    '''extern int __attribute__((__aligned__(24))) invalid_alignment;\n'''
    '''EOF\n'''
    '''"$host_cc" -E -P -x c "$work/invalid.c" -o "$work/invalid.i"\n'''
    '''if "$minic" -S "$work/invalid.i" -o "$work/invalid.s" 2>"$work/invalid.stderr"; then\n'''
    '''    printf '%s\\n' "FAIL compiler/c0/gnu_object_alignment_attribute: non-power-of-two alignment accepted" >&2\n'''
    '''    exit 1\n'''
    '''fi\n'''
    '''grep -F "GNU object alignment must be a power of two" "$work/invalid.stderr" >/dev/null\n\n'''
    '''printf '%s\\n' "PASS compiler/c0/gnu_object_alignment_attribute ownership=object alignment=64 section=preserved type-contamination=none invalid=reject"\n'''
)
runner.chmod(0o755)
focused = root / "tools/dev/pr76-focused.sh"
text = focused.read_text()
anchor = "sh tests/compiler/c0/run-gnu-top-level-empty-declaration.sh\n"
if text.count(anchor) != 1:
    raise SystemExit("focused registration anchor mismatch")
focused.write_text(text.replace(anchor, anchor + "sh tests/compiler/c0/run-gnu-object-alignment-attribute.sh\n", 1))

print("PASS generated object alignment semantic slice")
