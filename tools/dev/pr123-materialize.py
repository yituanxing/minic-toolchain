from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"anchor mismatch {path}: {count}")
    p.write_text(text.replace(old, new, 1))


# GlobalObject owns the C file-scope declaration state explicitly.  Tentative
# is not represented as an eager zero initializer because a later full
# definition in the same translation unit must still be able to replace it.
replace_once(
    "src/frontend/ast.h",
    "    bool is_zero_initialized;\n    bool is_extern;\n",
    "    bool is_zero_initialized;\n    bool is_extern;\n    bool is_tentative;\n",
)
replace_once(
    "src/frontend/ast.h",
    '''bool minic_c0_program_add_extern_global_object(MinicC0Program *program,
                                               const char *name,
                                               size_t name_length,
                                               MinicType type,
                                               bool is_read_only,
                                               MinicGlobalObjectId *global_object_id);
''',
    '''bool minic_c0_program_add_extern_global_object(MinicC0Program *program,
                                               const char *name,
                                               size_t name_length,
                                               MinicType type,
                                               bool is_read_only,
                                               MinicGlobalObjectId *global_object_id);
bool minic_c0_program_add_tentative_global_object(MinicC0Program *program,
                                                  const char *name,
                                                  size_t name_length,
                                                  MinicType type,
                                                  bool is_internal,
                                                  bool is_read_only,
                                                  MinicGlobalObjectId *global_object_id);
bool minic_c0_global_object_merge_tentative(MinicC0Program *program,
                                            MinicGlobalObjectId global_object_id);
bool minic_c0_global_object_begin_definition(MinicC0Program *program,
                                             MinicGlobalObjectId global_object_id);
''',
)

# State transitions live with the entity, not in every parser path.
ast_global = Path("src/frontend/ast_global.c")
text = ast_global.read_text()
anchor = '''bool minic_c0_program_add_extern_global_object(MinicC0Program *program,
                                               const char *name,
                                               size_t name_length,
                                               MinicType type,
                                               bool is_read_only,
                                               MinicGlobalObjectId *global_object_id) {
    return add_global_object_entity(
        program, name, name_length, type, false, is_read_only, true, global_object_id);
}
'''
addition = anchor + r'''

bool minic_c0_program_add_tentative_global_object(MinicC0Program *program,
                                                  const char *name,
                                                  size_t name_length,
                                                  MinicType type,
                                                  bool is_internal,
                                                  bool is_read_only,
                                                  MinicGlobalObjectId *global_object_id) {
    if (!add_global_object_entity(program,
                                  name,
                                  name_length,
                                  type,
                                  is_internal,
                                  is_read_only,
                                  false,
                                  global_object_id)) {
        return false;
    }
    program->global_objects[*global_object_id].is_tentative = true;
    return true;
}

static bool global_object_has_definition_payload(const MinicGlobalObject *object) {
    return object != NULL &&
           (object->initializer_count != 0U || object->function_relocation_count != 0U ||
            object->object_relocation_count != 0U || object->is_zero_initialized);
}

bool minic_c0_global_object_merge_tentative(MinicC0Program *program,
                                            MinicGlobalObjectId global_object_id) {
    MinicGlobalObject *object;

    if (program == NULL || global_object_id >= program->global_object_count) {
        return false;
    }
    object = &program->global_objects[global_object_id];
    if (!object->is_extern && !object->is_tentative) {
        /* A tentative definition after a full definition is another declaration
         * of the same already-defined entity. */
        return true;
    }
    if (global_object_has_definition_payload(object)) {
        return false;
    }
    object->is_extern = false;
    object->is_tentative = true;
    return true;
}

bool minic_c0_global_object_begin_definition(MinicC0Program *program,
                                             MinicGlobalObjectId global_object_id) {
    MinicGlobalObject *object;

    if (program == NULL || global_object_id >= program->global_object_count) {
        return false;
    }
    object = &program->global_objects[global_object_id];
    if ((!object->is_extern && !object->is_tentative) ||
        global_object_has_definition_payload(object)) {
        return false;
    }
    object->is_extern = false;
    object->is_tentative = false;
    object->is_block_scope_extern_only = false;
    return true;
}
'''
if text.count(anchor) != 1:
    raise SystemExit(f"ast_global extern add anchor mismatch: {text.count(anchor)}")
text = text.replace(anchor, addition, 1)
# Payload writers must not accidentally materialize a still-tentative entity.
text = text.replace(
    "    if (object->is_zero_initialized || object->function_relocation_count != 0U) {\n",
    "    if (object->is_tentative || object->is_zero_initialized ||\n        object->function_relocation_count != 0U) {\n",
    1,
)
text = text.replace(
    "    if (object->initializer_count != 0U || object->function_relocation_count >= 8U) {\n",
    "    if (object->is_tentative || object->initializer_count != 0U ||\n        object->function_relocation_count >= 8U) {\n",
    1,
)
# set_extern must not erase a tentative definition.
old = '''    if (object->initializer_count != 0U || object->function_relocation_count != 0U ||
        object->object_relocation_count != 0U || object->is_zero_initialized ||
        object->is_internal) {
'''
new = '''    if (object->is_tentative || object->initializer_count != 0U ||
        object->function_relocation_count != 0U || object->object_relocation_count != 0U ||
        object->is_zero_initialized || object->is_internal) {
'''
if text.count(old) != 1:
    raise SystemExit(f"ast_global set_extern guard mismatch: {text.count(old)}")
text = text.replace(old, new, 1)
# object relocation and eager zero payload are also definition payload.
old = '''    if (object->initializer_count != 0U || object->function_relocation_count != 0U ||
        !grow_array((void **)&object->object_relocations,
'''
new = '''    if (object->is_tentative || object->initializer_count != 0U ||
        object->function_relocation_count != 0U ||
        !grow_array((void **)&object->object_relocations,
'''
if text.count(old) != 1:
    raise SystemExit(f"ast_global object relocation guard mismatch: {text.count(old)}")
text = text.replace(old, new, 1)
old = '''    if (object->initializer_count != 0U) {
        return false;
    }
    object->is_zero_initialized = true;
'''
new = '''    if (object->is_tentative || object->initializer_count != 0U) {
        return false;
    }
    object->is_zero_initialized = true;
'''
if text.count(old) != 1:
    raise SystemExit(f"ast_global zero guard mismatch: {text.count(old)}")
text = text.replace(old, new, 1)
ast_global.write_text(text)

# Verifier freezes the three mutually exclusive declaration states and keeps
# tentative entities free of eager definition payload.
verifier = Path("src/frontend/ast_verifier.c")
text = verifier.read_text()
old = '''            (object->is_extern &&
             (object->is_internal || object->is_zero_initialized ||
              object->initializer_count != 0U || object->function_relocation_count != 0U ||
              object->object_relocation_count != 0U)) ||
            (object->is_zero_initialized && object->initializer_count != 0U) ||
'''
new = '''            (object->is_extern &&
             (object->is_tentative || object->is_internal || object->is_zero_initialized ||
              object->initializer_count != 0U || object->function_relocation_count != 0U ||
              object->object_relocation_count != 0U)) ||
            (object->is_tentative &&
             (object->is_extern || object->is_zero_initialized ||
              object->initializer_count != 0U || object->function_relocation_count != 0U ||
              object->object_relocation_count != 0U)) ||
            (object->is_zero_initialized && object->initializer_count != 0U) ||
'''
if text.count(old) != 1:
    raise SystemExit(f"verifier global state anchor mismatch: {text.count(old)}")
verifier.write_text(text.replace(old, new, 1))

# Codegen runs after the full translation unit is parsed.  A GlobalObject still
# marked tentative at this point is exactly the C end-of-TU implicit zero
# definition, so it reuses the existing zero-storage emitter.
codegen = Path("src/target/riscv64/codegen_function.c")
text = codegen.read_text()
start = text.find("static bool minic_riscv64_emit_global_object(")
end = text.find("\nstatic bool minic_riscv64_emit_global_objects(", start)
if start < 0 or end < 0:
    raise SystemExit(f"global emitter region mismatch start={start} end={end}")
region = text[start:end]
if region.count("if (object->is_zero_initialized) {") != 1:
    raise SystemExit(
        f"global zero branch mismatch: {region.count('if (object->is_zero_initialized) {')}"
    )
region = region.replace(
    "if (object->is_zero_initialized) {",
    "if (object->is_zero_initialized || object->is_tentative) {",
    1,
)
codegen.write_text(text[:start] + region + text[end:])

# File-scope external-linkage object state and metadata helpers.
parser = Path("src/frontend/parser_function.c")
text = parser.read_text()
insert_at = text.find("static bool parse_external_object_definition(")
if insert_at < 0:
    raise SystemExit("external object definition helper missing")
helpers = r'''static bool apply_external_object_metadata(MinicParser *parser,
                                           MinicGlobalObjectId object_id,
                                           const char *section_name,
                                           size_t section_name_length,
                                           bool has_section,
                                           size_t explicit_alignment,
                                           MinicSymbolVisibility visibility,
                                           bool has_visibility) {
    if (parser == NULL || object_id == MINIC_GLOBAL_OBJECT_INVALID) {
        return false;
    }
    if ((has_section &&
         !minic_c0_global_object_set_section(
             parser->program, object_id, section_name, section_name_length)) ||
        (explicit_alignment != 0U &&
         !minic_c0_global_object_set_explicit_alignment(
             parser->program, object_id, explicit_alignment)) ||
        (has_visibility &&
         !minic_c0_global_object_set_visibility(parser->program, object_id, visibility))) {
        minic_parser_error(parser, "conflicting external object definition attributes");
        return false;
    }
    return true;
}

static bool parse_external_tentative_object(MinicParser *parser,
                                            MinicType object_type,
                                            MinicSourceSpan name_span,
                                            const char *section_name,
                                            size_t section_name_length,
                                            bool has_section,
                                            size_t explicit_alignment,
                                            MinicSymbolVisibility visibility,
                                            bool has_visibility) {
    MinicGlobalObjectId object_id;
    const MinicGlobalObject *existing;

    if (parser == NULL || parser->current.kind != MINIC_TOKEN_SEMICOLON ||
        !minic_c0_type_is_complete_object(parser->program, object_type)) {
        if (parser != NULL) {
            minic_parser_error(parser,
                               "external tentative definition requires a complete object type");
        }
        return false;
    }
    object_id = minic_parser_find_global_object_entity(parser, name_span);
    if (object_id == MINIC_GLOBAL_OBJECT_INVALID) {
        if (!minic_c0_program_add_tentative_global_object(
                parser->program,
                parser->source + name_span.begin.offset,
                minic_parser_span_length(name_span),
                object_type,
                false,
                minic_type_is_const(object_type),
                &object_id)) {
            minic_parser_error(parser, "cannot create external tentative definition");
            return false;
        }
    } else {
        existing = minic_c0_program_global_object(parser->program, object_id);
        if (existing == NULL || !minic_c0_types_compatible(parser->program, existing->type, object_type) ||
            !minic_c0_global_object_merge_tentative(parser->program, object_id)) {
            minic_parser_error(parser, "conflicting external tentative definition");
            return false;
        }
    }
    parser->program->global_objects[object_id].is_block_scope_extern_only = false;
    if (!apply_external_object_metadata(parser,
                                        object_id,
                                        section_name,
                                        section_name_length,
                                        has_section,
                                        explicit_alignment,
                                        visibility,
                                        has_visibility)) {
        return false;
    }
    return minic_parser_advance(parser);
}

'''
text = text[:insert_at] + helpers + text[insert_at:]

# Replace scalar/pointer full-definition helper so tentative/extern declarations
# can transition to one real definition and metadata has one persistence owner.
start = text.find("static bool parse_external_object_definition(")
end = text.find("\nstatic bool parse_visible_external_array(", start)
if start < 0 or end < 0:
    raise SystemExit(f"external object helper region mismatch start={start} end={end}")
old_region = text[start:end]
new_region = r'''static bool parse_external_object_definition(MinicParser *parser,
                                             MinicType object_type,
                                             MinicSourceSpan name_span,
                                             const char *section_name,
                                             size_t section_name_length,
                                             bool has_section,
                                             size_t explicit_alignment,
                                             MinicSymbolVisibility visibility,
                                             bool has_visibility) {
    MinicGlobalObjectId object_id;
    MinicGlobalObjectId target_id;
    MinicSourceSpan literal_span;
    MinicType literal_type;
    MinicType literal_pointer_type;
    const MinicArrayType *literal_array;
    const MinicGlobalObject *existing;

    if (parser == NULL || parser->current.kind != MINIC_TOKEN_EQUAL ||
        (!minic_type_is_integer(object_type) && !minic_type_is_pointer(object_type))) {
        minic_parser_error(parser, "unsupported external object definition");
        return false;
    }

    object_id = minic_parser_find_global_object_entity(parser, name_span);
    if (object_id == MINIC_GLOBAL_OBJECT_INVALID) {
        if (!minic_c0_program_add_global_object(parser->program,
                                                parser->source + name_span.begin.offset,
                                                minic_parser_span_length(name_span),
                                                object_type,
                                                false,
                                                minic_type_is_const(object_type),
                                                &object_id)) {
            minic_parser_error(parser, "cannot create external object definition");
            return false;
        }
    } else {
        existing = minic_c0_program_global_object(parser->program, object_id);
        if (existing == NULL || !minic_c0_types_compatible(parser->program, existing->type, object_type) ||
            !minic_c0_global_object_begin_definition(parser->program, object_id)) {
            minic_parser_error(parser, "conflicting external object definition");
            return false;
        }
    }
    if (!apply_external_object_metadata(parser,
                                        object_id,
                                        section_name,
                                        section_name_length,
                                        has_section,
                                        explicit_alignment,
                                        visibility,
                                        has_visibility) ||
        !minic_parser_advance(parser)) {
        return false;
    }
    if (minic_type_is_integer(object_type)) {
        int value;

        if (!minic_parser_parse_integer_value(parser, &value) ||
            !minic_c0_global_object_add_initializer(parser->program, object_id, value)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot record external integer initializer");
            }
            return false;
        }
        return minic_parser_expect(
            parser, MINIC_TOKEN_SEMICOLON, "expected ';' after external object definition");
    }

    if (parser->current.kind != MINIC_TOKEN_STRING_LITERAL ||
        !minic_parser_create_string_literal_object(
            parser, &target_id, &literal_type, &literal_span)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser,
                               "external pointer definition requires a string literal initializer");
        }
        return false;
    }
    literal_array = minic_c0_program_array_type(parser->program, literal_type.array_type_id);
    if (literal_array == NULL || !minic_type_is_array(literal_type) ||
        !minic_type_pointer_to(literal_array->element_type, &literal_pointer_type) ||
        !minic_type_assignment_compatible(object_type, literal_pointer_type) ||
        !minic_c0_global_object_set_zero_initialized(parser->program, object_id) ||
        !minic_c0_global_object_add_object_relocation(parser->program, object_id, 0U, target_id)) {
        minic_parser_error(parser, "external pointer initializer type mismatch");
        return false;
    }
    (void)literal_span;
    return minic_parser_expect(
        parser, MINIC_TOKEN_SEMICOLON, "expected ';' after external object definition");
}
'''
text = text[:start] + new_region + text[end:]

# Existing full array definitions may follow an extern/tentative declaration.
start = text.find("static bool parse_external_integer_array_definition(")
end = text.find("\nstatic bool apply_external_object_metadata(", start)
if start < 0 or end < 0:
    raise SystemExit(f"external array definition region mismatch start={start} end={end}")
region = text[start:end]
region = region.replace(
    "    bool definition_omits_bound;\n",
    "    bool definition_omits_bound;\n    bool reused_existing;\n",
    1,
)
region = region.replace(
    "    definition_omits_bound = false;\n",
    "    definition_omits_bound = false;\n    reused_existing = false;\n",
    1,
)
old = '''    } else {
        object = &parser->program->global_objects[object_id];
        if (!object->is_extern || !minic_type_is_array(object->type)) {
'''
new = '''    } else {
        object = &parser->program->global_objects[object_id];
        reused_existing = true;
        if ((!object->is_extern && !object->is_tentative) || !minic_type_is_array(object->type)) {
'''
if region.count(old) != 1:
    raise SystemExit(f"external array reuse guard mismatch: {region.count(old)}")
region = region.replace(old, new, 1)
old = '''    object = &parser->program->global_objects[object_id];
    if (!minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '=' after external array")) {
        return false;
    }
'''
new = '''    object = &parser->program->global_objects[object_id];
    if ((reused_existing &&
         !minic_c0_global_object_begin_definition(parser->program, object_id)) ||
        !minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '=' after external array")) {
        return false;
    }
'''
if region.count(old) != 1:
    raise SystemExit(f"external array begin definition mismatch: {region.count(old)}")
region = region.replace(old, new, 1)
region = region.replace("        object->is_extern = false;\n", "", 1)
region = region.replace("    object->is_extern = false;\n", "", 1)
text = text[:start] + region + text[end:]

# Replace the visible-array wrapper: a no-initializer fixed array is tentative,
# not an extern declaration.  Incomplete tentative arrays remain fail-closed.
start = text.find("static bool parse_visible_external_array(")
end = text.find("\ntypedef struct MinicParsedDeclarationPrefix", start)
if start < 0 or end < 0:
    raise SystemExit(f"visible array wrapper region mismatch start={start} end={end}")
new_array_wrapper = r'''static bool parse_visible_external_array(MinicParser *parser,
                                         MinicType element_type,
                                         MinicSourceSpan name_span,
                                         const char *section_name,
                                         size_t section_name_length,
                                         bool has_section,
                                         size_t explicit_alignment,
                                         MinicSymbolVisibility visibility,
                                         bool has_visibility) {
    MinicParser probe;
    bool is_tentative;

    if (parser == NULL || parser->current.kind != MINIC_TOKEN_LBRACKET) {
        return false;
    }

    probe = *parser;
    if (!minic_parser_advance(&probe)) {
        return false;
    }
    while (probe.current.kind != MINIC_TOKEN_RBRACKET && probe.current.kind != MINIC_TOKEN_EOF) {
        if (!minic_parser_advance(&probe)) {
            return false;
        }
    }
    if (probe.current.kind != MINIC_TOKEN_RBRACKET || !minic_parser_advance(&probe)) {
        return false;
    }
    is_tentative = probe.current.kind == MINIC_TOKEN_SEMICOLON;

    if (is_tentative) {
        MinicGlobalObjectId object_id;
        MinicGlobalObject *object;
        const MinicArrayType *existing_array;
        MinicType array_type;
        size_t element_count;

        if (!minic_parser_advance(parser)) {
            return false;
        }
        if (parser->current.kind == MINIC_TOKEN_RBRACKET) {
            minic_parser_error(
                parser, "incomplete external tentative array is not implemented yet");
            return false;
        }
        if (!minic_parser_parse_fixed_array_bound(parser, &element_count)) {
            return false;
        }
        object_id = minic_parser_find_global_object_entity(parser, name_span);
        if (object_id == MINIC_GLOBAL_OBJECT_INVALID) {
            if (!minic_c0_program_add_array_type(
                    parser->program, element_type, element_count, &array_type) ||
                !minic_c0_program_add_tentative_global_object(
                    parser->program,
                    parser->source + name_span.begin.offset,
                    minic_parser_span_length(name_span),
                    array_type,
                    false,
                    minic_type_is_const(element_type),
                    &object_id)) {
                minic_parser_error(parser, "cannot create fixed external tentative array");
                return false;
            }
        } else {
            object = &parser->program->global_objects[object_id];
            existing_array = minic_type_is_array(object->type)
                                 ? minic_c0_program_array_type(
                                       parser->program, object->type.array_type_id)
                                 : NULL;
            if (existing_array == NULL ||
                !minic_c0_types_compatible(
                    parser->program, existing_array->element_type, element_type) ||
                (existing_array->element_count != 0U &&
                 existing_array->element_count != element_count) ||
                (existing_array->element_count == 0U &&
                 !minic_c0_program_complete_array_type(
                     parser->program, object->type, element_count)) ||
                !minic_c0_global_object_merge_tentative(parser->program, object_id)) {
                minic_parser_error(parser, "conflicting fixed external tentative array");
                return false;
            }
        }
        parser->program->global_objects[object_id].is_block_scope_extern_only = false;
        if (!apply_external_object_metadata(parser,
                                            object_id,
                                            section_name,
                                            section_name_length,
                                            has_section,
                                            explicit_alignment,
                                            visibility,
                                            has_visibility) ||
            !minic_parser_expect(
                parser, MINIC_TOKEN_SEMICOLON, "expected ';' after external tentative array")) {
            return false;
        }
        return true;
    }

    if (!parse_external_integer_array_definition(parser, element_type, name_span)) {
        return false;
    }
    {
        MinicGlobalObjectId object_id;

        object_id = minic_parser_find_global_object_entity(parser, name_span);
        if (object_id == MINIC_GLOBAL_OBJECT_INVALID ||
            !apply_external_object_metadata(parser,
                                            object_id,
                                            section_name,
                                            section_name_length,
                                            has_section,
                                            explicit_alignment,
                                            visibility,
                                            has_visibility)) {
            minic_parser_error(parser, "cannot record visible external array definition metadata");
            return false;
        }
    }
    return true;
}
'''
text = text[:start] + new_array_wrapper + text[end:]

# Non-extern single object declarators also own suffix GNU attributes.  Extern
# multi-declarators keep their existing per-declarator collector untouched.
old_route = '''        if (is_extern_declaration) {
            return minic_parser_parse_extern_global_after_head(parser,
                                                               base_type,
                                                               return_type,
                                                               name_span,
                                                               section_name,
                                                               section_name_length,
                                                               has_section,
                                                               object_explicit_alignment,
                                                               visibility,
                                                               has_visibility);
        }
        if (object_explicit_alignment != 0U) {
            minic_parser_error(
                parser, "GNU object alignment on a definition requires prior extern semantics");
            return false;
        }
        if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
            return parse_visible_external_array(
                parser, return_type, name_span, visibility, has_visibility);
        }
        return parse_external_object_definition(parser, return_type, name_span);
'''
new_route = '''        if (is_extern_declaration) {
            return minic_parser_parse_extern_global_after_head(parser,
                                                               base_type,
                                                               return_type,
                                                               name_span,
                                                               section_name,
                                                               section_name_length,
                                                               has_section,
                                                               object_explicit_alignment,
                                                               visibility,
                                                               has_visibility);
        }
        if (!minic_parser_parse_gnu_object_attribute_lists(parser,
                                                           section_name,
                                                           sizeof(section_name),
                                                           &section_name_length,
                                                           &has_section,
                                                           &object_explicit_alignment)) {
            return false;
        }
        if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
            return parse_visible_external_array(parser,
                                                return_type,
                                                name_span,
                                                section_name,
                                                section_name_length,
                                                has_section,
                                                object_explicit_alignment,
                                                visibility,
                                                has_visibility);
        }
        if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {
            return parse_external_tentative_object(parser,
                                                   return_type,
                                                   name_span,
                                                   section_name,
                                                   section_name_length,
                                                   has_section,
                                                   object_explicit_alignment,
                                                   visibility,
                                                   has_visibility);
        }
        return parse_external_object_definition(parser,
                                                return_type,
                                                name_span,
                                                section_name,
                                                section_name_length,
                                                has_section,
                                                object_explicit_alignment,
                                                visibility,
                                                has_visibility);
'''
if text.count(old_route) != 1:
    raise SystemExit(f"external object route mismatch: {text.count(old_route)}")
text = text.replace(old_route, new_route, 1)
parser.write_text(text)

# Focused translation-unit state machine + exact Linux-tail shapes.
Path("tests/compiler/c0/external_tentative_definitions.c").write_text(r'''typedef _Bool bool;

bool early_boot_irqs_disabled;

enum system_states {
    SYSTEM_BOOTING = 0,
    SYSTEM_RUNNING = 1,
};

enum system_states system_state;

void (*late_time_init)(void);

char __attribute__((__section__(".init.data"))) boot_command_line[1024];
char *saved_command_line __attribute__((__section__(".data..ro_after_init")));

int repeated_tentative;
int repeated_tentative;

extern int extern_then_tentative;
int extern_then_tentative;

int tentative_then_extern;
extern int tentative_then_extern;

int tentative_then_full;
int tentative_then_full = 7;

int full_then_tentative = 9;
int full_then_tentative;

struct tentative_record {
    int first;
    long second;
};

struct tentative_record record_state;
''')
Path("tests/compiler/c0/invalid_external_tentative_incomplete_array.c").write_text(
    "int incomplete_tentative_array[];\n"
)
Path("tests/compiler/c0/invalid_external_tentative_redeclaration.c").write_text(
    "extern int conflicting_tentative;\nlong conflicting_tentative;\n"
)

Path("tests/compiler/c0/run-external-tentative-definitions.sh").write_text(r'''#!/usr/bin/env sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-$root/build/debug/bin/minic}
host_cc=${HOST_CC:-cc}
work=${BUILD_DIR:-$root/build/external-tentative-definitions}
mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/external_tentative_definitions.c" \
    -o "$work/external_tentative_definitions.i"
"$minic" -S "$work/external_tentative_definitions.i" \
    -o "$work/external_tentative_definitions.s"
assembly="$work/external_tentative_definitions.s"

grep -F 'early_boot_irqs_disabled:' "$assembly" >/dev/null
grep -F 'system_state:' "$assembly" >/dev/null
grep -F 'late_time_init:' "$assembly" >/dev/null
grep -F 'boot_command_line:' "$assembly" >/dev/null
grep -F 'saved_command_line:' "$assembly" >/dev/null
grep -F '.section .init.data' "$assembly" >/dev/null
grep -F '.section .data..ro_after_init' "$assembly" >/dev/null
grep -F '.zero 1024' "$assembly" >/dev/null
grep -F '.word 7' "$assembly" >/dev/null
grep -F '.word 9' "$assembly" >/dev/null

test "$(grep -c '^repeated_tentative:' "$assembly")" -eq 1
test "$(grep -c '^extern_then_tentative:' "$assembly")" -eq 1
test "$(grep -c '^tentative_then_extern:' "$assembly")" -eq 1
test "$(grep -c '^tentative_then_full:' "$assembly")" -eq 1
test "$(grep -c '^full_then_tentative:' "$assembly")" -eq 1

if "$minic" -S "$root/tests/compiler/c0/invalid_external_tentative_incomplete_array.c" \
    -o "$work/invalid-incomplete.s" 2>"$work/invalid-incomplete.stderr"; then
    printf '%s\n' 'incomplete tentative array unexpectedly accepted' >&2
    exit 1
fi
grep -F 'incomplete external tentative array is not implemented yet' \
    "$work/invalid-incomplete.stderr" >/dev/null

if "$minic" -S "$root/tests/compiler/c0/invalid_external_tentative_redeclaration.c" \
    -o "$work/invalid-redecl.s" 2>"$work/invalid-redecl.stderr"; then
    printf '%s\n' 'conflicting tentative redeclaration unexpectedly accepted' >&2
    exit 1
fi
grep -F 'conflicting external tentative definition' "$work/invalid-redecl.stderr" >/dev/null

printf '%s\n' 'PASS compiler/c0/external_tentative_definitions state=extern|tentative|defined zero=end-of-tu fixed-array=1 attrs=section suffix=1 incomplete-array=fail-closed'
''')

# Permanent formal gate hook next to the existing external scalar/object gates.
gate = Path(".github/scripts/compiler-c0-full-gate.sh")
text = gate.read_text()
anchor = '''external_cjson_frontier() {
'''
helper = r'''external_tentative_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \
    HOST_CC=cc \
    BUILD_DIR="$root/build/ci-external-tentative" \
        sh tests/compiler/c0/run-external-tentative-definitions.sh
}

'''
if text.count(anchor) != 1:
    raise SystemExit(f"formal gate helper anchor mismatch: {text.count(anchor)}")
text = text.replace(anchor, helper + anchor, 1)
phase_anchor = "start_gate switch-control-flow-focused switch_control_flow_focused\n"
if text.count(phase_anchor) != 1:
    raise SystemExit(f"formal gate phase anchor mismatch: {text.count(phase_anchor)}")
text = text.replace(
    phase_anchor,
    phase_anchor + "start_gate external-tentative-focused external_tentative_focused\n",
    1,
)
gate.write_text(text)
