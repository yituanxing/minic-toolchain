#!/usr/bin/env python3
from pathlib import Path

source = Path("tools/ci/apply-m158-final-four.py").read_text()
old = '''def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, found {count}")
    return text.replace(old, new, 1)
'''
new = '''def replace_once(text: str, old: str, new: str, label: str) -> str:
    if label == "ctzl lowering":
        marker = "M129_LEAF_EXPRESSION_OWNERS"
        marker_at = text.find(marker)
        if marker_at < 0:
            raise SystemExit("ctzl lowering: M129 owner marker missing")
        anchor_at = text.find(old, marker_at)
        if anchor_at < 0:
            raise SystemExit("ctzl lowering: owner-local call anchor missing")
        return text[:anchor_at] + new + text[anchor_at + len(old):]
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, found {count}")
    return text.replace(old, new, 1)
'''
if source.count(old) != 1:
    raise SystemExit("M158 v2 could not patch replace_once helper")
source = source.replace(old, new, 1)
exec(compile(source, "tools/ci/apply-m158-final-four.py", "exec"), {"__name__": "__main__"})
