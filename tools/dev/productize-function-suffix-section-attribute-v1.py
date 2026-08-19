#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count == 0 and new in text:
        return
    if count != 1:
        raise SystemExit(f"{label}: expected one replacement, found {count}")
    target.write_text(text.replace(old, new, 1))


replace_once(
    "src/frontend/parser_function.c",
    """static bool parse_persistent_function_attributes(MinicParser *parser,
                                                 bool is_internal,
                                                 bool *is_weak,
                                                 MinicFunctionId *alias_target) {
    MinicFunctionAttributeContext context;

    context.allow_gnu_inline = false;
    context.is_internal = is_internal;
    context.is_inline = false;
    context.is_extern = false;
    context.section_name = NULL;
    context.section_capacity = 0U;
    context.section_name_length = NULL;
    context.has_section = NULL;
    context.is_weak = is_weak;
""",
    """static bool parse_persistent_function_attributes(MinicParser *parser,
                                                 bool is_internal,
                                                 char *section_name,
                                                 size_t section_capacity,
                                                 size_t *section_name_length,
                                                 bool *has_section,
                                                 bool *is_weak,
                                                 MinicFunctionId *alias_target) {
    MinicFunctionAttributeContext context;

    context.allow_gnu_inline = false;
    context.is_internal = is_internal;
    context.is_inline = false;
    context.is_extern = false;
    context.section_name = section_name;
    context.section_capacity = section_capacity;
    context.section_name_length = section_name_length;
    context.has_section = has_section;
    context.is_weak = is_weak;
""",
    "suffix-attribute-context",
)

replace_once(
    "src/frontend/parser_function.c",
    """                !parse_persistent_function_attributes(parser, is_internal, &entity_is_weak, NULL)) {
""",
    """                !parse_persistent_function_attributes(parser,
                                                     is_internal,
                                                     NULL,
                                                     0U,
                                                     NULL,
                                                     NULL,
                                                     &entity_is_weak,
                                                     NULL)) {
""",
    "typed-function-suffix-call",
)

replace_once(
    "src/frontend/parser_function.c",
    """        !parse_persistent_function_attributes(parser, is_internal, &is_weak, &alias_target)) {
""",
    """        !parse_persistent_function_attributes(parser,
                                             is_internal,
                                             section_name,
                                             sizeof(section_name),
                                             &section_name_length,
                                             &has_section,
                                             &is_weak,
                                             &alias_target)) {
""",
    "direct-function-suffix-call",
)

path = Path("tests/compiler/c0/run-gnu-function-attributes.sh")
text = path.read_text()
marker = "suffix-section=program-owned"
if marker not in text:
    anchor = """printf '%s\\n' 'PASS compiler/c0/gnu_function_attributes metadata=nothrow,leaf,nonnull,access,pure,malloc,alloc-size,assume-aligned,noreturn,deprecated,const-keyword arguments=registry-validated placement=pre-declarator,suffix optimization-metadata=parse-only unknown=reject aligned=not-silently-ignored'\n"""
    if text.count(anchor) != 1:
        raise SystemExit("function suffix section test anchor changed")
    block = r'''cat >"$work/function-suffix-section.c" <<'EOF'
extern void init_decl(void) __attribute__((__section__(".init.text"))) __attribute__((__cold__));
void init_body(void) __attribute__((cold)) __attribute__((section(".init.text")));
void init_body(void) {}
int suffix_section_probe(void) {
    init_decl();
    init_body();
    return 0;
}
EOF
"$host_cc" -E -P -x c "$work/function-suffix-section.c" \
    -o "$work/function-suffix-section.i"
"$minic" -S "$work/function-suffix-section.i" \
    -o "$work/function-suffix-section.s"
grep -F '.section .init.text' "$work/function-suffix-section.s" >/dev/null
grep -F 'init_body:' "$work/function-suffix-section.s" >/dev/null
grep -F '  call init_decl' "$work/function-suffix-section.s" >/dev/null

'''
    replacement = block + "printf '%s\\n' 'PASS compiler/c0/gnu_function_attributes metadata=nothrow,leaf,nonnull,access,pure,malloc,alloc-size,assume-aligned,noreturn,deprecated,const-keyword arguments=registry-validated placement=pre-declarator,suffix optimization-metadata=parse-only suffix-section=program-owned unknown=reject aligned=not-silently-ignored'\n"
    path.write_text(text.replace(anchor, replacement, 1))
