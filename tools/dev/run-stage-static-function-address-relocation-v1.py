#!/usr/bin/env python3
from pathlib import Path

source = Path('tools/dev/stage-static-function-address-relocation-v1.py').read_text()
old = '''old = \'\'\'                if (!minic_c0_global_object_set_zero_initialized(parser->program, object_id) ||
                    !minic_c0_global_object_add_object_relocation(parser->program,
                                                                  object_id,
                                                                  MINIC_GLOBAL_RELOCATION_LOCATION_SCALAR,
                                                                  0U,
                                                                  target_object_id)) {
\'\'\'
'''
new = '''old = \'\'\'                if (!minic_c0_global_object_set_zero_initialized(parser->program, object_id) ||
                    !minic_c0_global_object_add_object_relocation(
                        parser->program,
                        object_id,
                        MINIC_GLOBAL_RELOCATION_LOCATION_SCALAR,
                        0U,
                        target_object_id)) {
\'\'\'
'''
if source.count(old) != 1:
    raise SystemExit('cannot adapt scalar object relocation template')
source = source.replace(old, new, 1)
exec(compile(source, 'stage-static-function-address-relocation-v1.py', 'exec'), {'__name__': '__main__'})
