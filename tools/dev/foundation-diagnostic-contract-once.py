#!/usr/bin/env python3
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, found {count}")
    return text.replace(old, new, 1)


def replace_range(text: str, start: str, end: str, new: str, label: str) -> str:
    begin = text.find(start)
    if begin < 0:
        raise SystemExit(f"{label}: start anchor not found")
    finish = text.find(end, begin)
    if finish < 0:
        raise SystemExit(f"{label}: end anchor not found")
    return text[:begin] + new + text[finish:]


root = Path(__file__).resolve().parents[2]

# Parser entrypoint: a false return may never escape without a source-positioned diagnostic.
path = root / "src/frontend/parser_function.c"
text = path.read_text()
old = '''    minic_parser_destroy_scopes(&parser);\n    minic_parser_destroy_enum_constants(&parser);\n    return success;\n}\n'''
new = '''    if (!success && diagnostic != NULL && diagnostic->message[0] == '\\0') {\n        minic_parser_error(&parser, "parser failed without diagnostic");\n    }\n    minic_parser_destroy_scopes(&parser);\n    minic_parser_destroy_enum_constants(&parser);\n    return success;\n}\n'''
text = replace_once(text, old, new, "parser-failure-diagnostic-contract")
path.write_text(text)

# Remove an old one-off Linux archaeology dump that always inspected statement 1482.
path = root / "src/target/riscv64/codegen_function.c"
text = path.read_text()
start = '''            if (program->statement_count > 1482U) {\n'''
end = '''            }\n        }\n    }\n\n    if (!success) {\n'''
begin = text.find(start)
if begin < 0:
    raise SystemExit("legacy-codegen-debug: start anchor not found")
finish = text.find(end, begin)
if finish < 0:
    raise SystemExit("legacy-codegen-debug: end anchor not found")
# Preserve the closing braces for the function failure block / loop.
text = text[:begin] + '''        }\n    }\n\n    if (!success) {\n''' + text[finish + len(end):]
if "statement=1482" in text or "CODEGEN_DETAIL" in text or "CODEGEN_MEMBER" in text:
    raise SystemExit("legacy-codegen-debug: hard-coded archaeology remains")
path.write_text(text)
