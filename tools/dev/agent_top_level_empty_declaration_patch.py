from pathlib import Path

root = Path(__file__).resolve().parents[2]
parser_path = root / "src/frontend/parser_function.c"
focused_path = root / "tools/dev/pr76-focused.sh"
fixture_path = root / "tests/compiler/c0/gnu_top_level_empty_declaration.c"
runner_path = root / "tests/compiler/c0/run-gnu-top-level-empty-declaration.sh"

parser = parser_path.read_text()
old = (
    "        if (parser.current.kind == MINIC_TOKEN_KW_STATIC_ASSERT) {\n"
    "            success = minic_parser_parse_static_assert_declaration(&parser);\n"
)
new = (
    "        if (parser.current.kind == MINIC_TOKEN_SEMICOLON) {\n"
    "            success = minic_parser_advance(&parser);\n"
    "        } else if (parser.current.kind == MINIC_TOKEN_KW_STATIC_ASSERT) {\n"
    "            success = minic_parser_parse_static_assert_declaration(&parser);\n"
)
if parser.count(old) != 1:
    raise SystemExit("top-level dispatch anchor mismatch")
parser_path.write_text(parser.replace(old, new, 1))

fixture_path.write_text(
    ";\n"
    "static int first(void)\n"
    "{\n"
    "    return 11;\n"
    "}\n"
    ";;\n"
    "typedef int probe_int_t;\n"
    ";\n"
    "static probe_int_t second(void)\n"
    "{\n"
    "    return first() + 31;\n"
    "}\n"
    ";\n"
    "int main(void)\n"
    "{\n"
    "    return second() == 42 ? 0 : 1;\n"
    "}\n"
    ";\n"
)

runner_path.write_text(
    "#!/bin/sh\n"
    "set -eu\n\n"
    "root=$(CDPATH= cd -- \"$(dirname -- \"$0\")/../../..\" && pwd)\n"
    "minic=${MINIC:-\"$root/build/debug/bin/minic\"}\n"
    "host_cc=${HOST_CC:-${CC:-cc}}\n"
    "build_dir=${BUILD_DIR:-\"$root/build/debug\"}\n"
    "work=\"$build_dir/tests/compiler-c0-gnu-top-level-empty-declaration\"\n\n"
    "mkdir -p \"$work\"\n\n"
    "\"$host_cc\" -E -P -x c \\\n"
    "    \"$root/tests/compiler/c0/gnu_top_level_empty_declaration.c\" \\\n"
    "    -o \"$work/input.i\"\n"
    "\"$minic\" -S \"$work/input.i\" -o \"$work/output.s\"\n\n"
    "grep -F \"first:\" \"$work/output.s\" >/dev/null\n"
    "grep -F \"second:\" \"$work/output.s\" >/dev/null\n"
    "grep -F \"main:\" \"$work/output.s\" >/dev/null\n\n"
    "printf '%s\\n' \\\n"
    "    \"PASS compiler/c0/gnu_top_level_empty_declaration leading=1 consecutive=2 after-function=1 between-declarations=1 trailing=1\"\n"
)
runner_path.chmod(0o755)

focused = focused_path.read_text()
anchor = "sh tests/compiler/c0/run-gnu-static-local-implicit-zero.sh\n"
insert = anchor + "sh tests/compiler/c0/run-gnu-top-level-empty-declaration.sh\n"
if focused.count(anchor) != 1:
    raise SystemExit("focused runner anchor mismatch")
focused_path.write_text(focused.replace(anchor, insert, 1))

print("PASS generated top-level empty declaration slice")
