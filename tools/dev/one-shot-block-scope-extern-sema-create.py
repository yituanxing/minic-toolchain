from pathlib import Path

path = Path("src/frontend/parser_global.c")
text = path.read_text()
old = """    if (!minic_c0_program_add_global_object(parser->program,
                                            parser->source + name_span.begin.offset,
                                            minic_parser_span_length(name_span),
                                            object_type,
                                            false,
                                            minic_type_is_const(object_type),
                                            object_id) ||
        !minic_c0_global_object_set_extern(parser->program, *object_id)) {
        minic_parser_error(parser, "cannot declare block-scope extern object");
        return false;
    }
    parser->program->global_objects[*object_id].is_block_scope_extern_only = true;
    return true;
"""
new = """    {
        MinicDeclarationExternalObjectAttributes attributes;
        MinicDeclarationExternalObjectCreateStatus create_status;

        (void)memset(&attributes, 0, sizeof(attributes));
        attributes.visibility = MINIC_SYMBOL_VISIBILITY_DEFAULT;
        create_status = minic_declaration_create_external_object(
            parser->program,
            parser->source + name_span.begin.offset,
            minic_parser_span_length(name_span),
            object_type,
            minic_type_is_const(object_type),
            false,
            true,
            &attributes,
            object_id);
        if (create_status != MINIC_DECLARATION_EXTERNAL_OBJECT_CREATE_OK) {
            minic_parser_error(parser, "cannot declare block-scope extern object");
            return false;
        }
    }
    return true;
"""
if text.count(old) != 1:
    raise SystemExit("block-scope extern creation block is not unique")
path.write_text(text.replace(old, new, 1))
