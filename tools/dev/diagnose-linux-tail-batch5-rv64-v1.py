#!/usr/bin/env python3
from pathlib import Path

# Temporary diagnostics only. This file is removed by the productizer and the
# modified diagnostic sources are never staged into the clean product commit.

p = Path('src/target/riscv64/codegen_function.c')
text = p.read_text()
old = '''    initializer_index = 0U;\n    relocation_index = 0U;\n    emitted_size = 0U;\n    return minic_riscv64_emit_constant_value(file,\n                                             program,\n                                             object,\n                                             object->type,\n                                             &initializer_index,\n                                             &relocation_index,\n                                             &emitted_size) &&\n           initializer_index == object->initializer_count &&\n           relocation_index == object->relocation_count && emitted_size == storage_size;\n}\n\nstatic bool minic_riscv64_emit_file_asm'''
new = '''    initializer_index = 0U;\n    relocation_index = 0U;\n    emitted_size = 0U;\n    {\n        bool emitted;\n        bool complete;\n\n        emitted = minic_riscv64_emit_constant_value(file,\n                                                    program,\n                                                    object,\n                                                    object->type,\n                                                    &initializer_index,\n                                                    &relocation_index,\n                                                    &emitted_size);\n        complete = emitted && initializer_index == object->initializer_count &&\n                   relocation_index == object->relocation_count && emitted_size == storage_size;\n        if (!complete) {\n            (void)fprintf(stderr,\n                          "MINIC_RV64_RECURSIVE_ARRAY_FAIL name=%s emitted=%d init=%zu/%zu rel=%zu/%zu size=%zu/%zu\\n",\n                          object->name,\n                          emitted ? 1 : 0,\n                          initializer_index,\n                          object->initializer_count,\n                          relocation_index,\n                          object->relocation_count,\n                          emitted_size,\n                          storage_size);\n        }\n        return complete;\n    }\n}\n\nstatic bool minic_riscv64_emit_file_asm'''
if text.count(old) != 1:
    raise SystemExit(f'expected recursive array emitter block once, found {text.count(old)}')
p.write_text(text.replace(old, new, 1))

p = Path('src/frontend/parser_global.c')
text = p.read_text()
if '#include <stdio.h>' not in text:
    marker = '#include <string.h>\n'
    if text.count(marker) != 1:
        raise SystemExit('cannot locate parser_global include block')
    text = text.replace(marker, '#include <stdio.h>\n' + marker, 1)
old = '''        slot_index = overwrite ? overwrite_slot\n                               : parser->program->global_objects[object_id].initializer_count;\n        if (!parse_static_pointer_initializer(parser, type, &initializer)) {\n            return false;\n        }\n        if (initializer.has_relocation) {\n            bool recorded;\n\n            if (!overwrite &&\n                !minic_c0_global_object_add_initializer_bits(parser->program, object_id, 0U)) {'''
new = '''        size_t debug_before_parse;\n\n        slot_index = overwrite ? overwrite_slot\n                               : parser->program->global_objects[object_id].initializer_count;\n        debug_before_parse = parser->program->global_objects[object_id].initializer_count;\n        if (!parse_static_pointer_initializer(parser, type, &initializer)) {\n            return false;\n        }\n        if (initializer.has_relocation) {\n            bool recorded;\n\n            (void)fprintf(stderr,\n                          "MINIC_STATIC_POINTER_TRACE name=%s before=%zu after_parse=%zu slot=%zu overwrite=%d\\n",\n                          parser->program->global_objects[object_id].name,\n                          debug_before_parse,\n                          parser->program->global_objects[object_id].initializer_count,\n                          slot_index,\n                          overwrite ? 1 : 0);\n            if (!overwrite &&\n                !minic_c0_global_object_add_initializer_bits(parser->program, object_id, 0U)) {'''
if text.count(old) != 1:
    raise SystemExit(f'expected pointer relocation prelude once, found {text.count(old)}')
text = text.replace(old, new, 1)
old = '''            if (!recorded) {\n                minic_parser_error(parser, "cannot record nested static symbolic relocation");\n                return false;\n            }\n        } else if (overwrite) {'''
new = '''            (void)fprintf(stderr,\n                          "MINIC_STATIC_POINTER_TRACE name=%s after_reserve=%zu after_reloc=%zu recorded=%d\\n",\n                          parser->program->global_objects[object_id].name,\n                          parser->program->global_objects[object_id].initializer_count,\n                          parser->program->global_objects[object_id].initializer_count,\n                          recorded ? 1 : 0);\n            if (!recorded) {\n                minic_parser_error(parser, "cannot record nested static symbolic relocation");\n                return false;\n            }\n        } else if (overwrite) {'''
if text.count(old) != 1:
    raise SystemExit(f'expected pointer relocation terminal once, found {text.count(old)}')
p.write_text(text.replace(old, new, 1))
print('materialized temporary frontend/RV64 aggregate diagnostics')
