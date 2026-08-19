from pathlib import Path

path = Path(__file__).resolve().parents[2] / "src/frontend/parser_global.c"
text = path.read_text()

recursive = '''static void static_pointer_initializer_reset(MinicStaticPointerInitializer *initializer) {
    if (initializer == NULL) {
        return;
    }
    static_pointer_initializer_reset(initializer);
}
'''
fixed = '''static void static_pointer_initializer_reset(MinicStaticPointerInitializer *initializer) {
    if (initializer == NULL) {
        return;
    }
    (void)memset(initializer, 0, sizeof(*initializer));
    initializer->function_id = MINIC_FUNCTION_INVALID;
    initializer->relocation_target.object_id = MINIC_GLOBAL_OBJECT_INVALID;
}
'''
if recursive not in text:
    raise SystemExit("recursive static pointer reset materialization not found")
text = text.replace(recursive, fixed, 1)

function_start = text.index("static bool parse_static_pointer_initializer(")
function_end = text.index("\n}\n", function_start) + 3
body = text[function_start:function_end]
old_reset = '''    (void)memset(initializer, 0, sizeof(*initializer));
    initializer->function_id = MINIC_FUNCTION_INVALID;
    initializer->relocation_target.object_id = MINIC_GLOBAL_OBJECT_INVALID;
'''
if old_reset not in body:
    raise SystemExit("parse_static_pointer_initializer reset block not found")
body = body.replace(old_reset, "    static_pointer_initializer_reset(initializer);\n", 1)
text = text[:function_start] + body + text[function_end:]
path.write_text(text)
print("fixed static pointer initializer reset ownership")
