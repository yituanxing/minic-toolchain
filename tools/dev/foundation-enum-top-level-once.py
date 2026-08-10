#!/usr/bin/env python3
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, found {count}")
    return text.replace(old, new, 1)


root = Path(__file__).resolve().parents[2]

path = root / "src/frontend/parser_function.c"
text = path.read_text()
anchor = '''static bool record_keyword_starts_standalone_declaration(MinicParser *parser, bool *is_standalone) {\n'''
helper = r'''static bool enum_keyword_starts_definition(MinicParser *parser, bool *is_definition) {
    MinicParser probe;

    if (parser == NULL || is_definition == NULL || parser->current.kind != MINIC_TOKEN_KW_ENUM) {
        return false;
    }
    probe = *parser;
    if (!minic_parser_advance(&probe)) {
        return false;
    }
    if (probe.current.kind == MINIC_TOKEN_LBRACE) {
        *is_definition = true;
        return true;
    }
    if (probe.current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_error(parser, "expected enum tag or definition after enum keyword");
        return false;
    }
    if (!minic_parser_advance(&probe)) {
        return false;
    }
    *is_definition = probe.current.kind == MINIC_TOKEN_LBRACE;
    return true;
}

'''
text = replace_once(text, anchor, helper + anchor, "enum-definition-classifier")
text = replace_once(
    text,
    '''        } else if (parser.current.kind == MINIC_TOKEN_KW_ENUM) {\n            success = minic_parser_parse_enum_definition(&parser);\n''',
    '''        } else if (parser.current.kind == MINIC_TOKEN_KW_ENUM) {\n            bool is_definition;\n\n            if (!enum_keyword_starts_definition(&parser, &is_definition)) {\n                success = false;\n            } else if (is_definition) {\n                success = minic_parser_parse_enum_definition(&parser);\n            } else {\n                success = parse_function(&parser, false);\n            }\n''',
    "enum-top-level-dispatch",
)
path.write_text(text)

path = root / "tests/compiler/c0/enum_tag_type_references.c"
text = path.read_text()
text = replace_once(
    text,
    '''extern void add_taint_like(unsigned flag, enum lockdep_like state);\n\n''',
    '''extern void add_taint_like(unsigned flag, enum lockdep_like state);\n\nenum lockdep_like report_bug_like(unsigned long address, enum lockdep_like state);\n\n''',
    "enum-bare-top-level-prototype",
)
path.write_text(text)

path = root / "tests/compiler/c0/run-enum-tag-type-references.sh"
text = path.read_text()
text = replace_once(
    text,
    "definition=tagged reference=parameter+return representation=int unknown-tag=reject-by-registry",
    "definition=tagged reference=parameter+return top-level-bare-return=1 representation=int unknown-tag=reject-by-registry",
    "enum-gate-summary",
)
path.write_text(text)
