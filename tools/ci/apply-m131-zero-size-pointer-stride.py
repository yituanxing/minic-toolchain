#!/usr/bin/env python3
from pathlib import Path

path = Path('src/frontend/expression_semantics.c')
text = path.read_text()
marker = 'M131_ZERO_SIZE_POINTER_STRIDE'

if marker not in text:
    needle = '''    return minic_data_layout_type(layout, program, pointee, element_size, &alignment) &&
           *element_size != 0U;
'''
    if needle not in text:
        raise SystemExit('pointer arithmetic element-size seam changed')
    replacement = '''    /* M131_ZERO_SIZE_POINTER_STRIDE: GNU complete zero-size object types
       (notably empty records, and the zero-size array forms already accepted by
       the frontend) have a real pointer-arithmetic stride of zero. Completeness
       is owned by the caller/frontend; DataLayout returning a valid size of zero
       must not be reinterpreted here as an unsupported type. Incomplete types
       still fail because DataLayout cannot size them. */
    return minic_data_layout_type(layout, program, pointee, element_size, &alignment);
'''
    text = text.replace(needle, replacement, 1)
    path.write_text(text)

Path('tests/compiler/c0/m131_zero_size_record_subscript.c').write_text(r'''struct empty_record { };

static int index_calls;

static int next_index(void) {
    index_calls += 1;
    return 3;
}

int main(void) {
    struct empty_record object;
    struct empty_record *pointer = &object;
    (void)&pointer[next_index()];
    return index_calls == 1 ? 0 : 1;
}
''')

print('M131 zero-size pointer stride owner and strict regression staged')
