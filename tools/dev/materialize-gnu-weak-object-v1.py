from pathlib import Path


def replace_once(path, old, new, label):
    p = Path(path)
    text = p.read_text()
    if text.count(old) != 1:
        raise SystemExit(f'{label}: expected one anchor, got {text.count(old)}')
    p.write_text(text.replace(old, new, 1))

# Registry: weak is a symbol attribute for externally-linked objects too.
replace_once(
    'src/frontend/attribute.c',
    '''    MINIC_ATTRIBUTE_ENTRY("weak",\n                          MINIC_ATTRIBUTE_WEAK,\n                          MINIC_ATTRIBUTE_CLASS_SYMBOL,\n                          MINIC_ATTRIBUTE_TARGET_FUNCTION),\n    MINIC_ATTRIBUTE_ENTRY("__weak__",\n                          MINIC_ATTRIBUTE_WEAK,\n                          MINIC_ATTRIBUTE_CLASS_SYMBOL,\n                          MINIC_ATTRIBUTE_TARGET_FUNCTION),\n''',
    '''    MINIC_ATTRIBUTE_ENTRY("weak",\n                          MINIC_ATTRIBUTE_WEAK,\n                          MINIC_ATTRIBUTE_CLASS_SYMBOL,\n                          MINIC_ATTRIBUTE_TARGET_FUNCTION | MINIC_ATTRIBUTE_TARGET_OBJECT),\n    MINIC_ATTRIBUTE_ENTRY("__weak__",\n                          MINIC_ATTRIBUTE_WEAK,\n                          MINIC_ATTRIBUTE_CLASS_SYMBOL,\n                          MINIC_ATTRIBUTE_TARGET_FUNCTION | MINIC_ATTRIBUTE_TARGET_OBJECT),\n''',
    'weak registry')

# AST entity owns symbol binding semantics.
replace_once(
    'src/frontend/ast.h',
    '''    MinicSymbolVisibility visibility;\n    bool is_internal;\n    bool is_read_only;\n''',
    '''    MinicSymbolVisibility visibility;\n    bool is_internal;\n    bool is_weak;\n    bool is_read_only;\n''',
    'global weak field')
replace_once(
    'src/frontend/ast.h',
    '''bool minic_c0_global_object_set_visibility(MinicC0Program *program,\n''',
    '''bool minic_c0_global_object_set_weak(MinicC0Program *program,\n                                           MinicGlobalObjectId global_object_id,\n                                           bool is_weak);\nbool minic_c0_global_object_set_visibility(MinicC0Program *program,\n''',
    'global weak setter declaration')

replace_once(
    'src/frontend/ast_global.c',
    '''bool minic_c0_global_object_set_visibility(MinicC0Program *program,\n''',
    '''bool minic_c0_global_object_set_weak(MinicC0Program *program,\n                                           MinicGlobalObjectId global_object_id,\n                                           bool is_weak) {\n    MinicGlobalObject *object;\n\n    if (program == NULL || global_object_id >= program->global_object_count) {\n        return false;\n    }\n    object = &program->global_objects[global_object_id];\n    if (is_weak && object->is_internal) {\n        return false;\n    }\n    object->is_weak = is_weak;\n    return true;\n}\n\nbool minic_c0_global_object_set_visibility(MinicC0Program *program,\n''',
    'global weak setter implementation')

replace_once(
    'src/frontend/ast_verifier.c',
    '''            minic_type_is_function(object->type) ||\n            (minic_type_is_void(object->type) && !object->is_extern) ||\n''',
    '''            minic_type_is_function(object->type) || (object->is_internal && object->is_weak) ||\n            (minic_type_is_void(object->type) && !object->is_extern) ||\n''',
    'global weak verifier')

# Object attribute parser: existing object paths remain strict; only callers that
# explicitly request symbol metadata may consume weak.
replace_once(
    'src/frontend/parser_attribute.c',
    '''    MinicSymbolVisibility *visibility;\n    bool *has_visibility;\n} MinicObjectAttributeContext;\n''',
    '''    MinicSymbolVisibility *visibility;\n    bool *has_visibility;\n    bool *is_weak;\n} MinicObjectAttributeContext;\n''',
    'object attribute weak context')
replace_once(
    'src/frontend/parser_attribute.c',
    '''    if (descriptor->kind == MINIC_ATTRIBUTE_SECTION) {\n''',
    '''    if (descriptor->kind == MINIC_ATTRIBUTE_WEAK && context->is_weak != NULL) {\n        *context->is_weak = true;\n        return true;\n    }\n    if (descriptor->kind == MINIC_ATTRIBUTE_SECTION) {\n''',
    'consume weak object attribute')
replace_once(
    'src/frontend/parser_attribute.c',
    '''    context->visibility = NULL;\n    context->has_visibility = NULL;\n    return true;\n''',
    '''    context->visibility = NULL;\n    context->has_visibility = NULL;\n    context->is_weak = NULL;\n    return true;\n''',
    'initialize weak object context')

anchor = '''bool minic_parser_apply_alignment_attribute(MinicParser *parser,\n'''
new_function = '''bool minic_parser_parse_gnu_object_attribute_lists_with_symbol_metadata(\n    MinicParser *parser,\n    char *section_name,\n    size_t section_capacity,\n    size_t *section_name_length,\n    bool *has_section,\n    size_t *explicit_alignment,\n    MinicSymbolVisibility *visibility,\n    bool *has_visibility,\n    bool *is_weak) {\n    MinicObjectAttributeContext context;\n\n    if (parser == NULL || visibility == NULL || has_visibility == NULL || is_weak == NULL ||\n        !initialize_object_attribute_context(&context,\n                                             section_name,\n                                             section_capacity,\n                                             section_name_length,\n                                             has_section,\n                                             explicit_alignment)) {\n        return false;\n    }\n    context.visibility = visibility;\n    context.has_visibility = has_visibility;\n    context.is_weak = is_weak;\n    return minic_parser_parse_gnu_attribute_lists(parser, consume_object_attribute, &context);\n}\n\n'''
replace_once('src/frontend/parser_attribute.c', anchor, new_function + anchor, 'symbol metadata parser')

replace_once(
    'src/frontend/parser_internal.h',
    '''bool minic_parser_apply_alignment_attribute(MinicParser *parser,\n''',
    '''bool minic_parser_parse_gnu_object_attribute_lists_with_symbol_metadata(\n    MinicParser *parser,\n    char *section_name,\n    size_t section_capacity,\n    size_t *section_name_length,\n    bool *has_section,\n    size_t *explicit_alignment,\n    MinicSymbolVisibility *visibility,\n    bool *has_visibility,\n    bool *is_weak);\nbool minic_parser_apply_alignment_attribute(MinicParser *parser,\n''',
    'symbol metadata parser declaration')

# External-linkage object declarators consume weak and persist it monotonically
# onto the canonical global-object entity.
p = Path('src/frontend/parser_global.c')
text = p.read_text()
old = '''        bool declarator_has_visibility;\n        bool is_array;\n'''
new = '''        bool declarator_has_visibility;\n        bool declarator_is_weak;\n        bool is_array;\n'''
if text.count(old) != 1:
    raise SystemExit(f'extern declarator weak local: {text.count(old)}')
text = text.replace(old, new, 1)
old = '''        declarator_has_visibility = has_visibility;\n        (void)memset(declarator_section_name, 0, sizeof(declarator_section_name));\n'''
new = '''        declarator_has_visibility = has_visibility;\n        declarator_is_weak = false;\n        (void)memset(declarator_section_name, 0, sizeof(declarator_section_name));\n'''
if text.count(old) != 1:
    raise SystemExit(f'extern declarator weak init: {text.count(old)}')
text = text.replace(old, new, 1)
old_call = '''        if (!minic_parser_parse_gnu_object_attribute_lists_with_visibility(\n                parser,\n                declarator_section_name,\n                sizeof(declarator_section_name),\n                &declarator_section_name_length,\n                &declarator_has_section,\n                &declarator_explicit_alignment,\n                &declarator_visibility,\n                &declarator_has_visibility)) {\n'''
new_call = '''        if (!minic_parser_parse_gnu_object_attribute_lists_with_symbol_metadata(\n                parser,\n                declarator_section_name,\n                sizeof(declarator_section_name),\n                &declarator_section_name_length,\n                &declarator_has_section,\n                &declarator_explicit_alignment,\n                &declarator_visibility,\n                &declarator_has_visibility,\n                &declarator_is_weak)) {\n'''
if text.count(old_call) != 1:
    raise SystemExit(f'first extern object attr call: {text.count(old_call)}')
text = text.replace(old_call, new_call, 1)
old_call2 = '''            !minic_parser_parse_gnu_object_attribute_lists_with_visibility(\n                parser,\n                declarator_section_name,\n                sizeof(declarator_section_name),\n                &declarator_section_name_length,\n                &declarator_has_section,\n                &declarator_explicit_alignment,\n                &declarator_visibility,\n                &declarator_has_visibility)) {\n'''
new_call2 = '''            !minic_parser_parse_gnu_object_attribute_lists_with_symbol_metadata(\n                parser,\n                declarator_section_name,\n                sizeof(declarator_section_name),\n                &declarator_section_name_length,\n                &declarator_has_section,\n                &declarator_explicit_alignment,\n                &declarator_visibility,\n                &declarator_has_visibility,\n                &declarator_is_weak)) {\n'''
if text.count(old_call2) != 1:
    raise SystemExit(f'second extern object attr call: {text.count(old_call2)}')
text = text.replace(old_call2, new_call2, 1)
old = '''        parser->program->global_objects[object_id].is_block_scope_extern_only = false;\n\n        if (parser->current.kind != MINIC_TOKEN_COMMA) {\n'''
new = '''        if (declarator_is_weak &&\n            !minic_c0_global_object_set_weak(parser->program, object_id, true)) {\n            minic_parser_error(parser, "GNU weak requires external object linkage");\n            return false;\n        }\n        parser->program->global_objects[object_id].is_block_scope_extern_only = false;\n\n        if (parser->current.kind != MINIC_TOKEN_COMMA) {\n'''
if text.count(old) != 1:
    raise SystemExit(f'persist weak object: {text.count(old)}')
text = text.replace(old, new, 1)
p.write_text(text)

# RV64 symbol envelope: weak definitions replace .globl; weak extern declarations
# still need a .weak directive even though no storage is emitted.
replace_once(
    'src/target/riscv64/codegen_function.c',
    '''    if (!object->is_internal) {\n        if (fprintf(file, ".globl %s\\n", object->name) < 0) {\n''',
    '''    if (!object->is_internal) {\n        if (fprintf(file, object->is_weak ? ".weak %s\\n" : ".globl %s\\n", object->name) < 0) {\n''',
    'weak global definition emission')
replace_once(
    'src/target/riscv64/codegen_function.c',
    '''        if (program->global_objects[global_index].is_extern) {\n            continue;\n        }\n''',
    '''        if (program->global_objects[global_index].is_extern) {\n            const MinicGlobalObject *object;\n\n            object = &program->global_objects[global_index];\n            if (object->is_weak && !object->is_internal &&\n                fprintf(file, ".weak %s\\n", object->name) < 0) {\n                success = false;\n            }\n            continue;\n        }\n''',
    'weak extern object emission')
