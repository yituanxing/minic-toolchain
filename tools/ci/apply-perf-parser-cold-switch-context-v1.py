#!/usr/bin/env python3
"""One-factor, disposable-checkout Parser hot/cold switch-context trial.

An old MinicParser contains sixteen SwitchContext values, each carrying
2*512 uint64_t cases. That cold 131KiB dominates every by-value Parser
lookahead copy. This trial moves only that cold array behind one lazily
allocated, Parser-owned pointer. No other parser state or diagnostic
semantics are changed. Production source is never mutated by committing
this experiment script; only opt-in CI sandbox applies it.
"""
from pathlib import Path

def replace(path: str, old: str, new: str):
    p=Path(path)
    s=p.read_text()
    n=s.count(old)
    if n!=1:
        raise SystemExit(f"MINIC_PARSER_COLD_SWITCH=ANCHOR_ERROR path={path} occurrences={n} old={old[:100]!r}")
    p.write_text(s.replace(old,new,1))

replace("src/frontend/parser_internal.h",
    "    MinicParserSwitchContext switch_contexts[MINIC_PARSER_MAX_SWITCH_DEPTH];",
    """    /* Cold, Parser-owned switch cases: no hot 131KiB inline copy when
       speculative MinicParser snapshots only inspect lexer/token state. */
    MinicParserSwitchContext *switch_contexts;""")

replace("src/frontend/parser_statement.c",
    """    context = &parser->switch_contexts[parser->switch_depth];
    (void)memset(context, 0, sizeof(*context));""",
    """    /* Only allocate the large case-range table if a real switch exists.
       Sub-parser lexer-only lookaheads borrow this backing store. */
    if (parser->switch_contexts == NULL) {
        parser->switch_contexts = (MinicParserSwitchContext *)calloc(
            MINIC_PARSER_MAX_SWITCH_DEPTH, sizeof(*parser->switch_contexts));
        if (parser->switch_contexts == NULL) {
            minic_parser_error(parser, "out of memory allocating switch case contexts");
            return false;
        }
    }
    context = &parser->switch_contexts[parser->switch_depth];
    (void)memset(context, 0, sizeof(*context));""")

replace("src/frontend/parser_function.c",
    """    minic_parser_destroy_scopes(&parser);
    minic_parser_destroy_enum_constants(&parser);
    return success;""",
    """    minic_parser_destroy_scopes(&parser);
    minic_parser_destroy_enum_constants(&parser);
    free(parser.switch_contexts);
    parser.switch_contexts = NULL;
    return success;""")

print("MINIC_PARSER_COLD_SWITCH=APPLIED owners=main_parser shallow_snapshots=lexer_only malloc=on_first_switch")
