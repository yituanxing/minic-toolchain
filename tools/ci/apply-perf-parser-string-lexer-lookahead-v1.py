#!/usr/bin/env python3
"""One-factor opt-in experiment: remove 133-KiB Parser memcpy during asm string lookahead.

Unlike the already-rejected top-level enum/record lexer lookahead, this site
is a measured very-hot parser_string.c callsite:
  fs/ext4/fsync ~5,128 calls, kernel/bpf/btf ~6,257 calls (diagnostic census).
The original probe only advances lexer/current token, and decoded length
reads the immutable source + span. Diagnostic on failure is already issued by
the original Parser, so error behavior and token commit policy are unchanged.
Apply to a disposable CI checkout; never auto-promote to production.
"""
from pathlib import Path
p=Path("src/frontend/parser_string.c")
source=p.read_text()
begin="bool minic_parser_parse_string_text(MinicParser *parser,"
end="bool minic_parser_parse_string_literal(MinicParser *parser,"
assert source.count(begin)==1 and source.count(end)==1
a,suffix=source.split(begin,1)
body,c=suffix.split(end,1)
def replace(old,new,expected=1):
    global body
    if body.count(old)!=expected:
        raise SystemExit("STRING_LEXER_AB=FAIL anchor %r count=%d expected=%d"%
                         (old[:110],body.count(old),expected))
    body=body.replace(old,new)
replace("    MinicParser probe;\n", "    MinicLexer probe_lexer;\n    MinicToken probe_token;\n")
replace("    probe = *parser;\n", "    probe_lexer = parser->lexer;\n    probe_token = parser->current;\n")
replace("probe.current.kind", "probe_token.kind",2)
replace("probe.current.span", "probe_token.span",3)
replace("                &probe, probe_token.span, probe_token.kind, &decoded_length)",
        "                parser, probe_token.span, probe_token.kind, &decoded_length)")
replace("        if (!minic_parser_advance(&probe)) {",
        "        if (!minic_lexer_next(&probe_lexer, &probe_token, parser->diagnostic)) {")
if "probe." in body or "&probe" in body:
    raise SystemExit("STRING_LEXER_AB=FAIL unexplained Parser-probe field left")
out=a+begin+body+end+c
assert out!=source
p.write_text(out)
print("MINIC_PERF_PARSER_STRING_LEXER_LOOKAHEAD_V1=APPLIED")
