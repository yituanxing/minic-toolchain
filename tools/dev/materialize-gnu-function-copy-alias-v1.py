#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]

# Attribute registry: copy is semantic, alias is symbol identity. Both require one argument.
h = root / "src/frontend/attribute.h"
text = h.read_text()
old = '''    MINIC_ATTRIBUTE_GNU_INLINE,\n    MINIC_ATTRIBUTE_WEAK,\n'''
new = '''    MINIC_ATTRIBUTE_GNU_INLINE,\n    MINIC_ATTRIBUTE_COPY,\n    MINIC_ATTRIBUTE_ALIAS,\n    MINIC_ATTRIBUTE_WEAK,\n'''
if text.count(old) != 1:
    raise SystemExit(f"attribute enum anchor count={text.count(old)}")
h.write_text(text.replace(old, new, 1))

c = root / "src/frontend/attribute.c"
text = c.read_text()
old = '''    MINIC_ATTRIBUTE_ENTRY("__gnu_inline__",\n                          MINIC_ATTRIBUTE_GNU_INLINE,\n                          MINIC_ATTRIBUTE_CLASS_LANGUAGE_SEMANTIC,\n                          MINIC_ATTRIBUTE_TARGET_FUNCTION),\n    MINIC_ATTRIBUTE_ENTRY("weak",\n'''
new = '''    MINIC_ATTRIBUTE_ENTRY("__gnu_inline__",\n                          MINIC_ATTRIBUTE_GNU_INLINE,\n                          MINIC_ATTRIBUTE_CLASS_LANGUAGE_SEMANTIC,\n                          MINIC_ATTRIBUTE_TARGET_FUNCTION),\n    {\n        "copy",\n        sizeof("copy") - 1U,\n        MINIC_ATTRIBUTE_COPY,\n        MINIC_ATTRIBUTE_CLASS_LANGUAGE_SEMANTIC,\n        MINIC_ATTRIBUTE_TARGET_FUNCTION,\n        1U,\n        1U,\n        true,\n    },\n    {\n        "__copy__",\n        sizeof("__copy__") - 1U,\n        MINIC_ATTRIBUTE_COPY,\n        MINIC_ATTRIBUTE_CLASS_LANGUAGE_SEMANTIC,\n        MINIC_ATTRIBUTE_TARGET_FUNCTION,\n        1U,\n        1U,\n        true,\n    },\n    {\n        "alias",\n        sizeof("alias") - 1U,\n        MINIC_ATTRIBUTE_ALIAS,\n        MINIC_ATTRIBUTE_CLASS_SYMBOL,\n        MINIC_ATTRIBUTE_TARGET_FUNCTION | MINIC_ATTRIBUTE_TARGET_OBJECT,\n        1U,\n        1U,\n        true,\n    },\n    {\n        "__alias__",\n        sizeof("__alias__") - 1U,\n        MINIC_ATTRIBUTE_ALIAS,\n        MINIC_ATTRIBUTE_CLASS_SYMBOL,\n        MINIC_ATTRIBUTE_TARGET_FUNCTION | MINIC_ATTRIBUTE_TARGET_OBJECT,\n        1U,\n        1U,\n        true,\n    },\n    MINIC_ATTRIBUTE_ENTRY("weak",\n'''
if text.count(old) != 1:
    raise SystemExit(f"attribute descriptor anchor count={text.count(old)}")
c.write_text(text.replace(old, new, 1))

# Function semantic entity owns alias identity by stable FunctionId.
h = root / "src/frontend/ast.h"
text = h.read_text()
old = '''    MinicBlockId body_block;\n    bool is_defined;\n'''
new = '''    MinicBlockId body_block;\n    MinicFunctionId alias_target;\n    bool is_defined;\n'''
if text.count(old) != 1:
    raise SystemExit(f"MinicFunction alias field anchor count={text.count(old)}")
text = text.replace(old, new, 1)
old = '''bool minic_c0_program_set_function_weak(MinicC0Program *program,\n                                        MinicFunctionId function_id,\n                                        bool is_weak);\n'''
new = old + '''bool minic_c0_program_set_function_alias(MinicC0Program *program,\n                                         MinicFunctionId function_id,\n                                         MinicFunctionId target_function_id);\n'''
if text.count(old) != 1:
    raise SystemExit(f"function alias prototype anchor count={text.count(old)}")
h.write_text(text.replace(old, new, 1))

c = root / "src/frontend/ast.c"
text = c.read_text()
old = '''    function.body_block = body_block;\n    function.is_defined = body_block != MINIC_BLOCK_INVALID;\n'''
new = '''    function.body_block = body_block;\n    function.alias_target = MINIC_FUNCTION_INVALID;\n    function.is_defined = body_block != MINIC_BLOCK_INVALID;\n'''
if text.count(old) != 1:
    raise SystemExit(f"function initialization anchor count={text.count(old)}")
text = text.replace(old, new, 1)
old = '''    function = &program->functions[function_id];\n    if (function->is_defined) {\n        return false;\n    }\n'''
new = '''    function = &program->functions[function_id];\n    if (function->is_defined || function->alias_target != MINIC_FUNCTION_INVALID) {\n        return false;\n    }\n'''
if text.count(old) != 1:
    raise SystemExit(f"define-function alias guard anchor count={text.count(old)}")
c.write_text(text.replace(old, new, 1))

c = root / "src/frontend/ast_function.c"
text = c.read_text()
anchor = '''bool minic_c0_program_set_function_variadic(MinicC0Program *program,\n'''
helper = '''bool minic_c0_program_set_function_alias(MinicC0Program *program,\n                                         MinicFunctionId function_id,\n                                         MinicFunctionId target_function_id) {\n    MinicFunction *function;\n\n    if (program == NULL || function_id >= program->function_count ||\n        target_function_id >= program->function_count || function_id == target_function_id) {\n        return false;\n    }\n    function = &program->functions[function_id];\n    if (function->is_defined) {\n        return false;\n    }\n    if (function->alias_target != MINIC_FUNCTION_INVALID) {\n        return function->alias_target == target_function_id;\n    }\n    function->alias_target = target_function_id;\n    return true;\n}\n\n'''
if text.count(anchor) != 1:
    raise SystemExit(f"ast_function alias helper anchor count={text.count(anchor)}")
c.write_text(text.replace(anchor, helper + anchor, 1))

# Parser: resolve copy(identifier) and alias("symbol") while attributes are consumed.
p = root / "src/frontend/parser_function.c"
text = p.read_text()
old = '''    bool *is_weak;\n    const char *unsupported_message;\n} MinicFunctionAttributeContext;\n'''
new = '''    bool *is_weak;\n    MinicFunctionId *alias_target;\n    const char *unsupported_message;\n} MinicFunctionAttributeContext;\n'''
if text.count(old) != 1:
    raise SystemExit(f"function attribute context anchor count={text.count(old)}")
text = text.replace(old, new, 1)

anchor = '''static bool consume_function_attribute(MinicParser *parser,\n'''
helpers = r'''static bool function_attribute_argument_token(MinicParser *parser,
                                              const MinicParsedAttribute *attribute,
                                              MinicTokenKind expected_kind,
                                              MinicSourceSpan *span) {
    MinicParser probe;

    if (parser == NULL || attribute == NULL || span == NULL || !attribute->has_arguments ||
        attribute->arguments_span.end.offset <= attribute->arguments_span.begin.offset + 1U) {
        return false;
    }
    probe = *parser;
    minic_lexer_initialize(&probe.lexer, parser->path, parser->source, parser->lexer.length);
    probe.lexer.cursor = attribute->arguments_span.begin.offset + 1U;
    probe.lexer.line = attribute->arguments_span.begin.line;
    probe.lexer.column = attribute->arguments_span.begin.column + 1U;
    if (!minic_parser_advance(&probe) || probe.current.kind != expected_kind) {
        return false;
    }
    *span = probe.current.span;
    if (!minic_parser_advance(&probe) || probe.current.kind != MINIC_TOKEN_RPAREN ||
        probe.current.span.end.offset != attribute->arguments_span.end.offset) {
        return false;
    }
    return true;
}

static bool resolve_function_copy_attribute(MinicParser *parser,
                                            const MinicParsedAttribute *attribute) {
    MinicSourceSpan target_span;
    MinicFunctionId target_id;

    if (!function_attribute_argument_token(
            parser, attribute, MINIC_TOKEN_IDENTIFIER, &target_span)) {
        minic_parser_error(parser, "GNU copy requires one function identifier");
        return false;
    }
    target_id = minic_parser_find_function(parser, target_span);
    if (target_id == MINIC_FUNCTION_INVALID ||
        minic_c0_program_function(parser->program, target_id) == NULL) {
        minic_parser_error(parser, "GNU copy requires a previously declared function");
        return false;
    }
    /* MiniC currently persists symbol/layout function attributes separately from
     * the optimization/diagnostic attributes that GCC copy propagates. GCC copy
     * explicitly excludes alias/visibility/weak, so validating source identity is
     * the complete semantic effect for the currently persisted copy-eligible set. */
    return true;
}

static bool resolve_function_alias_attribute(MinicParser *parser,
                                             const MinicParsedAttribute *attribute,
                                             MinicFunctionId *target_id) {
    MinicSourceSpan literal_span;
    const char *target_name;
    size_t target_length;
    size_t function_index;

    if (target_id == NULL ||
        !function_attribute_argument_token(
            parser, attribute, MINIC_TOKEN_STRING_LITERAL, &literal_span) ||
        literal_span.end.offset <= literal_span.begin.offset + 1U) {
        minic_parser_error(parser, "GNU alias requires one string literal target");
        return false;
    }
    target_name = parser->source + literal_span.begin.offset + 1U;
    target_length = literal_span.end.offset - literal_span.begin.offset - 2U;
    if (target_length == 0U || memchr(target_name, '\\', target_length) != NULL) {
        minic_parser_error(parser, "escaped or empty GNU alias targets are not supported");
        return false;
    }
    for (function_index = 0U; function_index < parser->program->function_count; ++function_index) {
        const MinicFunction *candidate;
        const char *symbol_name;

        candidate = minic_c0_program_function(parser->program, function_index);
        symbol_name = minic_c0_function_symbol_name(candidate);
        if (candidate != NULL && symbol_name != NULL && strlen(symbol_name) == target_length &&
            memcmp(symbol_name, target_name, target_length) == 0) {
            *target_id = function_index;
            return true;
        }
    }
    minic_parser_error(parser, "GNU alias target must be declared in this translation unit");
    return false;
}

'''
if text.count(anchor) != 1:
    raise SystemExit(f"function attribute helper insertion count={text.count(anchor)}")
text = text.replace(anchor, helpers + anchor, 1)

old = '''    if (descriptor->kind == MINIC_ATTRIBUTE_SECTION) {\n'''
new = '''    if (descriptor->kind == MINIC_ATTRIBUTE_COPY) {\n        return resolve_function_copy_attribute(parser, attribute);\n    }\n\n    if (descriptor->kind == MINIC_ATTRIBUTE_ALIAS) {\n        MinicFunctionId target_id;\n\n        if (context->alias_target == NULL ||\n            !resolve_function_alias_attribute(parser, attribute, &target_id)) {\n            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {\n                minic_parser_error(parser, "GNU alias requires a function declaration entity");\n            }\n            return false;\n        }\n        if (*context->alias_target != MINIC_FUNCTION_INVALID &&\n            *context->alias_target != target_id) {\n            minic_parser_error(parser, "conflicting GNU function alias attributes");\n            return false;\n        }\n        *context->alias_target = target_id;\n        return true;\n    }\n\n    if (descriptor->kind == MINIC_ATTRIBUTE_SECTION) {\n'''
if text.count(old) != 1:
    raise SystemExit(f"function attribute dispatch anchor count={text.count(old)}")
text = text.replace(old, new, 1)

old = '''    context.is_weak = is_weak;\n    context.unsupported_message = unsupported_message;\n    return minic_parser_parse_gnu_attribute_lists(parser, consume_function_attribute, &context);\n'''
new = '''    context.is_weak = is_weak;\n    context.alias_target = NULL;\n    context.unsupported_message = unsupported_message;\n    return minic_parser_parse_gnu_attribute_lists(parser, consume_function_attribute, &context);\n'''
if text.count(old) != 1:
    raise SystemExit(f"stream attribute context anchor count={text.count(old)}")
text = text.replace(old, new, 1)

old_sig = '''                                          bool *is_weak,\n                                          const char *unsupported_message) {\n'''
new_sig = '''                                          bool *is_weak,\n                                          MinicFunctionId *alias_target,\n                                          const char *unsupported_message) {\n'''
if text.count(old_sig) != 1:
    raise SystemExit(f"apply attribute signature anchor count={text.count(old_sig)}")
text = text.replace(old_sig, new_sig, 1)
old = '''    context.is_weak = is_weak;\n    context.unsupported_message = unsupported_message;\n    for (index = 0U; index < attributes->count; ++index) {\n'''
new = '''    context.is_weak = is_weak;\n    context.alias_target = alias_target;\n    context.unsupported_message = unsupported_message;\n    for (index = 0U; index < attributes->count; ++index) {\n'''
if text.count(old) != 1:
    raise SystemExit(f"apply attribute context anchor count={text.count(old)}")
text = text.replace(old, new, 1)

old = '''static bool\nparse_persistent_function_attributes(MinicParser *parser, bool is_internal, bool *is_weak) {\n    return parse_function_attribute_lists(\n        parser,\n        false,\n        is_internal,\n        false,\n        is_weak,\n        "unsupported GNU function attribute; ABI/layout-affecting and unknown attributes must be "\n        "implemented explicitly");\n}\n'''
new = '''static bool parse_persistent_function_attributes(MinicParser *parser,\n                                                 bool is_internal,\n                                                 bool *is_weak,\n                                                 MinicFunctionId *alias_target) {\n    MinicFunctionAttributeContext context;\n\n    context.allow_gnu_inline = false;\n    context.is_internal = is_internal;\n    context.is_inline = false;\n    context.is_extern = false;\n    context.section_name = NULL;\n    context.section_capacity = 0U;\n    context.section_name_length = NULL;\n    context.has_section = NULL;\n    context.is_weak = is_weak;\n    context.alias_target = alias_target;\n    context.unsupported_message =\n        "unsupported GNU function attribute; ABI/layout-affecting and unknown attributes must be "\n        "implemented explicitly";\n    return minic_parser_parse_gnu_attribute_lists(parser, consume_function_attribute, &context);\n}\n'''
if text.count(old) != 1:
    raise SystemExit(f"persistent attribute helper anchor count={text.count(old)}")
text = text.replace(old, new, 1)

# Typed function declarators remain declaration-only; copy is accepted but alias stays fail-closed.
text = text.replace('''                &is_weak,\n                "unsupported GNU prefix function attribute; semantic and ABI-affecting attributes "\n''', '''                &is_weak,\n                NULL,\n                "unsupported GNU prefix function attribute; semantic and ABI-affecting attributes "\n''', 1)
text = text.replace('''                !parse_persistent_function_attributes(parser, is_internal, &entity_is_weak)) {\n''', '''                !parse_persistent_function_attributes(\n                    parser, is_internal, &entity_is_weak, NULL)) {\n''', 1)

# Normal function declarators retain alias target across prefix and suffix attributes.
old = '''    bool object_is_weak;\n\n    body_block = MINIC_BLOCK_INVALID;\n'''
new = '''    bool object_is_weak;\n    MinicFunctionId alias_target;\n\n    body_block = MINIC_BLOCK_INVALID;\n'''
if text.count(old) != 1:
    raise SystemExit(f"parse_function alias local anchor count={text.count(old)}")
text = text.replace(old, new, 1)
old = '''    object_is_weak = false;\n    (void)memset(assembler_name, 0, sizeof(assembler_name));\n'''
new = '''    object_is_weak = false;\n    alias_target = MINIC_FUNCTION_INVALID;\n    (void)memset(assembler_name, 0, sizeof(assembler_name));\n'''
if text.count(old) != 1:
    raise SystemExit(f"parse_function alias init anchor count={text.count(old)}")
text = text.replace(old, new, 1)

# The second apply call is the normal function path.
needle = '''            &is_weak,\n            "unsupported GNU prefix function attribute; semantic and ABI-affecting attributes must "\n'''
replacement = '''            &is_weak,\n            &alias_target,\n            "unsupported GNU prefix function attribute; semantic and ABI-affecting attributes must "\n'''
if text.count(needle) != 1:
    raise SystemExit(f"normal prefix alias anchor count={text.count(needle)}")
text = text.replace(needle, replacement, 1)
needle = '''        !parse_persistent_function_attributes(parser, is_internal, &is_weak)) {\n'''
replacement = '''        !parse_persistent_function_attributes(\n            parser, is_internal, &is_weak, &alias_target)) {\n'''
if text.count(needle) != 1:
    raise SystemExit(f"normal suffix alias anchor count={text.count(needle)}")
text = text.replace(needle, replacement, 1)

# Finish declaration with alias semantic validation and ownership.
old_sig = '''                                               const char *section_name,\n                                               size_t section_name_length,\n                                               bool has_section) {\n    if (parser == NULL || parser->current.kind != MINIC_TOKEN_SEMICOLON ||\n'''
new_sig = '''                                               const char *section_name,\n                                               size_t section_name_length,\n                                               bool has_section,\n                                               MinicFunctionId alias_target) {\n    MinicFunctionId function_id;\n    const MinicFunction *alias_function;\n\n    if (parser == NULL || parser->current.kind != MINIC_TOKEN_SEMICOLON ||\n'''
if text.count(old_sig) != 1:
    raise SystemExit(f"finish declaration signature anchor count={text.count(old_sig)}")
text = text.replace(old_sig, new_sig, 1)
old = '''                                            section_name,\n                                            section_name_length,\n                                            has_section)) {\n        return false;\n    }\n    return minic_parser_advance(parser);\n}\n'''
new = '''                                            section_name,\n                                            section_name_length,\n                                            has_section)) {\n        return false;\n    }\n    function_id = minic_parser_find_function(parser, name_span);\n    if (function_id == MINIC_FUNCTION_INVALID) {\n        return false;\n    }\n    if (alias_target != MINIC_FUNCTION_INVALID) {\n        alias_function = minic_c0_program_function(parser->program, alias_target);\n        if (alias_function == NULL || !alias_function->is_defined || has_section ||\n            !minic_parser_function_signature_matches(alias_function,\n                                                     return_type,\n                                                     parameter_types,\n                                                     parameter_count,\n                                                     is_variadic) ||\n            !minic_c0_program_set_function_alias(\n                parser->program, function_id, alias_target)) {\n            minic_parser_error(\n                parser,\n                "GNU function alias requires a defined same-TU target with matching signature");\n            return false;\n        }\n    }\n    return minic_parser_advance(parser);\n}\n'''
if text.count(old) != 1:
    raise SystemExit(f"finish declaration body anchor count={text.count(old)}")
text = text.replace(old, new, 1)

old = '''                                                  section_name,\n                                                  section_name_length,\n                                                  has_section);\n'''
new = '''                                                  section_name,\n                                                  section_name_length,\n                                                  has_section,\n                                                  alias_target);\n'''
if text.count(old) != 1:
    raise SystemExit(f"finish declaration call anchor count={text.count(old)}")
text = text.replace(old, new, 1)

old = '''    if (!minic_type_is_integer(return_type) && !minic_type_is_void(return_type) &&\n'''
new = '''    if (alias_target != MINIC_FUNCTION_INVALID) {\n        minic_parser_error(parser, "GNU alias applies to declarations, not function definitions");\n        return false;\n    }\n    if (!minic_type_is_integer(return_type) && !minic_type_is_void(return_type) &&\n'''
if text.count(old) != 1:
    raise SystemExit(f"alias definition guard anchor count={text.count(old)}")
text = text.replace(old, new, 1)
p.write_text(text)

# Verifier: aliases are declaration entities whose target is a real definition with the same type.
v = root / "src/frontend/ast_verifier.c"
text = v.read_text()
anchor = '''bool minic_c0_program_verify_target(const MinicC0Program *program,\n'''
helper = '''static bool function_alias_signature_matches(const MinicC0Program *program,\n                                           const MinicFunction *alias,\n                                           const MinicFunction *target) {\n    size_t parameter_index;\n\n    if (program == NULL || alias == NULL || target == NULL ||\n        alias->parameter_count != target->parameter_count ||\n        alias->is_variadic != target->is_variadic ||\n        !minic_c0_types_compatible(program, alias->return_type, target->return_type)) {\n        return false;\n    }\n    for (parameter_index = 0U; parameter_index < alias->parameter_count; ++parameter_index) {\n        if (!minic_c0_types_compatible(program,\n                                       alias->parameter_types[parameter_index],\n                                       target->parameter_types[parameter_index])) {\n            return false;\n        }\n    }\n    return true;\n}\n\n'''
if text.count(anchor) != 1:
    raise SystemExit(f"verifier alias helper anchor count={text.count(anchor)}")
text = text.replace(anchor, helper + anchor, 1)
old = '''        function = &program->functions[index];\n        if (function->name == NULL || function->parameter_count > MINIC_MAX_FUNCTION_PARAMETERS ||\n'''
new = '''        function = &program->functions[index];\n        if (function->alias_target != MINIC_FUNCTION_INVALID) {\n            const MinicFunction *alias_target_function;\n\n            alias_target_function =\n                minic_c0_program_function(program, function->alias_target);\n            if (alias_target_function == NULL || !alias_target_function->is_defined ||\n                function->is_defined || function->section_name != NULL ||\n                !function_alias_signature_matches(\n                    program, function, alias_target_function)) {\n                return false;\n            }\n        }\n        if (function->name == NULL || function->parameter_count > MINIC_MAX_FUNCTION_PARAMETERS ||\n'''
if text.count(old) != 1:
    raise SystemExit(f"verifier function alias anchor count={text.count(old)}")
v.write_text(text.replace(old, new, 1))

# RV64 writer emits aliases as symbol metadata plus .set; no synthetic body is created.
r = root / "src/target/riscv64/codegen_function.c"
text = r.read_text()
old = '''        if (!function->is_defined) {\n            const char *symbol_name;\n\n            symbol_name = minic_c0_function_symbol_name(function);\n            if (function->is_weak && !function->is_internal) {\n                success = symbol_name != NULL && symbol_name[0] != '\\0' &&\n                          fprintf(file, ".weak %s\\n", symbol_name) >= 0;\n            }\n            continue;\n        }\n'''
new = '''        if (!function->is_defined) {\n            const char *symbol_name;\n\n            symbol_name = minic_c0_function_symbol_name(function);\n            if (function->alias_target != MINIC_FUNCTION_INVALID) {\n                const MinicFunction *target_function;\n                const char *target_name;\n                const char *visibility_directive;\n\n                target_function =\n                    minic_c0_program_function(program, function->alias_target);\n                target_name = minic_c0_function_symbol_name(target_function);\n                success = symbol_name != NULL && symbol_name[0] != '\\0' &&\n                          target_function != NULL && target_function->is_defined &&\n                          target_name != NULL && target_name[0] != '\\0';\n                if (success && !function->is_internal) {\n                    success = fprintf(file,\n                                      function->is_weak ? ".weak %s\\n" : ".globl %s\\n",\n                                      symbol_name) >= 0;\n                    if (success &&\n                        function->visibility != MINIC_SYMBOL_VISIBILITY_DEFAULT) {\n                        visibility_directive =\n                            minic_riscv64_function_visibility_directive(function->visibility);\n                        success = visibility_directive != NULL &&\n                                  fprintf(file,\n                                          "%s %s\\n",\n                                          visibility_directive,\n                                          symbol_name) >= 0;\n                    }\n                }\n                if (success) {\n                    success = fprintf(file,\n                                      ".type %s, @function\\n"\n                                      ".set %s, %s\\n",\n                                      symbol_name,\n                                      symbol_name,\n                                      target_name) >= 0;\n                }\n                continue;\n            }\n            if (function->is_weak && !function->is_internal) {\n                success = symbol_name != NULL && symbol_name[0] != '\\0' &&\n                          fprintf(file, ".weak %s\\n", symbol_name) >= 0;\n            }\n            continue;\n        }\n'''
if text.count(old) != 1:
    raise SystemExit(f"rv64 undefined function alias anchor count={text.count(old)}")
r.write_text(text.replace(old, new, 1))
