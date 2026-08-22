from pathlib import Path

path = Path("src/frontend/parser_global.c")
text = path.read_text()

local_anchor = """        bool declarator_is_weak;
        bool is_array;
"""
local_replacement = """        bool declarator_is_weak;
        bool object_was_existing;
        bool is_array;
"""
if text.count(local_anchor) != 1:
    raise SystemExit("file-scope extern local anchor is not unique")
text = text.replace(local_anchor, local_replacement, 1)

old = """        object_id = minic_parser_find_global_object_entity(parser, name_span);
        if (object_id != MINIC_GLOBAL_OBJECT_INVALID) {
            if (!merge_extern_object_declaration(parser,
                                                 object_id,
                                                 object_type,
                                                 declarator_section_name,
                                                 declarator_section_name_length,
                                                 declarator_has_section,
                                                 declarator_explicit_alignment,
                                                 declarator_visibility,
                                                 declarator_has_visibility)) {
                return false;
            }
            parser->program->array_type_count = array_type_begin;
        } else if (!minic_c0_program_add_extern_global_object(
                       parser->program,
                       parser->source + name_span.begin.offset,
                       minic_parser_span_length(name_span),
                       object_type,
                       minic_type_is_const(declarator_element_type),
                       &object_id) ||
                   (declarator_has_section &&
                    !minic_c0_global_object_set_section(parser->program,
                                                        object_id,
                                                        declarator_section_name,
                                                        declarator_section_name_length)) ||
                   (declarator_explicit_alignment != 0U &&
                    !minic_c0_global_object_set_explicit_alignment(
                        parser->program, object_id, declarator_explicit_alignment)) ||
                   (declarator_has_visibility &&
                    !minic_c0_global_object_set_visibility(
                        parser->program, object_id, declarator_visibility))) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
                minic_parser_error(parser, "cannot declare extern object");
            }
            return false;
        }
        parser->program->global_objects[object_id].is_block_scope_extern_only = false;
        if (declarator_is_weak &&
            !minic_c0_global_object_set_weak(parser->program, object_id, true)) {
            minic_parser_error(parser, "GNU weak requires external object linkage");
            return false;
        }
"""
new = """        object_id = minic_parser_find_global_object_entity(parser, name_span);
        object_was_existing = object_id != MINIC_GLOBAL_OBJECT_INVALID;
        if (object_was_existing) {
            if (!merge_extern_object_declaration(parser,
                                                 object_id,
                                                 object_type,
                                                 declarator_section_name,
                                                 declarator_section_name_length,
                                                 declarator_has_section,
                                                 declarator_explicit_alignment,
                                                 declarator_visibility,
                                                 declarator_has_visibility)) {
                return false;
            }
            parser->program->array_type_count = array_type_begin;
        } else {
            MinicDeclarationExternalObjectAttributes attributes;
            MinicDeclarationExternalObjectCreateStatus create_status;

            attributes.section_name = declarator_section_name;
            attributes.section_name_length = declarator_section_name_length;
            attributes.explicit_alignment = declarator_explicit_alignment;
            attributes.visibility = declarator_visibility;
            attributes.has_section = declarator_has_section;
            attributes.has_visibility = declarator_has_visibility;
            create_status = minic_declaration_create_external_object(
                parser->program,
                parser->source + name_span.begin.offset,
                minic_parser_span_length(name_span),
                object_type,
                minic_type_is_const(declarator_element_type),
                declarator_is_weak,
                false,
                &attributes,
                &object_id);
            if (create_status != MINIC_DECLARATION_EXTERNAL_OBJECT_CREATE_OK) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
                    minic_parser_error(parser, "cannot declare extern object");
                }
                return false;
            }
        }
        parser->program->global_objects[object_id].is_block_scope_extern_only = false;
        if (object_was_existing && declarator_is_weak &&
            !minic_c0_global_object_set_weak(parser->program, object_id, true)) {
            minic_parser_error(parser, "GNU weak requires external object linkage");
            return false;
        }
"""
if text.count(old) != 1:
    raise SystemExit("file-scope extern creation block is not unique")
text = text.replace(old, new, 1)
path.write_text(text)
