#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one replacement, found {count}: {old[:120]!r}")
    target.write_text(text.replace(old, new, 1))


# Reuse the symbol-visibility model for objects as well as functions.
replace_once(
    "src/frontend/ast.h",
    "    size_t alignment;\n    bool is_internal;\n    bool is_read_only;\n",
    "    size_t alignment;\n    MinicSymbolVisibility visibility;\n    bool is_internal;\n    bool is_read_only;\n",
)
replace_once(
    "src/frontend/ast.h",
    "bool minic_c0_global_object_set_extern(MinicC0Program *program,\n                                       MinicGlobalObjectId global_object_id);\n",
    "bool minic_c0_global_object_set_extern(MinicC0Program *program,\n                                       MinicGlobalObjectId global_object_id);\n"
    "bool minic_c0_global_object_set_visibility(MinicC0Program *program,\n"
    "                                           MinicGlobalObjectId global_object_id,\n"
    "                                           MinicSymbolVisibility visibility);\n",
)

ast_global = Path("src/frontend/ast_global.c")
text = ast_global.read_text()
text += r'''

bool minic_c0_global_object_set_visibility(MinicC0Program *program,
                                           MinicGlobalObjectId global_object_id,
                                           MinicSymbolVisibility visibility) {
    MinicGlobalObject *object;

    if (program == NULL || global_object_id >= program->global_object_count ||
        visibility < MINIC_SYMBOL_VISIBILITY_DEFAULT ||
        visibility > MINIC_SYMBOL_VISIBILITY_PROTECTED) {
        return false;
    }
    object = &program->global_objects[global_object_id];
    if (object->visibility != MINIC_SYMBOL_VISIBILITY_DEFAULT &&
        object->visibility != visibility) {
        return false;
    }
    object->visibility = visibility;
    return true;
}
'''
ast_global.write_text(text)

# An attributed top-level declaration currently enters parse_function so it can inspect the
# declarator. Distinguish a real array declaration (];) from an array definition (= ...),
# preserving the same object and visibility metadata for later completion in this TU.
parser = Path("src/frontend/parser_function.c")
text = parser.read_text()
marker = "static bool parse_function(MinicParser *parser, bool is_internal) {\n"
helper = r'''static bool parse_visible_external_array(MinicParser *parser,
                                         MinicType element_type,
                                         MinicSourceSpan name_span,
                                         MinicSymbolVisibility visibility,
                                         bool has_visibility) {
    MinicParser probe;
    bool is_declaration;

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
    is_declaration = probe.current.kind == MINIC_TOKEN_SEMICOLON;

    if (is_declaration) {
        MinicGlobalObjectId object_id;
        MinicType array_type;
        size_t element_count;
        bool incomplete;

        if (!minic_parser_advance(parser)) {
            return false;
        }
        incomplete = parser->current.kind == MINIC_TOKEN_RBRACKET;
        if (incomplete) {
            if (!minic_c0_program_add_incomplete_array_type(parser->program, element_type, &array_type) ||
                !minic_parser_advance(parser)) {
                minic_parser_error(parser, "cannot declare visible incomplete extern array");
                return false;
            }
        } else if (!minic_parser_parse_fixed_array_bound(parser, &element_count) ||
                   !minic_c0_program_add_array_type(
                       parser->program, element_type, element_count, &array_type)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot declare visible fixed extern array");
            }
            return false;
        }
        if (minic_parser_find_global_object(parser, name_span) != MINIC_GLOBAL_OBJECT_INVALID ||
            !minic_c0_program_add_global_object(parser->program,
                                                parser->source + name_span.begin.offset,
                                                minic_parser_span_length(name_span),
                                                array_type,
                                                false,
                                                minic_type_is_const(element_type),
                                                &object_id) ||
            !minic_c0_global_object_set_extern(parser->program, object_id) ||
            (has_visibility &&
             !minic_c0_global_object_set_visibility(parser->program, object_id, visibility))) {
            minic_parser_error(parser, "cannot record visible extern array declaration");
            return false;
        }
        return minic_parser_expect(
            parser, MINIC_TOKEN_SEMICOLON, "expected ';' after visible extern array declaration");
    }

    if (!parse_external_integer_array_definition(parser, element_type, name_span)) {
        return false;
    }
    if (has_visibility) {
        MinicGlobalObjectId object_id;

        object_id = minic_parser_find_global_object(parser, name_span);
        if (object_id == MINIC_GLOBAL_OBJECT_INVALID ||
            !minic_c0_global_object_set_visibility(parser->program, object_id, visibility)) {
            minic_parser_error(parser, "cannot record visible external array definition");
            return false;
        }
    }
    return true;
}

'''
if text.count(marker) != 1:
    raise SystemExit(f"visible array helper marker: expected 1 match, found {text.count(marker)}")
text = text.replace(marker, helper + marker, 1)
parser.write_text(text)

replace_once(
    "src/frontend/parser_function.c",
    "        if (parser->current.kind == MINIC_TOKEN_LBRACKET) {\n"
    "            return parse_external_integer_array_definition(parser, return_type, name_span);\n"
    "        }\n",
    "        if (parser->current.kind == MINIC_TOKEN_LBRACKET) {\n"
    "            return parse_visible_external_array(\n"
    "                parser, return_type, name_span, visibility, has_visibility);\n"
    "        }\n",
)

# Emit ELF visibility for actual object definitions. Pure extern declarations are skipped by
# the global-object emitter, but their metadata survives if the declaration is completed later.
codegen = Path("src/target/riscv64/codegen_function.c")
text = codegen.read_text()
old = '''    if (!object->is_internal && fprintf(file, ".globl %s\\n", object->name) < 0) {
        return false;
    }
    if (fprintf(file,
'''
new = '''    if (!object->is_internal) {
        if (fprintf(file, ".globl %s\\n", object->name) < 0) {
            return false;
        }
        if (object->visibility != MINIC_SYMBOL_VISIBILITY_DEFAULT) {
            const char *visibility_directive;

            visibility_directive = object->visibility == MINIC_SYMBOL_VISIBILITY_HIDDEN ? ".hidden"
                                   : object->visibility == MINIC_SYMBOL_VISIBILITY_INTERNAL ? ".internal"
                                   : object->visibility == MINIC_SYMBOL_VISIBILITY_PROTECTED ? ".protected"
                                                                                             : NULL;
            if (visibility_directive == NULL ||
                fprintf(file, "%s %s\\n", visibility_directive, object->name) < 0) {
                return false;
            }
        }
    }
    if (fprintf(file,
'''
if text.count(old) != 1:
    raise SystemExit(f"global visibility codegen anchor: expected 1 match, found {text.count(old)}")
codegen.write_text(text.replace(old, new, 1))

print("staged visible extern array declarations and object ELF visibility")
