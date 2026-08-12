from pathlib import Path

p = Path("tools/dev/pr115-materialize.py")
text = p.read_text()
marker = '    "focused-summary",\n)\n'
marker_at = text.find(marker)
if marker_at < 0:
    raise SystemExit("focused-summary patch marker missing")
block_start = text.rfind("text = replace_once(\n", 0, marker_at)
block_end = marker_at + len(marker)
replacement = '''text = replace_once(
    text,
    "layout-bearing=fail-closed",
    "section=global-object declaration-wide=2 aligned=fail-closed",
    "focused-summary",
)
'''
text = text[:block_start] + replacement + text[block_end:]
exec(compile(text, str(p), "exec"))
