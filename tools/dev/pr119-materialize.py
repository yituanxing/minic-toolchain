from pathlib import Path


parser_path = Path("src/frontend/parser_function.c")
text = parser_path.read_text()

# Split declaration-entity recording from semicolon ownership so a declaration
# list can record each function entity without faking parser state.
start = text.find("static bool finish_function_declaration_entity(")
end = text.find("\nstatic bool parse_function(", start)
if start < 0 or end < 0:
    raise SystemExit(f"function declaration helper region mismatch start={start} end={end}")
region = text[start:end].rstrip("\n")
region = region.replace(
    "static bool finish_function_declaration_entity(",
    "static bool record_function_declaration_entity(",
    1,
)
guard = """    if (parser == NULL || parameter_count > MINIC_MAX_FUNCTION_PARAMETERS ||
        (parameter_count != 0U && parameter_types == NULL) ||
        parser->current.kind != MINIC_TOKEN_SEMICOLON) {
        return false;
    }
"""
replacement_guard = """    if (parser == NULL || parameter_count > MINIC_MAX_FUNCTION_PARAMETERS ||
        (parameter_count != 0U && parameter_types == NULL)) {
        return false;
    }
"""
if region.count(guard) != 1:
    raise SystemExit(f"function declaration guard mismatch: {region.count(guard)}")
region = region.replace(guard, replacement_guard, 1)
tail = "    return minic_parser_advance(parser);\n}"
if not region.endswith(tail):
    raise SystemExit("function declaration helper tail mismatch")
region = region[: -len(tail)] + "    return true;\n}"

wrapper = r'''

static bool finish_function_declaration_entity(MinicParser *parser,
                                               MinicSourceSpan name_span,
                                               MinicType return_type,
                                               const MinicType *parameter_types,
                                               size_t parameter_count,
                                               bool is_variadic,
                                               bool is_internal,
                                               bool is_weak,
                                               const char *assembler_name,
                                               size_t assembler_name_length,
                                               bool has_assembler_name,
                                               MinicSymbolVisibility visibility,
                                               bool has_visibility,
                                               const char *section_name,
                                               size_t section_name_length,
                                               bool has_section) {
    if (parser == NULL || parser->current.kind != MINIC_TOKEN_SEMICOLON ||
        !record_function_declaration_entity(parser,
                                            name_span,
                                            return_type,
                                            parameter_types,
                                            parameter_count,
                                            is_variadic,
                                            is_internal,
                                            is_weak,
                                            assembler_name,
                                            assembler_name_length,
                                            has_assembler_name,
                                            visibility,
                                            has_visibility,
                                            section_name,
                                            section_name_length,
                                            has_section)) {
        return false;
    }
    return minic_parser_advance(parser);
}
'''
text = text[:start] + region + wrapper + text[end:]

# #109 already routes a final FunctionType to MinicFunction. Extend only the
# declaration-list layer: every direct declarator reuses the same canonical
# FunctionType snapshot and the same entity recorder. Pointer/object mixes stay
# fail-closed until a complete declarator-list model owns mixed entity kinds.
start_marker = "    if (!is_function_pointer_object && minic_type_is_function(return_type)) {"
end_marker = "    if (is_static_declaration && parser->current.kind != MINIC_TOKEN_LPAREN) {"
start = text.find(start_marker)
end = text.find(end_marker, start)
if start < 0 or end < 0:
    raise SystemExit(f"function-typed branch mismatch start={start} end={end}")
new_branch = r'''    if (!is_function_pointer_object && minic_type_is_function(return_type)) {
        const MinicFunctionType *function_type;
        MinicType typed_return_type;
        MinicType typed_parameter_types[MINIC_MAX_FUNCTION_PARAMETERS];
        size_t typed_parameter_count;
        size_t parameter_index;
        bool declaration_is_weak;

        if (parser->current.kind == MINIC_TOKEN_LPAREN) {
            minic_parser_error(parser,
                               "function-typed declarator cannot add another function suffix");
            return false;
        }
        function_type =
            minic_c0_program_function_type(parser->program, return_type.function_type_id);
        if (function_type == NULL ||
            function_type->parameter_count > MINIC_MAX_FUNCTION_PARAMETERS) {
            minic_parser_error(parser, "invalid function-typed declarator signature");
            return false;
        }
        /* FunctionType is Program-owned growable storage. Snapshot its canonical
         * signature before any subsequent semantic operation can grow owner pools. */
        typed_return_type = function_type->return_type;
        typed_parameter_count = function_type->parameter_count;
        for (parameter_index = 0U; parameter_index < typed_parameter_count; ++parameter_index) {
            typed_parameter_types[parameter_index] =
                function_type->parameter_types[parameter_index];
        }
        if (!apply_function_attribute_list(
                parser,
                &deferred_attributes,
                true,
                is_internal,
                is_inline,
                section_name,
                sizeof(section_name),
                &section_name_length,
                &has_section,
                &is_weak,
                "unsupported GNU prefix function attribute; semantic and ABI-affecting attributes "
                "must be implemented explicitly")) {
            return false;
        }
        declaration_is_weak = is_weak;

        for (;;) {
            bool entity_is_weak;

            if (parser->current.kind == MINIC_TOKEN_LPAREN) {
                minic_parser_error(parser,
                                   "function-typed declarator cannot add another function suffix");
                return false;
            }
            entity_is_weak = declaration_is_weak;
            assembler_name_length = 0U;
            has_assembler_name = false;
            (void)memset(assembler_name, 0, sizeof(assembler_name));
            if (!parse_gnu_function_asm_label(parser,
                                              assembler_name,
                                              sizeof(assembler_name),
                                              &assembler_name_length,
                                              &has_assembler_name) ||
                !parse_persistent_function_attributes(parser, is_internal, &entity_is_weak)) {
                return false;
            }
            if (parser->current.kind != MINIC_TOKEN_COMMA &&
                parser->current.kind != MINIC_TOKEN_SEMICOLON) {
                minic_parser_error(
                    parser, "function-typed declarators currently support declarations only");
                return false;
            }
            if (!record_function_declaration_entity(parser,
                                                    name_span,
                                                    typed_return_type,
                                                    typed_parameter_types,
                                                    typed_parameter_count,
                                                    false,
                                                    is_internal,
                                                    entity_is_weak,
                                                    assembler_name,
                                                    assembler_name_length,
                                                    has_assembler_name,
                                                    visibility,
                                                    has_visibility,
                                                    section_name,
                                                    section_name_length,
                                                    has_section)) {
                return false;
            }
            if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {
                return minic_parser_advance(parser);
            }
            if (!minic_parser_advance(parser) ||
                !minic_parser_parse_direct_declarator_name(parser, &name_span)) {
                return false;
            }
        }
    }
'''
text = text[:start] + new_branch + text[end:]
parser_path.write_text(text)

fixture = Path("tests/compiler/c0/function_typed_declarator.c")
fixture_text = fixture.read_text()
addition = r'''

struct p_log;
struct fs_parameter_spec;
struct fs_parameter;
struct fs_parse_result;

typedef int fs_param_type(struct p_log *log,
                          const struct fs_parameter_spec *spec,
                          struct fs_parameter *param,
                          struct fs_parse_result *result);

fs_param_type fs_param_is_bool, fs_param_is_u32, fs_param_is_s32, fs_param_is_u64,
    fs_param_is_enum, fs_param_is_string, fs_param_is_blob, fs_param_is_blockdev,
    fs_param_is_path, fs_param_is_fd;

int invoke_function_typed_list(struct p_log *log,
                               const struct fs_parameter_spec *spec,
                               struct fs_parameter *param,
                               struct fs_parse_result *result) {
    return fs_param_is_bool(log, spec, param, result) +
           fs_param_is_fd(log, spec, param, result);
}
'''
if "fs_param_type fs_param_is_bool" in fixture_text:
    raise SystemExit("function-typed list fixture already materialized")
fixture.write_text(fixture_text + addition)

runner = Path("tests/compiler/c0/run-function-typed-declarator.sh")
runner_text = runner.read_text()
needle = 'grep -F "  jalr ra, t0, 0" "$work/function_typed_declarator.s" >/dev/null\n'
addition = needle + (
    'grep -F "  call fs_param_is_bool" "$work/function_typed_declarator.s" >/dev/null\n'
    'grep -F "  call fs_param_is_fd" "$work/function_typed_declarator.s" >/dev/null\n'
)
if runner_text.count(needle) != 1:
    raise SystemExit(f"function-typed runner anchor mismatch: {runner_text.count(needle)}")
runner_text = runner_text.replace(needle, addition, 1)
runner_text = runner_text.replace(
    "pointer-typedef=object+jalr definition=reject",
    "pointer-typedef=object+jalr declaration-list=direct-functions definition=reject",
    1,
)
runner.write_text(runner_text)
