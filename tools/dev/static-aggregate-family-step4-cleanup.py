from pathlib import Path

p = Path('src/frontend/parser_global.c')
text = p.read_text()
start_marker = 'static bool\nparse_static_pointer_constant_bits('
end_marker = '\nstatic bool parse_static_pointer_initializer('
start = text.find(start_marker)
if start < 0:
    raise SystemExit('step4 cleanup start marker missing')
end = text.find(end_marker, start)
if end < 0:
    raise SystemExit('step4 cleanup unified initializer marker missing')
p.write_text(text[:start] + text[end + 1:])
