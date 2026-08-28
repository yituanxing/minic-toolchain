#!/usr/bin/env python3
from pathlib import Path

path = Path('tools/ci/apply-m151-indirect-call-batch.py')
text = path.read_text()
old = '''# Add a durable semantic marker next to the signature table validation.\nneedle = '    for (index = 0U; index < function->call_signature_count; ++index) {'\nif text.count(needle) != 1:\n    raise SystemExit('M151 could not mark call-signature owner')\ntext = text.replace(needle,\n                    '    /* M151_INDIRECT_CALL_BATCH_OWNER: indirect fixed parameters share the direct scalar/record domain. */\\n' + needle,\n                    1)'''
new = '''# Add a durable semantic marker at the unique signature creation owner.\nneedle = 'bool minic_core_function_add_call_signature(MinicCoreFunction *function,'\nif text.count(needle) != 1:\n    raise SystemExit('M151 could not mark unique call-signature owner')\ntext = text.replace(needle,\n                    '/* M151_INDIRECT_CALL_BATCH_OWNER: indirect fixed parameters share the direct scalar/record domain. */\\n' + needle,\n                    1)'''
if text.count(old) != 1:
    raise SystemExit(f'M151 staging marker block mismatch: {text.count(old)}')
path.write_text(text.replace(old, new, 1))
print('M151 staging marker site fixed')
