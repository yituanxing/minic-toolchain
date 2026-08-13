from pathlib import Path

path = Path('src/frontend/parser_core.c')
text = path.read_text()
old = '#include <stdint.h>\n'
new = '#include <limits.h>\n#include <stdint.h>\n'
if text.count(old) != 1:
    raise SystemExit(f'parser_core stdint include anchor mismatch: {text.count(old)}')
path.write_text(text.replace(old, new, 1))
