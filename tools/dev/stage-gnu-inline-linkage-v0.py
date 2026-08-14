#!/usr/bin/env python3
from pathlib import Path
import re

parser_path = Path("src/frontend/parser_function.c")
text = parser_path.read_text()

needle = "    bool is_inline;\n    char *section_name;"
assert text.count(needle) == 1
text = text.replace(needle, "    bool is_inline;\n    bool is_extern;\n    char *section_name;", 1)

pattern = re.compile(
    r"        /\* GNU inline changes external-inline linkage semantics\. Linux's current\n"
    r"         \* accepted placement is static inline, where this parse-only attribute\n"
    r"         \* does not change externally visible linkage\. \*/\n"
    r"        if \(!context->is_internal \|\| !context->is_inline\) \{\n"
    r"            minic_parser_error\(parser,\n"
    r"                               \"GNU gnu_inline requires explicit non-static inline semantics\"\);\n"
    r"            return false;\n"
    r"        \}\n"
    r"        return true;\n"
)
replacement = """        /* GNU inline changes C linkage/emission semantics. Static inline and
         * non-extern inline definitions match MiniC's existing standalone emission
         * model. Extern inline requires an inline-only body model, which is not yet
         * represented, so keep that case fail-closed. */
        if (!context->is_inline) {
            minic_parser_error(parser, "GNU gnu_inline requires an inline declaration");
            return false;
        }
        if (context->is_extern) {
            minic_parser_error(
                parser,
                "GNU extern inline gnu_inline requires inline-only emission semantics");
            return false;
        }
        return true;
"""
text, count = pattern.subn(replacement, text, count=1)
assert count == 1

needle = "                                          bool is_inline,\n                                          char *section_name,"
assert text.count(needle) == 1
text = text.replace(
    needle,
    "                                          bool is_inline,\n                                          bool is_extern,\n                                          char *section_name,",
    1,
)

needle = "    context.is_inline = is_inline;\n    context.section_name = NULL;"
assert text.count(needle) == 1
text = text.replace(
    needle,
    "    context.is_inline = is_inline;\n    context.is_extern = false;\n    context.section_name = NULL;",
    1,
)

needle = "    context.is_inline = is_inline;\n    context.section_name = section_name;"
assert text.count(needle) == 1
text = text.replace(
    needle,
    "    context.is_inline = is_inline;\n    context.is_extern = is_extern;\n    context.section_name = section_name;",
    1,
)

pattern = re.compile(r"(?m)^(?P<indent>\s*)is_inline,\n(?P=indent)section_name,$")
matches = list(pattern.finditer(text))
assert len(matches) == 2
text = pattern.sub(
    lambda match: (
        f"{match.group('indent')}is_inline,\n"
        f"{match.group('indent')}is_extern_declaration,\n"
        f"{match.group('indent')}section_name,"
    ),
    text,
)
parser_path.write_text(text)

fixture_path = Path("tests/compiler/c0/gnu_prefix_function_attributes.c")
fixture = fixture_path.read_text()
anchor = """const int *call_prefix_attribute_identity(const int *value)
{
    return prefix_attribute_identity(value);
}
"""
addition = """const int *call_prefix_attribute_identity(const int *value)
{
    return prefix_attribute_identity(value);
}

inline __attribute__((__gnu_inline__)) __attribute__((__unused__))
    __attribute__((__no_instrument_function__)) int prefix_external_identity(int value)
{
    return value;
}
"""
assert fixture.count(anchor) == 1
fixture_path.write_text(fixture.replace(anchor, addition, 1))

Path("tests/compiler/c0/gnu_extern_inline_attribute.c").write_text(
    """extern inline __attribute__((__gnu_inline__)) int prefix_extern_inline_identity(int value)
{
    return value;
}
"""
)

runner_path = Path("tests/compiler/c0/run-gnu-prefix-function-attributes.sh")
runner = runner_path.read_text()
needle = """grep -F 'prefix_attribute_identity:' "$assembly" >/dev/null
grep -F 'call_prefix_attribute_identity:' "$assembly" >/dev/null
grep -F 'externally_visible_decl:' "$assembly" >/dev/null
"""
replacement = """grep -F 'prefix_attribute_identity:' "$assembly" >/dev/null
grep -F 'call_prefix_attribute_identity:' "$assembly" >/dev/null
grep -F 'prefix_external_identity:' "$assembly" >/dev/null
grep -F '.globl prefix_external_identity' "$assembly" >/dev/null
grep -F 'externally_visible_decl:' "$assembly" >/dev/null
"""
assert runner.count(needle) == 1
runner = runner.replace(needle, replacement, 1)

needle = """if grep -F '.hidden externally_visible_decl' "$assembly" >/dev/null; then
    printf '%s\\n' 'FAIL compiler/c0/gnu_prefix_function_attributes: externally_visible was mapped to ELF hidden visibility' >&2
    exit 1
fi

printf '%s\\n' 'PASS compiler/c0/gnu_prefix_function_attributes prefix=1 metadata=unused,no-instrument,externally-visible function+object=1 reachability=parse-only public-linkage=preserved gnu-inline=static-only no-sanitize-address=parse-only no-stack-protector=parse-only'
"""
replacement = """if grep -F '.hidden externally_visible_decl' "$assembly" >/dev/null; then
    printf '%s\\n' 'FAIL compiler/c0/gnu_prefix_function_attributes: externally_visible was mapped to ELF hidden visibility' >&2
    exit 1
fi

"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/gnu_extern_inline_attribute.c" \\
    -o "$work/gnu_extern_inline_attribute.i"
if "$minic" -S "$work/gnu_extern_inline_attribute.i" \\
    -o "$work/gnu_extern_inline_attribute.s" \\
    >"$work/gnu_extern_inline_attribute.out" 2>"$work/gnu_extern_inline_attribute.err"; then
    printf '%s\\n' 'FAIL compiler/c0/gnu_prefix_function_attributes: extern inline gnu_inline unexpectedly emitted a standalone definition' >&2
    exit 1
fi
grep -F 'GNU extern inline gnu_inline requires inline-only emission semantics' \\
    "$work/gnu_extern_inline_attribute.err" >/dev/null

printf '%s\\n' 'PASS compiler/c0/gnu_prefix_function_attributes prefix=1 metadata=unused,no-instrument,externally-visible function+object=1 reachability=parse-only public-linkage=preserved gnu-inline=static+bare extern-inline=fail-closed no-sanitize-address=parse-only no-stack-protector=parse-only'
"""
assert runner.count(needle) == 1
runner_path.write_text(runner.replace(needle, replacement, 1))
