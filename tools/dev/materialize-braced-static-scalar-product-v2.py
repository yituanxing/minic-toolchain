from pathlib import Path

path = Path("src/frontend/parser_global.c")
text = path.read_text()
old = """static bool parse_static_scalar(MinicParser *parser, MinicType type, MinicSourceSpan name_span) {\n    MinicGlobalObjectId object_id;\n"""
new = """static bool parse_static_scalar(MinicParser *parser, MinicType type, MinicSourceSpan name_span) {\n    MinicGlobalObjectId object_id;\n    bool braced;\n"""
if text.count(old) != 1:
    raise SystemExit(f"scalar declaration anchor count={text.count(old)}")
text = text.replace(old, new, 1)

old = """    if (minic_type_is_integer(type)) {\n        uint64_t bits;\n\n        if (parser->current.kind == MINIC_TOKEN_LBRACE) {\n            minic_parser_error(parser, \"expected integer constant expression\");\n            return false;\n        }\n"""
new = """    braced = parser->current.kind == MINIC_TOKEN_LBRACE;\n    if (braced && !minic_parser_advance(parser)) {\n        return false;\n    }\n    if (minic_type_is_integer(type)) {\n        uint64_t bits;\n\n"""
if text.count(old) != 1:
    raise SystemExit(f"scalar brace reject anchor count={text.count(old)}")
text = text.replace(old, new, 1)

old = """    } else {\n        minic_parser_error(parser, \"unsupported static scalar type\");\n        return false;\n    }\n    return minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, \"expected ';' after global object\");\n}\n"""
new = """    } else {\n        minic_parser_error(parser, \"unsupported static scalar type\");\n        return false;\n    }\n    if (braced) {\n        if (parser->current.kind == MINIC_TOKEN_COMMA && !minic_parser_advance(parser)) {\n            return false;\n        }\n        if (!minic_parser_expect(\n                parser, MINIC_TOKEN_RBRACE, \"expected '}' after static scalar initializer\")) {\n            return false;\n        }\n    }\n    return minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, \"expected ';' after global object\");\n}\n"""
if text.count(old) != 1:
    raise SystemExit(f"scalar finalize anchor count={text.count(old)}")
path.write_text(text.replace(old, new, 1))

path = Path("tests/compiler/c0/run.sh")
text = path.read_text()
old = """expect_compile_failure \\\n    invalid_braced_scalar_static_global \\\n    \"expected integer constant expression\"\n"""
new = """compile_source braced_scalar_static_global invalid_braced_scalar_static_global\ngrep -F \"  .word 1\" \"$work/braced_scalar_static_global.s\" >/dev/null\nprintf '%s\\n' \"PASS compiler/c0/braced_scalar_static_global\"\n"""
if text.count(old) != 1:
    raise SystemExit(f"old braced scalar contract count={text.count(old)}")
text = text.replace(old, new, 1)
call = """\nMINIC=\"$minic\" HOST_CC=\"$host_cc\" \\\nBUILD_DIR=\"${BUILD_DIR:-\"$root/build/debug\"}\" \\\nsh \"$root/tests/compiler/c0/run-braced-static-scalar.sh\"\n"""
if call not in text:
    text += call
path.write_text(text)

Path("tests/compiler/c0/braced_static_scalar.c").write_text(
    """static unsigned long value = {7};\nstatic int *pointer = {\n    0,\n};\nint main(void) { return value == 7 && pointer == 0 ? 0 : 1; }\n"""
)
Path("tests/compiler/c0/invalid_braced_static_scalar_extra.c").write_text(
    "static int value = {1, 2};\n"
)
script = Path("tests/compiler/c0/run-braced-static-scalar.sh")
script.write_text(
    """#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
cc=${HOST_CC:-cc}
work=${BUILD_DIR:-"$root/build/debug"}/tests/braced-static-scalar
mkdir -p "$work"
"$cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/braced_static_scalar.c" -o "$work/positive.i"
"$minic" -S "$work/positive.i" -o "$work/positive.s"
grep -F 'value:' "$work/positive.s" >/dev/null
grep -F 'pointer:' "$work/positive.s" >/dev/null
"$cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/invalid_braced_static_scalar_extra.c" -o "$work/negative.i"
if "$minic" -S "$work/negative.i" -o "$work/negative.s" 2>"$work/negative.err"; then exit 1; fi
grep -F "expected '}' after static scalar initializer" "$work/negative.err" >/dev/null
printf '%s\n' 'PASS compiler/c0/braced-static-scalar scalar=integer,pointer trailing-comma=1 excess=reject'
"""
)
script.chmod(0o755)
