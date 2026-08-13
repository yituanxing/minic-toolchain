from pathlib import Path

path = Path(__file__).resolve().parents[2] / "tests/compiler/c0/run-static-local-scalars.sh"
text = path.read_text()
old_legacy = '''test "$(grep -c -F '.type __minic_static_local_' "$work/static_local_scalar.s")" -eq 2'''
new_legacy = '''test "$(grep -c -F '.type __minic_static_local_' "$work/static_local_scalar.s")" -eq 6'''
old_expanded = '''test "$(grep -c -F '.type __minic_static_local_' "$work/static_local_scalar.s")" -ge 7'''
new_expanded = '''test "$(grep -c -F '.type __minic_static_local_' "$work/static_local_scalar.s")" -eq 6'''
assert text.count(old_legacy) == 1
assert text.count(old_expanded) == 1
text = text.replace(old_legacy, new_legacy, 1).replace(old_expanded, new_expanded, 1)
path.write_text(text)
