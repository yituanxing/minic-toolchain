#!/usr/bin/env python3
"""Materialize generic chained static array designators such as [i][j] = value."""
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    if new in text:
        return
    if text.count(old) != 1:
        raise SystemExit(f"{path}: expected one anchor, found {text.count(old)}")
    p.write_text(text.replace(old, new, 1))


# Split the common array-designator parser into a bracket-only component and the
# historical wrapper that consumes '='. This lets aggregate owners decide whether
# another designator component follows without weakening scalar callers.
p = Path("src/frontend/parser_constant.c")
text = p.read_text()
start = text.find("bool minic_parser_parse_array_designator(\n")
if start < 0:
    if "bool minic_parser_parse_array_designator_component(" not in text:
        raise SystemExit("parser_constant.c: array designator function anchor missing")
else:
    old = text[start:]
    new = '''bool minic_parser_parse_array_designator_component(\n    MinicParser *parser, size_t element_count, bool infer_bound, size_t *first, size_t *last) {\n    if (parser == NULL || first == NULL || last == NULL ||\n        parser->current.kind != MINIC_TOKEN_LBRACKET || !minic_parser_advance(parser) ||\n        !parse_array_designator_bound(parser, element_count, infer_bound, first)) {\n        return false;\n    }\n    *last = *first;\n    if (parser->current.kind == MINIC_TOKEN_ELLIPSIS) {\n        if (!minic_parser_advance(parser) ||\n            !parse_array_designator_bound(parser, element_count, infer_bound, last)) {\n            return false;\n        }\n        if (*last < *first) {\n            minic_parser_error(parser,\n                               "GNU array range designator upper bound is below lower bound");\n            return false;\n        }\n    }\n    return minic_parser_expect(\n        parser, MINIC_TOKEN_RBRACKET, "expected ']' after array designator");\n}\n\nbool minic_parser_parse_array_designator(\n    MinicParser *parser, size_t element_count, bool infer_bound, size_t *first, size_t *last) {\n    return minic_parser_parse_array_designator_component(\n               parser, element_count, infer_bound, first, last) &&\n           minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '=' after array designator");\n}\n'''
    p.write_text(text[:start] + new)

p = Path("src/frontend/parser_internal.h")
text = p.read_text()
prototype = '''bool minic_parser_parse_array_designator_component(\n    MinicParser *parser, size_t element_count, bool infer_bound, size_t *first, size_t *last);\n'''
if prototype not in text:
    anchor = "bool minic_parser_parse_array_designator(\n"
    pos = text.find(anchor)
    if pos < 0:
        raise SystemExit("parser_internal.h: array designator prototype anchor missing")
    text = text[:pos] + prototype + text[pos:]
    p.write_text(text)

helper = '''static bool append_static_chained_array_designator_value(MinicParser *parser,\n                                                         MinicGlobalObjectId object_id,\n                                                         MinicType array_type) {\n    const MinicArrayType *array;\n    size_t first;\n    size_t last;\n    size_t index;\n\n    if (parser == NULL || !minic_type_is_array(array_type) ||\n        parser->current.kind != MINIC_TOKEN_LBRACKET) {\n        return false;\n    }\n    array = minic_c0_program_array_type(parser->program, array_type.array_type_id);\n    if (array == NULL || array->element_count == 0U || array->is_zero_length ||\n        !minic_parser_parse_array_designator_component(\n            parser, array->element_count, false, &first, &last)) {\n        return false;\n    }\n    if (first != last) {\n        minic_parser_error(\n            parser, "GNU range designators inside chained static arrays are not supported yet");\n        return false;\n    }\n    for (index = 0U; index < first; ++index) {\n        if (!append_static_constant_zero(parser, object_id, array->element_type)) {\n            minic_parser_error(parser, "cannot zero-fill chained static array prefix");\n            return false;\n        }\n    }\n    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {\n        if (!minic_type_is_array(array->element_type) ||\n            !append_static_chained_array_designator_value(\n                parser, object_id, array->element_type)) {\n            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {\n                minic_parser_error(parser,\n                                   "chained array designator requires another array dimension");\n            }\n            return false;\n        }\n    } else {\n        if (!minic_parser_expect(\n                parser, MINIC_TOKEN_EQUAL, "expected '=' after chained array designator") ||\n            !minic_parser_parse_static_storage_initializer_value(\n                parser, object_id, array->element_type)) {\n            return false;\n        }\n    }\n    for (index = first + 1U; index < array->element_count; ++index) {\n        if (!append_static_constant_zero(parser, object_id, array->element_type)) {\n            minic_parser_error(parser, "cannot zero-fill chained static array suffix");\n            return false;\n        }\n    }\n    return true;\n}\n\n'''
p = Path("src/frontend/parser_global.c")
text = p.read_text()
marker = "static bool parse_static_forward_array_initializer(MinicParser *parser,\n"
if helper not in text:
    pos = text.find(marker)
    if pos < 0:
        raise SystemExit("parser_global.c: forward aggregate array anchor missing")
    text = text[:pos] + helper + text[pos:]

old_decl = '''        size_t initializer_begin;\n        size_t relocation_begin;\n        size_t union_selection_begin;\n\n        if (parser->current.kind == MINIC_TOKEN_LBRACKET) {\n'''
new_decl = '''        size_t initializer_begin;\n        size_t relocation_begin;\n        size_t union_selection_begin;\n        bool chained_designator;\n\n        chained_designator = false;\n        if (parser->current.kind == MINIC_TOKEN_LBRACKET) {\n'''
if new_decl not in text:
    if text.count(old_decl) != 1:
        raise SystemExit("parser_global.c: aggregate action declaration anchor missing")
    text = text.replace(old_decl, new_decl, 1)

old_designator = '''            if (!minic_parser_parse_array_designator(\n                    parser, element_count, infer_bound, &first, &last) ||\n                !minic_array_initializer_plan_add_designated(&plan, first, last, &action_id)) {\n                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {\n                    minic_parser_error(parser,\n                                       "static aggregate array designator extent overflows");\n                }\n                goto done;\n            }\n'''
new_designator = '''            if (!minic_parser_parse_array_designator_component(\n                    parser, element_count, infer_bound, &first, &last) ||\n                !minic_array_initializer_plan_add_designated(&plan, first, last, &action_id)) {\n                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {\n                    minic_parser_error(parser,\n                                       "static aggregate array designator extent overflows");\n                }\n                goto done;\n            }\n            if (parser->current.kind == MINIC_TOKEN_LBRACKET) {\n                if (first != last) {\n                    minic_parser_error(\n                        parser, "outer GNU range designator cannot be chained yet");\n                    goto done;\n                }\n                if (!minic_type_is_array(element_type)) {\n                    minic_parser_error(parser,\n                                       "chained array designator exceeds array dimensions");\n                    goto done;\n                }\n                chained_designator = true;\n            } else if (!minic_parser_expect(\n                           parser, MINIC_TOKEN_EQUAL, "expected '=' after array designator")) {\n                goto done;\n            }\n'''
if new_designator not in text:
    if text.count(old_designator) != 1:
        raise SystemExit("parser_global.c: aggregate designator parse anchor missing")
    text = text.replace(old_designator, new_designator, 1)

old_value = '''        if (!minic_parser_parse_static_storage_initializer_value(parser, object_id, element_type) ||\n            !capture_static_aggregate_array_action(parser,\n                                                   object_id,\n                                                   initializer_begin,\n                                                   relocation_begin,\n                                                   union_selection_begin,\n                                                   &actions[action_id])) {\n            goto done;\n        }\n'''
new_value = '''        if ((chained_designator\n                 ? !append_static_chained_array_designator_value(\n                       parser, object_id, element_type)\n                 : !minic_parser_parse_static_storage_initializer_value(\n                       parser, object_id, element_type)) ||\n            !capture_static_aggregate_array_action(parser,\n                                                   object_id,\n                                                   initializer_begin,\n                                                   relocation_begin,\n                                                   union_selection_begin,\n                                                   &actions[action_id])) {\n            goto done;\n        }\n'''
if new_value not in text:
    if text.count(old_value) != 1:
        raise SystemExit("parser_global.c: aggregate action value anchor missing")
    text = text.replace(old_value, new_value, 1)
p.write_text(text)

replace_once(
    "tests/compiler/c0/static_array_designators.c",
    '''static int mutable_inferred[] = {\n    [3] = 8,\n    [1] = 4,\n};\n\nint main(void) {\n    return (int)(indexed[1] + ranged[1] + (syscall_shape[0] != 0) + (names[2] != 0) +\n                 mutable_inferred[3]);\n}\n''',
    '''static int mutable_inferred[] = {\n    [3] = 8,\n    [1] = 4,\n};\n\nstatic const unsigned long chained[3][2] = {\n    [1][0] = 11UL,\n    [2][1] = 13UL,\n};\n\nint main(void) {\n    return (int)(indexed[1] + ranged[1] + (syscall_shape[0] != 0) + (names[2] != 0) +\n                 mutable_inferred[3] + chained[1][0] + chained[2][1]);\n}\n''')

replace_once(
    "tests/compiler/c0/run-static-array-designators.sh",
    '''grep -F 'names:' "$asm" >/dev/null\ngrep -F '.size names, 24' "$asm" >/dev/null\n\nsed -n '/^syscall_shape:/,/^.size syscall_shape, 32/p' "$asm" | \\\n''',
    '''grep -F 'names:' "$asm" >/dev/null\ngrep -F '.size names, 24' "$asm" >/dev/null\ngrep -F 'chained:' "$asm" >/dev/null\ngrep -F '.size chained, 48' "$asm" >/dev/null\n\nsed -n '/^chained:/,/^.size chained, 48/p' "$asm" | \\\n    grep '^  \\.dword ' >"$work/chained.actual"\ncat >"$work/chained.expected" <<'EOF'\n  .dword 0\n  .dword 0\n  .dword 11\n  .dword 0\n  .dword 0\n  .dword 13\nEOF\ndiff -u "$work/chained.expected" "$work/chained.actual"\n\nsed -n '/^syscall_shape:/,/^.size syscall_shape, 32/p' "$asm" | \\\n''')

replace_once(
    "tests/compiler/c0/run-static-array-designators.sh",
    "'PASS compiler/c0/static_array_designators c99=1 gnu-range=1 overwrite=1 function-reloc=4 string-reloc=2 inferred-backward=1'\n",
    "'PASS compiler/c0/static_array_designators c99=1 gnu-range=1 overwrite=1 function-reloc=4 string-reloc=2 inferred-backward=1 chained=1'\n")
