#!/usr/bin/env python3
from pathlib import Path

p = Path("src/compiler/compiler.c")
text = p.read_text()
old = '''    return source->is_defined && source->is_internal && source->is_inline &&
           !minic_inline_source_has_noncore_root(program, source_id);
'''
new = '''    /* M179_INTERNAL_CORE_REACHABILITY: parser-level is_referenced is not an
     * executable root for any internal function once Core has been lowered.
     * Reachable CALL/FUNCTION_ADDRESS instructions pull static functions back
     * into the graph; aliases, entry/force_emit and global relocations remain
     * explicit non-Core roots.  This also prunes static non-inline bodies whose
     * only source-level calls disappeared after constant-CFG folding. */
    return source->is_defined && source->is_internal &&
           !minic_inline_source_has_noncore_root(program, source_id);
'''
count = text.count(old)
if count != 1:
    raise SystemExit(f"expected one internal reachability predicate, found {count}")
p.write_text(text.replace(old, new, 1))
print("M179_INTERNAL_CORE_REACHABILITY=APPLIED")
