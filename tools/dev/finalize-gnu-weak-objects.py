#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: anchor count={count}")
    p.write_text(text.replace(old, new, 1))


# Persist weak linkage on global objects and expose a narrow owner mutation API.
replace_once(
    "src/frontend/ast.h",
    "    MinicSymbolVisibility visibility;\n    bool is_internal;\n    bool is_read_only;\n",
    "    MinicSymbolVisibility visibility;\n    bool is_internal;\n    bool is_weak;\n    bool is_read_only;\n",
    "global object weak field",
)
replace_once(
    "src/frontend/ast.h",
    "bool minic_c0_global_object_set_extern(MinicC0Program *program,\n"
    "                                       MinicGlobalObjectId global_object_id);\n"
    "bool minic_c0_global_object_set_visibility(MinicC0Program *program,\n",
    "bool minic_c0_global_object_set_extern(MinicC0Program *program,\n"
    "                                       MinicGlobalObjectId global_object_id);\n"
    "bool minic_c0_global_object_set_weak(MinicC0Program *program,\n"
    "                                     MinicGlobalObjectId global_object_id,\n"
    "                                     bool is_weak);\n"
    "bool minic_c0_global_object_set_visibility(MinicC0Program *program,\n",
    "global object weak declaration",
)
replace_once(
    "src/frontend/ast_global.c",
    "    object->is_extern = true;\n"
    "    return true;\n"
    "}\n\n"
    "bool minic_c0_global_object_add_object_relocation_path_addend(\n",
    "    object->is_extern = true;\n"
    "    return true;\n"
    "}\n\n"
    "bool minic_c0_global_object_set_weak(MinicC0Program *program,\n"
    "                                     MinicGlobalObjectId global_object_id,\n"
    "                                     bool is_weak) {\n"
    "    MinicGlobalObject *object;\n\n"
    "    if (program == NULL || global_object_id >= program->global_object_count) {\n"
    "        return false;\n"
    "    }\n"
    "    object = &program->global_objects[global_object_id];\n"
    "    if (is_weak && object->is_internal) {\n"
    "        return false;\n"
    "    }\n"
    "    object->is_weak = is_weak;\n"
    "    return true;\n"
    "}\n\n"
    "bool minic_c0_global_object_add_object_relocation_path_addend(\n",
    "global object weak setter",
)
replace_once(
    "src/frontend/ast_verifier.c",
    "            minic_type_is_function(object->type) ||\n"
    "            (minic_type_is_void(object->type) && !object->is_extern) ||\n",
    "            minic_type_is_function(object->type) || (object->is_internal && object->is_weak) ||\n"
    "            (minic_type_is_void(object->type) && !object->is_extern) ||\n",
    "weak object verifier",
)

# GNU weak is a real object symbol attribute, not parse-only noise.
replace_once(
    "src/frontend/attribute.c",
    "    MINIC_ATTRIBUTE_ENTRY(\"weak\",\n"
    "                          MINIC_ATTRIBUTE_WEAK,\n"
    "                          MINIC_ATTRIBUTE_CLASS_SYMBOL,\n"
    "                          MINIC_ATTRIBUTE_TARGET_FUNCTION),\n"
    "    MINIC_ATTRIBUTE_ENTRY(\"__weak__\",\n"
    "                          MINIC_ATTRIBUTE_WEAK,\n"
    "                          MINIC_ATTRIBUTE_CLASS_SYMBOL,\n"
    "                          MINIC_ATTRIBUTE_TARGET_FUNCTION),\n",
    "    MINIC_ATTRIBUTE_ENTRY(\"weak\",\n"
    "                          MINIC_ATTRIBUTE_WEAK,\n"
    "                          MINIC_ATTRIBUTE_CLASS_SYMBOL,\n"
    "                          MINIC_ATTRIBUTE_TARGET_FUNCTION | MINIC_ATTRIBUTE_TARGET_OBJECT),\n"
    "    MINIC_ATTRIBUTE_ENTRY(\"__weak__\",\n"
    "                          MINIC_ATTRIBUTE_WEAK,\n"
    "                          MINIC_ATTRIBUTE_CLASS_SYMBOL,\n"
    "                          MINIC_ATTRIBUTE_TARGET_FUNCTION | MINIC_ATTRIBUTE_TARGET_OBJECT),\n",
    "weak object attribute registry",
)

replace_once(
    "src/frontend/parser_attribute.c",
    "    MinicSymbolVisibility *visibility;\n    bool *has_visibility;\n} MinicObjectAttributeContext;\n",
    "    MinicSymbolVisibility *visibility;\n    bool *has_visibility;\n    bool *is_weak;\n} MinicObjectAttributeContext;\n",
    "weak object attribute context",
)
replace_once(
    "src/frontend/parser_attribute.c",
    "    if (object_attribute_class_is_parse_only(descriptor->semantic_class)) {\n"
    "        return true;\n"
    "    }\n"
    "    if (descriptor->kind == MINIC_ATTRIBUTE_SECTION) {\n",
    "    if (object_attribute_class_is_parse_only(descriptor->semantic_class)) {\n"
    "        return true;\n"
    "    }\n"
    "    if (descriptor->kind == MINIC_ATTRIBUTE_WEAK) {\n"
    "        if (context->is_weak == NULL) {\n"
    "            minic_parser_error(parser, \"GNU weak object attribute requires symbol metadata ownership\");\n"
    "            return false;\n"
    "        }\n"
    "        *context->is_weak = true;\n"
    "        return true;\n"
    "    }\n"
    "    if (descriptor->kind == MINIC_ATTRIBUTE_SECTION) {\n",
    "consume weak object attribute",
)
replace_once(
    "src/frontend/parser_attribute.c",
    "    context->visibility = NULL;\n    context->has_visibility = NULL;\n    return true;\n",
    "    context->visibility = NULL;\n    context->has_visibility = NULL;\n    context->is_weak = NULL;\n    return true;\n",
    "initialize weak object metadata",
)

p = Path("src/frontend/parser_attribute.c")
text = p.read_text()
marker = "bool minic_parser_apply_alignment_attribute(MinicParser *parser,\n"
if text.count(marker) != 1:
    raise SystemExit(f"object metadata insertion marker count={text.count(marker)}")
metadata_helpers = r'''bool minic_parser_apply_object_attribute_list_with_symbol_metadata(
    MinicParser *parser,
    const MinicParsedAttributeList *attributes,
    char *section_name,
    size_t section_capacity,
    size_t *section_name_length,
    bool *has_section,
    size_t *explicit_alignment,
    MinicSymbolVisibility *visibility,
    bool *has_visibility,
    bool *is_weak) {
    MinicObjectAttributeContext context;
    size_t index;

    if (parser == NULL || attributes == NULL || visibility == NULL || has_visibility == NULL ||
        is_weak == NULL ||
        !initialize_object_attribute_context(&context,
                                             section_name,
                                             section_capacity,
                                             section_name_length,
                                             has_section,
                                             explicit_alignment)) {
        return false;
    }
    context.visibility = visibility;
    context.has_visibility = has_visibility;
    context.is_weak = is_weak;
    for (index = 0U; index < attributes->count; ++index) {
        if (!consume_object_attribute(parser, &attributes->values[index], &context)) {
            return false;
        }
    }
    return true;
}

bool minic_parser_parse_gnu_object_attribute_lists_with_symbol_metadata(
    MinicParser *parser,
    char *section_name,
    size_t section_capacity,
    size_t *section_name_length,
    bool *has_section,
    size_t *explicit_alignment,
    MinicSymbolVisibility *visibility,
    bool *has_visibility,
    bool *is_weak) {
    MinicObjectAttributeContext context;

    if (parser == NULL || visibility == NULL || has_visibility == NULL || is_weak == NULL ||
        !initialize_object_attribute_context(&context,
                                             section_name,
                                             section_capacity,
                                             section_name_length,
                                             has_section,
                                             explicit_alignment)) {
        return false;
    }
    context.visibility = visibility;
    context.has_visibility = has_visibility;
    context.is_weak = is_weak;
    return minic_parser_parse_gnu_attribute_lists(parser, consume_object_attribute, &context);
}

'''
p.write_text(text.replace(marker, metadata_helpers + marker, 1))

p = Path("src/frontend/parser_internal.h")
text = p.read_text()
marker = "bool minic_parser_apply_alignment_attribute(MinicParser *parser,\n"
if text.count(marker) != 1:
    raise SystemExit(f"internal weak metadata prototype marker count={text.count(marker)}")
prototypes = r'''bool minic_parser_apply_object_attribute_list_with_symbol_metadata(
    MinicParser *parser,
    const MinicParsedAttributeList *attributes,
    char *section_name,
    size_t section_capacity,
    size_t *section_name_length,
    bool *has_section,
    size_t *explicit_alignment,
    MinicSymbolVisibility *visibility,
    bool *has_visibility,
    bool *is_weak);
bool minic_parser_parse_gnu_object_attribute_lists_with_symbol_metadata(
    MinicParser *parser,
    char *section_name,
    size_t section_capacity,
    size_t *section_name_length,
    bool *has_section,
    size_t *explicit_alignment,
    MinicSymbolVisibility *visibility,
    bool *has_visibility,
    bool *is_weak);
'''
p.write_text(text.replace(marker, prototypes + marker, 1))

# Tentative incomplete arrays are valid file-scope entities; the canonical object owns
# their array descriptor until a later definition completes the composite type.
replace_once(
    "src/frontend/parser_function.c",
    "    if (parser == NULL || object_id_out == NULL ||\n"
    "        !minic_c0_type_is_complete_object(parser->program, object_type)) {\n",
    "    if (parser == NULL || object_id_out == NULL ||\n"
    "        (!minic_c0_type_is_complete_object(parser->program, object_type) &&\n"
    "         !minic_type_is_array(object_type))) {\n",
    "tentative incomplete external array",
)

p = Path("src/frontend/parser_function.c")
text = p.read_text()
marker = "static bool parse_external_tentative_object(MinicParser *parser,\n"
if text.count(marker) != 1:
    raise SystemExit(f"external weak helper marker count={text.count(marker)}")
weak_helper = r'''static bool apply_external_object_weak(MinicParser *parser,
                                       MinicSourceSpan name_span,
                                       bool is_weak) {
    MinicGlobalObjectId object_id;

    if (!is_weak) {
        return true;
    }
    object_id = minic_parser_find_global_object_entity(parser, name_span);
    if (object_id == MINIC_GLOBAL_OBJECT_INVALID ||
        !minic_c0_global_object_set_weak(parser->program, object_id, true)) {
        minic_parser_error(parser, "GNU weak requires external object linkage");
        return false;
    }
    return true;
}

'''
text = text.replace(marker, weak_helper + marker, 1)

# Replace the array owner as one bounded unit so the current-main declarator shape is explicit.
start = text.index("static bool parse_visible_external_array(MinicParser *parser,")
end = text.index("typedef struct MinicParsedDeclarationPrefix {", start)
new_visible_array = r'''static bool incomplete_array_declarator_is_tentative(const MinicParser *parser) {
    MinicParser probe;

    if (parser == NULL || parser->current.kind != MINIC_TOKEN_LBRACKET) {
        return false;
    }
    probe = *parser;
    if (!minic_parser_advance(&probe) || probe.current.kind != MINIC_TOKEN_RBRACKET ||
        !minic_parser_advance(&probe)) {
        return false;
    }
    while (function_identifier_is(&probe, "__attribute__") ||
           function_identifier_is(&probe, "__attribute")) {
        size_t depth;

        if (!minic_parser_advance(&probe) || probe.current.kind != MINIC_TOKEN_LPAREN) {
            return false;
        }
        depth = 0U;
        for (;;) {
            if (probe.current.kind == MINIC_TOKEN_LPAREN) {
                depth += 1U;
            } else if (probe.current.kind == MINIC_TOKEN_RPAREN) {
                if (depth == 0U) {
                    return false;
                }
                depth -= 1U;
            }
            if (!minic_parser_advance(&probe)) {
                return false;
            }
            if (depth == 0U) {
                break;
            }
        }
    }
    return probe.current.kind == MINIC_TOKEN_SEMICOLON;
}

static bool parse_visible_external_array(MinicParser *parser,
                                         MinicType element_type,
                                         MinicSourceSpan name_span,
                                         char *section_name,
                                         size_t section_name_capacity,
                                         size_t *section_name_length,
                                         bool *has_section,
                                         size_t *explicit_alignment,
                                         MinicSymbolVisibility visibility,
                                         bool has_visibility,
                                         bool *is_weak) {
    MinicParser probe;
    MinicType array_type;
    bool is_array;

    if (parser == NULL || parser->current.kind != MINIC_TOKEN_LBRACKET || section_name == NULL ||
        section_name_length == NULL || has_section == NULL || explicit_alignment == NULL ||
        is_weak == NULL) {
        return false;
    }

    if (incomplete_array_declarator_is_tentative(parser)) {
        if (!minic_parser_parse_array_declarator_suffix(
                parser, element_type, true, &array_type, &is_array) ||
            !is_array || !minic_type_is_array(array_type) ||
            !minic_parser_parse_gnu_object_attribute_lists_with_symbol_metadata(
                parser,
                section_name,
                section_name_capacity,
                section_name_length,
                has_section,
                explicit_alignment,
                &visibility,
                &has_visibility,
                is_weak) ||
            !parse_external_tentative_object(parser,
                                             array_type,
                                             name_span,
                                             section_name,
                                             *section_name_length,
                                             *has_section,
                                             *explicit_alignment,
                                             visibility,
                                             has_visibility)) {
            return false;
        }
        return apply_external_object_weak(parser, name_span, *is_weak);
    }

    probe = *parser;
    if (!minic_parser_advance(&probe)) {
        return false;
    }
    if (probe.current.kind == MINIC_TOKEN_RBRACKET) {
        if (!minic_parser_advance(&probe)) {
            return false;
        }
        if (minic_type_is_record(element_type)) {
            return parse_inferred_external_record_array_definition(parser,
                                                                   element_type,
                                                                   name_span,
                                                                   section_name,
                                                                   section_name_capacity,
                                                                   section_name_length,
                                                                   has_section,
                                                                   explicit_alignment,
                                                                   visibility,
                                                                   has_visibility);
        }
        return parse_external_integer_array_definition(parser, element_type, name_span);
    }

    if (!minic_parser_parse_array_declarator_suffix(
            parser, element_type, true, &array_type, &is_array) ||
        !is_array ||
        !minic_parser_parse_gnu_object_attribute_lists_with_symbol_metadata(
            parser,
            section_name,
            section_name_capacity,
            section_name_length,
            has_section,
            explicit_alignment,
            &visibility,
            &has_visibility,
            is_weak)) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {
        if (!parse_external_tentative_object(parser,
                                             array_type,
                                             name_span,
                                             section_name,
                                             *section_name_length,
                                             *has_section,
                                             *explicit_alignment,
                                             visibility,
                                             has_visibility)) {
            return false;
        }
    } else if (!parse_external_object_definition(parser,
                                                 array_type,
                                                 name_span,
                                                 section_name,
                                                 *section_name_length,
                                                 *has_section,
                                                 *explicit_alignment,
                                                 visibility,
                                                 has_visibility)) {
        return false;
    }
    return apply_external_object_weak(parser, name_span, *is_weak);
}

'''
text = text[:start] + new_visible_array + text[end:]

# Replace only the external-object dispatch block; static-object handling remains strict.
start_marker = "    if (!is_internal &&\n        (is_function_pointer_object || parser->current.kind != MINIC_TOKEN_LPAREN)) {\n"
end_marker = "    if (!apply_function_attribute_list(\n"
start = text.index(start_marker)
end = text.index(end_marker, start)
new_external_dispatch = r'''    if (!is_internal &&
        (is_function_pointer_object || parser->current.kind != MINIC_TOKEN_LPAREN)) {
        if (is_inline) {
            minic_parser_error(parser, "inline specifier requires a function declarator");
            return false;
        }
        if (!minic_parser_apply_object_attribute_list_with_symbol_metadata(
                parser,
                &deferred_attributes,
                section_name,
                sizeof(section_name),
                &section_name_length,
                &has_section,
                &object_explicit_alignment,
                &visibility,
                &has_visibility,
                &is_weak) ||
            !minic_parser_apply_object_attribute_list_with_symbol_metadata(
                parser,
                &declarator_attributes,
                section_name,
                sizeof(section_name),
                &section_name_length,
                &has_section,
                &object_explicit_alignment,
                &visibility,
                &has_visibility,
                &is_weak)) {
            return false;
        }
        if (is_extern_declaration) {
            if (is_weak) {
                minic_parser_error(parser,
                                   "GNU weak extern object declarations are not supported yet");
                return false;
            }
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
        if (!minic_parser_parse_gnu_object_attribute_lists_with_symbol_metadata(
                parser,
                section_name,
                sizeof(section_name),
                &section_name_length,
                &has_section,
                &object_explicit_alignment,
                &visibility,
                &has_visibility,
                &is_weak)) {
            return false;
        }
        if (parser->current.kind == MINIC_TOKEN_COMMA) {
            if (is_weak) {
                minic_parser_error(parser, "GNU weak declaration lists are not supported yet");
                return false;
            }
            return parse_external_tentative_declaration_list_after_head(parser,
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
        if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
            return parse_visible_external_array(parser,
                                                return_type,
                                                name_span,
                                                section_name,
                                                sizeof(section_name),
                                                &section_name_length,
                                                &has_section,
                                                &object_explicit_alignment,
                                                visibility,
                                                has_visibility,
                                                &is_weak);
        }
        if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {
            if (!parse_external_tentative_object(parser,
                                                 return_type,
                                                 name_span,
                                                 section_name,
                                                 section_name_length,
                                                 has_section,
                                                 object_explicit_alignment,
                                                 visibility,
                                                 has_visibility)) {
                return false;
            }
        } else if (!parse_external_object_definition(parser,
                                                     return_type,
                                                     name_span,
                                                     section_name,
                                                     section_name_length,
                                                     has_section,
                                                     object_explicit_alignment,
                                                     visibility,
                                                     has_visibility)) {
            return false;
        }
        return apply_external_object_weak(parser, name_span, is_weak);
    }
'''
text = text[:start] + new_external_dispatch + text[end:]
p.write_text(text)

# Emit weak binding for definitions/tentatives and weak extern symbols.
replace_once(
    "src/target/riscv64/codegen_function.c",
    "        if (fprintf(file, \".globl %s\\n\", object->name) < 0) {\n",
    "        if (fprintf(file, object->is_weak ? \".weak %s\\n\" : \".globl %s\\n\", object->name) < 0) {\n",
    "weak object definition emission",
)
replace_once(
    "src/target/riscv64/codegen_function.c",
    "        if (program->global_objects[global_index].is_extern) {\n"
    "            continue;\n"
    "        }\n",
    "        if (program->global_objects[global_index].is_extern) {\n"
    "            const MinicGlobalObject *object;\n\n"
    "            object = &program->global_objects[global_index];\n"
    "            if (object->is_weak && !object->is_internal &&\n"
    "                fprintf(file, \".weak %s\\n\", object->name) < 0) {\n"
    "                success = false;\n"
    "            }\n"
    "            continue;\n"
    "        }\n",
    "weak extern object emission",
)

# Permanent focused regression mirrors Linux's tentative-then-definition shapes.
Path("tests/compiler/c0/gnu_weak_external_objects.c").write_text(
    r'''struct uts_like { int value; };
struct uts_like weak_record __attribute__((__weak__));
const char weak_banner[] __attribute__((__weak__));
int weak_defined __attribute__((weak)) = 7;

struct uts_like weak_record = { 0 };
const char weak_banner[] = "x";

int main(void) {
    return weak_record.value + weak_banner[0] + weak_defined - ('x' + 7);
}
'''
)
Path("tests/compiler/c0/invalid_gnu_weak_internal_object.c").write_text(
    r'''static int internal_value __attribute__((weak));
int main(void) { return internal_value; }
'''
)
Path("tests/compiler/c0/run-gnu-weak-external-objects.sh").write_text(
    r'''#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/gnu-weak-external-objects
mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/gnu_weak_external_objects.c" -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$work/output.s"
grep -q '^\.weak weak_record$' "$work/output.s"
grep -q '^\.weak weak_banner$' "$work/output.s"
grep -q '^\.weak weak_defined$' "$work/output.s"
"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/invalid_gnu_weak_internal_object.c" -o "$work/invalid.i"
if "$minic" -S "$work/invalid.i" -o "$work/invalid.s" >"$work/invalid.out" 2>"$work/invalid.err"; then
  echo 'expected internal weak object rejection' >&2
  exit 1
fi
'''
)
run = Path("tests/compiler/c0/run.sh")
run_text = run.read_text()
entry = (
    '\nMINIC="$minic" HOST_CC="$host_cc" BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \\\n'
    '  sh "$root/tests/compiler/c0/run-gnu-weak-external-objects.sh"\n'
)
if "run-gnu-weak-external-objects.sh" not in run_text:
    run.write_text(run_text + entry)
