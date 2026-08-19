#!/usr/bin/env python3
from pathlib import Path
import re


def read(path):
    return Path(path).read_text()


def write(path, text):
    Path(path).write_text(text)


def replace_once(path, old, new):
    text = read(path)
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one exact match, found {count}: {old[:80]!r}")
    write(path, text.replace(old, new, 1))


def regex_once(path, pattern, replacement, flags=0):
    text = read(path)
    text2, count = re.subn(pattern, replacement, text, count=1, flags=flags)
    if count != 1:
        raise SystemExit(f"{path}: expected one regex match, found {count}: {pattern[:100]!r}")
    write(path, text2)


# Function-type identity owns variadicness. Keep the old API as the non-variadic wrapper
# so standalone tests and simple clients do not need unrelated churn.
replace_once(
    "src/frontend/ast.h",
    "typedef struct MinicFunctionType {\n"
    "    MinicType return_type;\n"
    "    MinicType parameter_types[MINIC_MAX_FUNCTION_PARAMETERS];\n"
    "    size_t parameter_count;\n"
    "} MinicFunctionType;",
    "typedef struct MinicFunctionType {\n"
    "    MinicType return_type;\n"
    "    MinicType parameter_types[MINIC_MAX_FUNCTION_PARAMETERS];\n"
    "    size_t parameter_count;\n"
    "    bool is_variadic;\n"
    "} MinicFunctionType;",
)
replace_once(
    "src/frontend/ast.h",
    "bool minic_c0_program_add_function_type(MinicC0Program *program,\n"
    "                                        MinicType return_type,\n"
    "                                        const MinicType *parameter_types,\n"
    "                                        size_t parameter_count,\n"
    "                                        MinicType *function_type);",
    "bool minic_c0_program_add_variadic_function_type(MinicC0Program *program,\n"
    "                                                 MinicType return_type,\n"
    "                                                 const MinicType *parameter_types,\n"
    "                                                 size_t parameter_count,\n"
    "                                                 bool is_variadic,\n"
    "                                                 MinicType *function_type);\n"
    "bool minic_c0_program_add_function_type(MinicC0Program *program,\n"
    "                                        MinicType return_type,\n"
    "                                        const MinicType *parameter_types,\n"
    "                                        size_t parameter_count,\n"
    "                                        MinicType *function_type);",
)
replace_once(
    "src/frontend/ast.c",
    "static bool minic_function_type_matches(const MinicFunctionType *descriptor,\n"
    "                                        MinicType return_type,\n"
    "                                        const MinicType *parameter_types,\n"
    "                                        size_t parameter_count) {",
    "static bool minic_function_type_matches(const MinicFunctionType *descriptor,\n"
    "                                        MinicType return_type,\n"
    "                                        const MinicType *parameter_types,\n"
    "                                        size_t parameter_count,\n"
    "                                        bool is_variadic) {",
)
replace_once(
    "src/frontend/ast.c",
    "    if (descriptor == NULL || descriptor->parameter_count != parameter_count ||\n"
    "        !minic_type_equal(descriptor->return_type, return_type)) {",
    "    if (descriptor == NULL || descriptor->parameter_count != parameter_count ||\n"
    "        descriptor->is_variadic != is_variadic ||\n"
    "        !minic_type_equal(descriptor->return_type, return_type)) {",
)
replace_once(
    "src/frontend/ast.c",
    "bool minic_c0_program_add_function_type(MinicC0Program *program,\n"
    "                                        MinicType return_type,\n"
    "                                        const MinicType *parameter_types,\n"
    "                                        size_t parameter_count,\n"
    "                                        MinicType *function_type) {",
    "bool minic_c0_program_add_variadic_function_type(MinicC0Program *program,\n"
    "                                                 MinicType return_type,\n"
    "                                                 const MinicType *parameter_types,\n"
    "                                                 size_t parameter_count,\n"
    "                                                 bool is_variadic,\n"
    "                                                 MinicType *function_type) {",
)
replace_once(
    "src/frontend/ast.c",
    "        if (minic_function_type_matches(&program->function_types[function_type_index],\n"
    "                                        return_type,\n"
    "                                        normalized_parameter_types,\n"
    "                                        parameter_count)) {",
    "        if (minic_function_type_matches(&program->function_types[function_type_index],\n"
    "                                        return_type,\n"
    "                                        normalized_parameter_types,\n"
    "                                        parameter_count,\n"
    "                                        is_variadic)) {",
)
replace_once(
    "src/frontend/ast.c",
    "    descriptor.return_type = return_type;\n"
    "    descriptor.parameter_count = parameter_count;",
    "    descriptor.return_type = return_type;\n"
    "    descriptor.parameter_count = parameter_count;\n"
    "    descriptor.is_variadic = is_variadic;",
)
replace_once(
    "src/frontend/ast.c",
    "bool minic_c0_program_add_type_alias(MinicC0Program *program,",
    "bool minic_c0_program_add_function_type(MinicC0Program *program,\n"
    "                                        MinicType return_type,\n"
    "                                        const MinicType *parameter_types,\n"
    "                                        size_t parameter_count,\n"
    "                                        MinicType *function_type) {\n"
    "    return minic_c0_program_add_variadic_function_type(program,\n"
    "                                                       return_type,\n"
    "                                                       parameter_types,\n"
    "                                                       parameter_count,\n"
    "                                                       false,\n"
    "                                                       function_type);\n"
    "}\n\n"
    "bool minic_c0_program_add_type_alias(MinicC0Program *program,",
)

# Carry the parser-owned variadic bit into the canonical type descriptor.
replace_once(
    "src/frontend/parser_declarator.c",
    "    if (!minic_c0_program_add_function_type(parser->program,\n"
    "                                            return_type,\n"
    "                                            declarator->parameter_types,\n"
    "                                            declarator->parameter_count,\n"
    "                                            &function_type)) {",
    "    if (!minic_c0_program_add_variadic_function_type(parser->program,\n"
    "                                                     return_type,\n"
    "                                                     declarator->parameter_types,\n"
    "                                                     declarator->parameter_count,\n"
    "                                                     declarator->is_variadic,\n"
    "                                                     &function_type)) {",
)

# Function entities already own variadicness. Function references should materialize the
# matching function type instead of rejecting the designator.
regex_once(
    "src/frontend/parser_expression.c",
    r'\n\s*if \(function->is_variadic\) \{\n\s*minic_parser_error\(parser, "variadic function designator is not supported yet"\);\n\s*return false;\n\s*\}',
    "",
)
regex_once(
    "src/frontend/parser_expression.c",
    r'minic_c0_program_add_function_type\(parser->program,\s*function->return_type,\s*function->parameter_types,\s*function->parameter_count,\s*&function_type\)',
    "minic_c0_program_add_variadic_function_type(parser->program,\n"
    "                                                     function->return_type,\n"
    "                                                     function->parameter_types,\n"
    "                                                     function->parameter_count,\n"
    "                                                     function->is_variadic,\n"
    "                                                     &function_type)",
    re.S,
)

# Typedef and parameter declarators no longer reject an already-representable variadic type.
regex_once(
    "src/frontend/parser_typedef.c",
    r'\n\s*if \(declarator\.is_variadic\) \{\n\s*minic_parser_error\(parser, "variadic function pointer typedefs are not supported yet"\);\n\s*return false;\n\s*\}',
    "",
)
regex_once(
    "src/frontend/parser_typedef.c",
    r'\n\s*if \(declarator\.is_variadic\) \{\n\s*minic_parser_error\(parser, "variadic function typedefs are not supported yet"\);\n\s*return false;\n\s*\}',
    "",
)
regex_once(
    "src/frontend/parser_function.c",
    r'\n\s*if \(declarator\.is_variadic\) \{\n\s*minic_parser_error\(parser, "variadic function pointer parameters are not supported yet"\);\n\s*return false;\n\s*\}',
    "",
)

# Declaration-head GNU attributes are consumed locally, after the final semantic target is
# known. Do not make __attribute__ a global type-start token.
typedef_helpers = r'''
static bool typedef_declaration_attribute_class_is_parse_only(
    MinicAttributeClass semantic_class) {
    return semantic_class == MINIC_ATTRIBUTE_CLASS_INFORMATIONAL ||
           semantic_class == MINIC_ATTRIBUTE_CLASS_DIAGNOSTIC ||
           semantic_class == MINIC_ATTRIBUTE_CLASS_OPTIMIZATION ||
           semantic_class == MINIC_ATTRIBUTE_CLASS_CONTROL_FLOW;
}

static bool typedef_type_targets_function(MinicType type) {
    MinicType pointee;

    if (minic_type_is_function(type)) {
        return true;
    }
    return minic_type_pointee(type, &pointee) && minic_type_is_function(pointee);
}

static bool apply_typedef_declaration_head_attributes(
    MinicParser *parser, const MinicParsedAttributeList *attributes, MinicType aliased_type) {
    size_t index;
    bool function_target;

    if (parser == NULL || attributes == NULL) {
        return false;
    }
    function_target = typedef_type_targets_function(aliased_type);
    for (index = 0U; index < attributes->count; ++index) {
        const MinicAttributeDescriptor *descriptor;

        descriptor = attributes->values[index].descriptor;
        if (descriptor == NULL ||
            !typedef_declaration_attribute_class_is_parse_only(descriptor->semantic_class) ||
            ((!minic_attribute_allowed_on(descriptor, MINIC_ATTRIBUTE_TARGET_TYPE)) &&
             (!function_target ||
              !minic_attribute_allowed_on(descriptor, MINIC_ATTRIBUTE_TARGET_FUNCTION)))) {
            minic_parser_error(parser, "unsupported GNU declaration-head typedef attribute");
            return false;
        }
    }
    return true;
}

'''
replace_once(
    "src/frontend/parser_typedef.c",
    "static bool parse_typedef_attributes(MinicParser *parser, MinicType *aliased_type) {",
    typedef_helpers + "static bool parse_typedef_attributes(MinicParser *parser, MinicType *aliased_type) {",
)
replace_once(
    "src/frontend/parser_typedef.c",
    "    MinicTypeAliasId alias_id;\n"
    "    bool is_function_declarator;",
    "    MinicTypeAliasId alias_id;\n"
    "    MinicParsedAttributeList leading_attributes;\n"
    "    MinicParsedAttributeList post_type_attributes;\n"
    "    bool is_function_declarator;",
)
replace_once(
    "src/frontend/parser_typedef.c",
    "    if (!minic_parser_expect(parser, MINIC_TOKEN_KW_TYPEDEF, \"expected keyword 'typedef'\")) {\n"
    "        return false;\n"
    "    }\n"
    "    {",
    "    if (!minic_parser_expect(parser, MINIC_TOKEN_KW_TYPEDEF, \"expected keyword 'typedef'\") ||\n"
    "        !minic_parser_collect_gnu_attribute_lists(parser, &leading_attributes)) {\n"
    "        return false;\n"
    "    }\n"
    "    {",
)
replace_once(
    "src/frontend/parser_typedef.c",
    "        if (!minic_parser_parse_type_specifiers(parser, &base_type) ||\n"
    "            !minic_parser_parse_pointer_declarator(parser, base_type, &aliased_type)) {\n"
    "            return false;\n"
    "        }",
    "        if (!minic_parser_parse_type_specifiers(parser, &base_type) ||\n"
    "            !minic_parser_collect_gnu_attribute_lists(parser, &post_type_attributes) ||\n"
    "            !minic_parser_parse_pointer_declarator(parser, base_type, &aliased_type)) {\n"
    "            return false;\n"
    "        }",
)
replace_once(
    "src/frontend/parser_typedef.c",
    "    if (!parse_typedef_attributes(parser, &aliased_type)) {\n"
    "        return false;\n"
    "    }",
    "    if (!apply_typedef_declaration_head_attributes(\n"
    "            parser, &leading_attributes, aliased_type) ||\n"
    "        !apply_typedef_declaration_head_attributes(\n"
    "            parser, &post_type_attributes, aliased_type) ||\n"
    "        !parse_typedef_attributes(parser, &aliased_type)) {\n"
    "        return false;\n"
    "    }",
)

parameter_helper = r'''
static bool apply_parameter_declarator_attribute_list(
    MinicParser *parser, const MinicParsedAttributeList *attributes) {
    size_t index;

    if (parser == NULL || attributes == NULL) {
        return false;
    }
    for (index = 0U; index < attributes->count; ++index) {
        if (!consume_parameter_declarator_attribute(parser, &attributes->values[index], NULL)) {
            return false;
        }
    }
    return true;
}

'''
replace_once(
    "src/frontend/parser_function.c",
    "bool minic_parser_parse_parameter_list(MinicParser *parser,",
    parameter_helper + "bool minic_parser_parse_parameter_list(MinicParser *parser,",
)
replace_once(
    "src/frontend/parser_function.c",
    "        MinicSourceSpan declarator_name_span;\n"
    "        MinicType parameter_type;\n"
    "        bool declarator_has_name;",
    "        MinicSourceSpan declarator_name_span;\n"
    "        MinicType parameter_type;\n"
    "        MinicParsedAttributeList leading_attributes;\n"
    "        MinicParsedAttributeList post_type_attributes;\n"
    "        bool declarator_has_name;",
)
replace_once(
    "src/frontend/parser_function.c",
    "        if (!minic_parser_parse_type_name_preserving_incomplete(parser, &parameter_type)) {\n"
    "            return false;\n"
    "        }",
    "        if (!minic_parser_collect_gnu_attribute_lists(parser, &leading_attributes) ||\n"
    "            !minic_parser_parse_type_name_preserving_incomplete(parser, &parameter_type) ||\n"
    "            !minic_parser_collect_gnu_attribute_lists(parser, &post_type_attributes)) {\n"
    "            return false;\n"
    "        }",
)
replace_once(
    "src/frontend/parser_function.c",
    "        if (!minic_parser_parse_gnu_attribute_lists(\n"
    "                parser, consume_parameter_declarator_attribute, NULL)) {\n"
    "            return false;\n"
    "        }",
    "        if (!apply_parameter_declarator_attribute_list(parser, &leading_attributes) ||\n"
    "            !apply_parameter_declarator_attribute_list(parser, &post_type_attributes) ||\n"
    "            !minic_parser_parse_gnu_attribute_lists(\n"
    "                parser, consume_parameter_declarator_attribute, NULL)) {\n"
    "            return false;\n"
    "        }",
)

# Calls consume the variadic tail after fixed parameters. Verifier/backend already own the
# scalar admissibility and RV64 default integer conversion rules respectively.
new_parse_indirect = r'''static bool parse_indirect_arguments(MinicParser *parser,
                                     MinicExpression *call,
                                     const MinicFunctionType *function_type) {
    size_t argument_index;

    if (function_type == NULL || function_type->parameter_count > MINIC_MAX_FUNCTION_PARAMETERS ||
        !minic_parser_advance(parser)) {
        return false;
    }
    for (argument_index = 0U; argument_index < function_type->parameter_count; ++argument_index) {
        MinicExpressionId argument_id;

        if (parser->current.kind == MINIC_TOKEN_RPAREN) {
            indirect_argument_count_error(parser);
            return false;
        }
        if (!minic_parser_parse_expression(parser, &argument_id, 0U) ||
            !minic_parser_apply_fixed_call_argument_conversion(
                parser, function_type->parameter_types[argument_index], &argument_id) ||
            !minic_c0_fixed_call_argument_compatible(
                parser->program, function_type->parameter_types[argument_index], argument_id)) {
            minic_parser_error(parser, "indirect call argument type does not match declaration");
            return false;
        }
        call->value.call.arguments[argument_index] = argument_id;
        if (argument_index + 1U < function_type->parameter_count) {
            if (parser->current.kind != MINIC_TOKEN_COMMA || !minic_parser_advance(parser)) {
                indirect_argument_count_error(parser);
                return false;
            }
        }
    }

    argument_index = function_type->parameter_count;
    if (function_type->is_variadic && parser->current.kind == MINIC_TOKEN_COMMA) {
        do {
            const MinicExpression *argument;
            MinicExpressionId argument_id;

            if (argument_index >= MINIC_MAX_FUNCTION_PARAMETERS ||
                !minic_parser_advance(parser) ||
                !minic_parser_parse_expression(parser, &argument_id, 0U) ||
                !minic_parser_apply_array_decay(parser, argument_id, &argument_id)) {
                minic_parser_error(parser, "variadic call argument count exceeds implementation limit");
                return false;
            }
            argument = minic_c0_program_expression(parser->program, argument_id);
            if (argument == NULL ||
                (!minic_type_is_integer(argument->type) &&
                 !minic_type_is_pointer(argument->type) &&
                 !minic_type_is_double(argument->type))) {
                minic_parser_error(parser, "unsupported variadic call argument type");
                return false;
            }
            call->value.call.arguments[argument_index++] = argument_id;
        } while (parser->current.kind == MINIC_TOKEN_COMMA);
    }
    if (parser->current.kind != MINIC_TOKEN_RPAREN) {
        indirect_argument_count_error(parser);
        return false;
    }
    call->value.call.argument_count = argument_index;
    return true;
}

'''
regex_once(
    "src/frontend/parser_postfix.c",
    r'static bool parse_indirect_arguments\(MinicParser \*parser,.*?\n\}\n\n(?=static bool parse_one_indirect_call)',
    new_parse_indirect,
    re.S,
)

# Register the focused gate permanently after productization.
run = read("tests/compiler/c0/run.sh")
block = '''\nMINIC="$minic" \\\nHOST_CC="$host_cc" \\\nBUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \\\nsh "$root/tests/compiler/c0/run-declaration-head-variadic.sh"\n'''
if "run-declaration-head-variadic.sh" in run:
    raise SystemExit("tests/compiler/c0/run.sh: focused gate already registered")
write("tests/compiler/c0/run.sh", run.rstrip() + "\n" + block)

print("materialized declaration-head attributes and variadic function types")
