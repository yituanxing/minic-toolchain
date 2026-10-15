#!/usr/bin/env python3
"""Adapt the proven union-selection slice to the current combined record parser."""
from pathlib import Path

source_path = Path("tools/dev/materialize-linux-first500-union-selection-v1.py")
source = source_path.read_text()
old = '''replace_between(
    parser,
    "static bool parse_static_union_constant(",
    "static bool parse_static_record_constant(",
    parse_union,
)
'''
new = '''replace_once(
    parser,
    "static bool parse_static_record_constant(",
    parse_union + "\\n\\nstatic bool parse_static_record_constant(",
)
replace_once(
    parser,
    """        return parse_static_record_constant(
            parser, object_id, minic_c0_program_record(parser->program, type.record_id));
""",
    """        {
            const MinicRecord *record;

            record = minic_c0_program_record(parser->program, type.record_id);
            if (record == NULL) {
                return false;
            }
            return record->is_union ? parse_static_union_constant(parser, object_id, record)
                                    : parse_static_record_constant(parser, object_id, record);
        }
""",
)
'''
if source.count(old) != 1:
    raise SystemExit("union-selection v1 parser-dispatch block changed unexpectedly")
source = source.replace(old, new, 1)
exec(compile(source, str(source_path), "exec"), {"__name__": "__main__"})
