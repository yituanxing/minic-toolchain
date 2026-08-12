#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]
path = root / "tools/dev/pr110-materialize-gnu-weak-function-symbol.py"
text = path.read_text()
old_block = '''# Both deferred/prefix function attribute applications persist weak.
old_apply_tail = """                &section_name_length,\\n                &has_section,\\n                \\"unsupported GNU prefix function attribute; semantic and ABI-affecting attributes \\\"\\n"""
new_apply_tail = """                &section_name_length,\\n                &has_section,\\n                &is_weak,\\n                \\"unsupported GNU prefix function attribute; semantic and ABI-affecting attributes \\\"\\n"""
path = ROOT / "src/frontend/parser_function.c"
text = path.read_text()
count = text.count(old_apply_tail)
if count != 2:
    raise SystemExit(f"parser_function.c: expected two persistent prefix attribute call sites, found {count}")
path.write_text(text.replace(old_apply_tail, new_apply_tail))
'''
new_block = '''# Both deferred/prefix function attribute applications persist weak. Their
# diagnostic strings wrap differently, so keep each current source shape exact.
path = ROOT / "src/frontend/parser_function.c"
text = path.read_text()
typed_apply_tail = """                &section_name_length,\\n                &has_section,\\n                \\"unsupported GNU prefix function attribute; semantic and ABI-affecting attributes \\\"\\n"""
typed_apply_weak = """                &section_name_length,\\n                &has_section,\\n                &is_weak,\\n                \\"unsupported GNU prefix function attribute; semantic and ABI-affecting attributes \\\"\\n"""
ordinary_apply_tail = """            &section_name_length,\\n            &has_section,\\n            \\"unsupported GNU prefix function attribute; semantic and ABI-affecting attributes must \\\"\\n"""
ordinary_apply_weak = """            &section_name_length,\\n            &has_section,\\n            &is_weak,\\n            \\"unsupported GNU prefix function attribute; semantic and ABI-affecting attributes must \\\"\\n"""
if text.count(typed_apply_tail) != 1 or text.count(ordinary_apply_tail) != 1:
    raise SystemExit(
        f"parser_function.c: prefix call anchors typed={text.count(typed_apply_tail)} ordinary={text.count(ordinary_apply_tail)}"
    )
text = text.replace(typed_apply_tail, typed_apply_weak, 1)
text = text.replace(ordinary_apply_tail, ordinary_apply_weak, 1)
path.write_text(text)
'''
if text.count(old_block) != 1:
    raise SystemExit(f"materializer: expected one prefix-call block, found {text.count(old_block)}")
path.write_text(text.replace(old_block, new_block, 1))
