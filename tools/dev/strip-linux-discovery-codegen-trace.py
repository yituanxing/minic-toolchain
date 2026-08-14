#!/usr/bin/env python3
from pathlib import Path

FILES = (
    Path("src/target/riscv64/codegen_expression.c"),
    Path("src/target/riscv64/codegen_function.c"),
    Path("src/target/riscv64/codegen_statement.c"),
)


def strip_codegen_fprintf(text: str) -> tuple[str, int]:
    marker = "fprintf(stderr,"
    removed = 0
    search_from = 0
    while True:
        call = text.find(marker, search_from)
        if call < 0:
            break
        line_start = text.rfind("\n", 0, call) + 1
        depth = 0
        quote = None
        escape = False
        end = None
        i = call
        while i < len(text):
            ch = text[i]
            if quote is not None:
                if escape:
                    escape = False
                elif ch == "\\":
                    escape = True
                elif ch == quote:
                    quote = None
            else:
                if ch in ('"', "'"):
                    quote = ch
                elif ch == "(":
                    depth += 1
                elif ch == ")":
                    depth -= 1
                elif ch == ";" and depth == 0:
                    end = i + 1
                    if end < len(text) and text[end] == "\n":
                        end += 1
                    break
            i += 1
        if end is None:
            raise SystemExit("unterminated fprintf(stderr, ...) statement")
        statement = text[line_start:end]
        if "CODEGEN_" not in statement:
            search_from = end
            continue
        text = text[:line_start] + text[end:]
        removed += 1
        search_from = line_start
    return text, removed


def simplify_frame_layout_failure(text: str) -> str:
    start_marker = "    if (!minic_riscv64_frame_layout_from_function_layout("
    start = text.find(start_marker)
    if start < 0:
        return text
    tail = (
        "        minic_riscv64_function_layout_destroy(&function_layout);\n"
        "        return false;\n"
        "    }\n"
    )
    tail_at = text.find(tail, start)
    if tail_at < 0:
        raise SystemExit("frame-layout failure tail not found")
    brace = text.find(" {\n", start, tail_at)
    if brace < 0:
        raise SystemExit("frame-layout failure opening brace not found")
    body_start = brace + 3
    return text[:body_start] + tail + text[tail_at + len(tail):]


def strip_statement_diagnostic_locals(text: str) -> str:
    snippets = (
        "            const MinicExpression *cleanup =\n"
        "                minic_c0_program_expression(program, context->cleanup_expression);\n",
        "            const MinicExpression *failed_value =\n"
        "                minic_c0_program_expression(program, statement->expression);\n",
    )
    for snippet in snippets:
        text = text.replace(snippet, "")
    return text


total = 0
for path in FILES:
    text = path.read_text()
    text, removed = strip_codegen_fprintf(text)
    if path.name == "codegen_function.c":
        text = simplify_frame_layout_failure(text)
    if path.name == "codegen_statement.c":
        text = strip_statement_diagnostic_locals(text)
    if "CODEGEN_" in text:
        raise SystemExit(f"CODEGEN trace remains in {path}")
    path.write_text(text)
    total += removed

if total == 0:
    print("NOOP Linux discovery codegen trace already absent")
else:
    print(f"STRIPPED Linux discovery codegen trace statements={total}")
