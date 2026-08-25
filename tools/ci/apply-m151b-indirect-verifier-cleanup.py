#!/usr/bin/env python3
from pathlib import Path

path = Path('src/core/core_ir.c')
text = path.read_text()
start = text.find('case MINIC_CORE_INSTRUCTION_INDIRECT_CALL: {')
if start < 0:
    raise SystemExit('M151b could not locate indirect-call verifier')
end = text.find('\n    case ', start + 1)
if end < 0:
    raise SystemExit('M151b could not locate indirect-call verifier end')
body = text[start:end]
old = '''            const MinicCoreCallArgument *argument;\n            MinicCoreValueId value_id;\n            size_t parameter_index;'''
new = '''            const MinicCoreCallArgument *argument;\n            size_t parameter_index;'''
if body.count(old) != 1:
    raise SystemExit(f'M151b expected one stale verifier value declaration, found {body.count(old)}')
body = body.replace(old, new, 1)
text = text[:start] + body + text[end:]
path.write_text(text)
print('M151b indirect verifier cleanup staged')
