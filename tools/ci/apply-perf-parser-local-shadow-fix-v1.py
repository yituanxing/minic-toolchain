#!/usr/bin/env python3
"""Fix nearest-binding lookup semantics on the isolated audit branch.

A nearer typedef or block-scope extern must hide a farther local variable.
Keeping the old test order incorrectly leaks an outer local into inner scopes.
"""
from pathlib import Path
p=Path("src/frontend/parser_core.c")
src=p.read_text()
start=src.find("MinicLocalId minic_parser_find_local(const MinicParser *parser, MinicSourceSpan name_span) {")
end=src.find("\n}\n\nMinicGlobalObjectId minic_parser_find_scoped_global_object(",start)
if start<0 or end<start:
    raise SystemExit("cannot isolate nearest local lookup")
fragment=src[start:end]
old="""        if (binding->local_id != MINIC_LOCAL_INVALID &&
            minic_parser_span_equals(parser, name_span, binding->name_span)) {
            return binding->local_id;
        }"""
new="""        /* Resolve by the nearest binding, even if that binding is a typedef
         * or scoped extern.  Those names hide an outer local variable. */
        if (minic_parser_span_equals(parser, name_span, binding->name_span)) {
            return binding->local_id;
        }"""
if fragment.count(old)!=1:
    raise SystemExit(f"local lookup exact anchor count={fragment.count(old)}")
p.write_text(src[:start]+fragment.replace(old,new,1)+src[end:])
print("MINIC_PARSER_LOCAL_SHADOW_V1=APPLIED")
