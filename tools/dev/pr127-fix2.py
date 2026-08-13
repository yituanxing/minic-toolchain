from pathlib import Path

verifier = Path("src/frontend/ast_verifier.c")
text = verifier.read_text()
if "#include <string.h>" not in text:
    anchor = "#include <stdio.h>\n"
    if text.count(anchor) != 1:
        raise SystemExit("ast_verifier include anchor mismatch")
    text = text.replace(anchor, anchor + "#include <string.h>\n", 1)
verifier.write_text(text)

parser = Path("src/frontend/parser_function.c")
text = parser.read_text()
if "#include <stdlib.h>" not in text:
    anchor = "#include <limits.h>\n#include <string.h>\n"
    if text.count(anchor) != 1:
        raise SystemExit("parser_function include block mismatch")
    text = text.replace(anchor, "#include <limits.h>\n#include <stdlib.h>\n#include <string.h>\n", 1)
parser.write_text(text)

Path("tests/compiler/c0/file_scope_basic_asm.c").write_text(r'''int file_asm_target;

asm(".section \".export_symbol\",\"a\" ; __export_symbol_file_asm_target: ; "
    ".asciz \"\" ; .ascii \"\" \"\\0\" ; .balign 8 ; .quad file_asm_target ; .previous");

__asm__(".section \".minic.fileasm\",\"a\" ; __minic_file_asm_second: ; "
        ".ascii \"%\" ; .previous");

__asm(".section \".minic.fileasm\",\"a\" ; __minic_file_asm_third: ; .previous");
''')
