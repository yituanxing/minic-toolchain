from pathlib import Path


def replace_once(path, old, new, label):
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label}: expected one anchor, got {count}')
    p.write_text(text.replace(old, new, 1))

replace_once(
    'src/frontend/attribute.c',
    '''    MINIC_ATTRIBUTE_ENTRY("unused",\n                          MINIC_ATTRIBUTE_UNUSED,\n                          MINIC_ATTRIBUTE_CLASS_INFORMATIONAL,\n                          MINIC_ATTRIBUTE_TARGET_FUNCTION | MINIC_ATTRIBUTE_TARGET_OBJECT |\n                              MINIC_ATTRIBUTE_TARGET_TYPE | MINIC_ATTRIBUTE_TARGET_FIELD),\n    MINIC_ATTRIBUTE_ENTRY("__unused__",\n                          MINIC_ATTRIBUTE_UNUSED,\n                          MINIC_ATTRIBUTE_CLASS_INFORMATIONAL,\n                          MINIC_ATTRIBUTE_TARGET_FUNCTION | MINIC_ATTRIBUTE_TARGET_OBJECT |\n                              MINIC_ATTRIBUTE_TARGET_TYPE | MINIC_ATTRIBUTE_TARGET_FIELD),\n''',
    '''    MINIC_ATTRIBUTE_ENTRY("unused",\n                          MINIC_ATTRIBUTE_UNUSED,\n                          MINIC_ATTRIBUTE_CLASS_INFORMATIONAL,\n                          MINIC_ATTRIBUTE_TARGET_FUNCTION | MINIC_ATTRIBUTE_TARGET_OBJECT |\n                              MINIC_ATTRIBUTE_TARGET_TYPE | MINIC_ATTRIBUTE_TARGET_FIELD |\n                              MINIC_ATTRIBUTE_TARGET_STATEMENT),\n    MINIC_ATTRIBUTE_ENTRY("__unused__",\n                          MINIC_ATTRIBUTE_UNUSED,\n                          MINIC_ATTRIBUTE_CLASS_INFORMATIONAL,\n                          MINIC_ATTRIBUTE_TARGET_FUNCTION | MINIC_ATTRIBUTE_TARGET_OBJECT |\n                              MINIC_ATTRIBUTE_TARGET_TYPE | MINIC_ATTRIBUTE_TARGET_FIELD |\n                              MINIC_ATTRIBUTE_TARGET_STATEMENT),\n''',
    'unused statement target')

replace_once(
    'src/frontend/parser_statement.c',
    '''typedef struct MinicStatementAttributeContext {\n    bool saw_fallthrough;\n} MinicStatementAttributeContext;\n''',
    '''typedef struct MinicStatementAttributeContext {\n    bool saw_fallthrough;\n    bool saw_informational;\n} MinicStatementAttributeContext;\n''',
    'statement attribute context')

replace_once(
    'src/frontend/parser_statement.c',
    '''    if (descriptor->kind != MINIC_ATTRIBUTE_FALLTHROUGH ||\n        descriptor->semantic_class != MINIC_ATTRIBUTE_CLASS_DIAGNOSTIC) {\n        minic_parser_error(parser, "GNU statement attribute semantics are not implemented");\n        return false;\n    }\n    context->saw_fallthrough = true;\n    return true;\n''',
    '''    if (descriptor->kind == MINIC_ATTRIBUTE_UNUSED &&\n        descriptor->semantic_class == MINIC_ATTRIBUTE_CLASS_INFORMATIONAL) {\n        /* MiniC has no unused-label diagnostic state yet. GNU unused at statement/label\n         * position is parse-time informational metadata with no runtime AST edge. */\n        context->saw_informational = true;\n        return true;\n    }\n    if (descriptor->kind == MINIC_ATTRIBUTE_FALLTHROUGH &&\n        descriptor->semantic_class == MINIC_ATTRIBUTE_CLASS_DIAGNOSTIC) {\n        context->saw_fallthrough = true;\n        return true;\n    }\n    minic_parser_error(parser, "GNU statement attribute semantics are not implemented");\n    return false;\n''',
    'statement attribute semantics')

replace_once(
    'src/frontend/parser_statement.c',
    '''    if (!minic_parser_parse_gnu_attribute_lists(parser, consume_statement_attribute, &context) ||\n        !context.saw_fallthrough ||\n        !minic_parser_expect(\n            parser, MINIC_TOKEN_SEMICOLON, "expected ';' after GNU statement attribute")) {\n''',
    '''    if (!minic_parser_parse_gnu_attribute_lists(parser, consume_statement_attribute, &context) ||\n        (!context.saw_fallthrough && !context.saw_informational) ||\n        !minic_parser_expect(\n            parser, MINIC_TOKEN_SEMICOLON, "expected ';' after GNU statement attribute")) {\n''',
    'statement attribute acceptance')

replace_once(
    'src/frontend/parser_statement.c',
    '''    if (parser->switch_depth == 0U) {\n        minic_parser_error(parser,\n                           "GNU fallthrough statement attribute requires an enclosing switch");\n        return false;\n    }\n''',
    '''    if (context.saw_fallthrough && parser->switch_depth == 0U) {\n        minic_parser_error(parser,\n                           "GNU fallthrough statement attribute requires an enclosing switch");\n        return false;\n    }\n''',
    'fallthrough-only control-flow validation')
