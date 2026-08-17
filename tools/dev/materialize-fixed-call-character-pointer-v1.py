#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]
path = root / "src/frontend/parser_expression.c"
text = path.read_text()

old = '''        !minic_type_pointee(source, &source_pointee) || !minic_type_is_integer(target_pointee) ||
        !minic_type_is_integer(source_pointee) ||
        target_pointee.integer_rank != source_pointee.integer_rank ||
        target_pointee.integer_sign == source_pointee.integer_sign ||
        target_pointee.is_plain_char != source_pointee.is_plain_char) {
        return false;
    }
'''
new = '''        !minic_type_pointee(source, &source_pointee) || !minic_type_is_integer(target_pointee) ||
        !minic_type_is_integer(source_pointee) ||
        target_pointee.integer_rank != source_pointee.integer_rank) {
        return false;
    }
    /* Fixed C calls in GNU/Linux routinely pass plain char buffers through APIs
       whose byte-oriented parameter is signed or unsigned char. Keep that
       compatibility local to the explicit call conversion: ordinary pointer
       assignment still distinguishes the three character types. */
    if (target_pointee.integer_rank != MINIC_INTEGER_RANK_CHAR &&
        (target_pointee.integer_sign == source_pointee.integer_sign ||
         target_pointee.is_plain_char != source_pointee.is_plain_char)) {
        return false;
    }
'''
if text.count(old) != 1:
    raise SystemExit(f"pointer sign call conversion guard count={text.count(old)}")
path.write_text(text.replace(old, new, 1))
