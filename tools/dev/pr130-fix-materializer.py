from pathlib import Path

path = Path('tools/dev/pr130-materialize.py')
text = path.read_text()
start_marker = "fixture = root / 'tests/compiler/c0/static_global_object_section.c'\n"
end_marker = "\ninvalid = root / 'tests/compiler/c0/invalid_static_global_alignment.c'\n"
start = text.find(start_marker)
end = text.find(end_marker, start)
if start < 0 or end < 0:
    raise SystemExit('PR130 fixture materializer region mismatch')
replacement = '''fixture = root / 'tests/compiler/c0/static_global_object_section.c'\nfixture.write_text(\n    'static int __attribute__((__section__(".data.static.init"))) section_initialized = 7;\\n'\n    'static int __attribute__((section(".data.static.zero"))) section_zero;\\n'\n    'static void *__attribute__((__used__))\\n'\n    '__attribute__((__section__(".discard.addressable"))) addressable_shape = (void *)0;\\n'\n    'static int __attribute__((aligned(16))) aligned_static = 1;\\n'\n    'static const char linux_setup_string[]\\n'\n    '__attribute__((section(".init.rodata")))\\n'\n    '__attribute__((__aligned__(1))) = "reset_devices";\\n\\n'\n    'int read_static_global_sections(void) {\\n'\n    '    return section_initialized + section_zero + aligned_static + linux_setup_string[0] +\\n'\n    '           (addressable_shape == (void *)0);\\n'\n    '}\\n'\n)\n'''
path.write_text(text[:start] + replacement + text[end:])
