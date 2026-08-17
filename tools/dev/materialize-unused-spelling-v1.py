from pathlib import Path

path = Path('src/frontend/attribute.c')
text = path.read_text()
anchor = '''    MINIC_ATTRIBUTE_ENTRY("__unused__",
                          MINIC_ATTRIBUTE_UNUSED,
                          MINIC_ATTRIBUTE_CLASS_INFORMATIONAL,
                          MINIC_ATTRIBUTE_TARGET_FUNCTION | MINIC_ATTRIBUTE_TARGET_OBJECT |
                              MINIC_ATTRIBUTE_TARGET_TYPE | MINIC_ATTRIBUTE_TARGET_FIELD),
'''
replacement = '''    MINIC_ATTRIBUTE_ENTRY("unused",
                          MINIC_ATTRIBUTE_UNUSED,
                          MINIC_ATTRIBUTE_CLASS_INFORMATIONAL,
                          MINIC_ATTRIBUTE_TARGET_FUNCTION | MINIC_ATTRIBUTE_TARGET_OBJECT |
                              MINIC_ATTRIBUTE_TARGET_TYPE | MINIC_ATTRIBUTE_TARGET_FIELD),
''' + anchor
if text.count(anchor) != 1:
    raise SystemExit(f'unused descriptor anchor mismatch: {text.count(anchor)}')
path.write_text(text.replace(anchor, replacement, 1))
