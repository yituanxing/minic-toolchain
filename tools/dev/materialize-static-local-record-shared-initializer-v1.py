from pathlib import Path

root = Path(__file__).resolve().parents[2]

path = root / "src/frontend/parser_statement.c"
text = path.read_text()
start_marker = '''static bool parse_static_local_record_initializer(MinicParser *parser,
                                                  MinicType declared_type,
                                                  MinicSourceSpan name_span,
                                                  MinicGlobalObjectId *out_object_id) {
'''
end_marker = '''static bool add_implicitly_zero_initialized_static_local(MinicParser *parser,
'''
start = text.find(start_marker)
end = text.find(end_marker, start)
if start < 0 or end < 0:
    raise SystemExit("static local record initializer owner shape changed")
old = text[start:end]
if "expected '{' in static record initializer" not in old or \
   "static record field requires an integer constant expression" not in old:
    raise SystemExit("static local record initializer no longer has the expected specialized semantics")
new = '''static bool parse_static_local_record_initializer(MinicParser *parser,
                                                  MinicType declared_type,
                                                  MinicSourceSpan name_span,
                                                  MinicGlobalObjectId *out_object_id) {
    char symbol_name[96];
    MinicGlobalObjectId object_id;
    int symbol_length;

    if (parser == NULL || out_object_id == NULL || !minic_type_is_record(declared_type) ||
        !minic_parser_require_complete_object_type(
            parser, declared_type, "static local record requires a complete record type")) {
        return false;
    }
    symbol_length = snprintf(symbol_name,
                             sizeof(symbol_name),
                             "__minic_static_local_%zu_%zu",
                             (size_t)parser->current_function,
                             parser->program->global_object_count);
    if (symbol_length <= 0 || (size_t)symbol_length >= sizeof(symbol_name)) {
        minic_parser_error(parser, "cannot build static local record symbol name");
        return false;
    }
    if (!minic_c0_program_add_global_object(parser->program,
                                            symbol_name,
                                            (size_t)symbol_length,
                                            declared_type,
                                            true,
                                            minic_type_is_const(declared_type),
                                            &object_id) ||
        !minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '=' after static record") ||
        !minic_parser_parse_static_storage_initializer_value(parser, object_id, declared_type) ||
        !minic_parser_bind_scoped_global_object(parser, name_span, object_id)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
            minic_parser_error(parser, "cannot initialize static local record storage");
        }
        return false;
    }
    *out_object_id = object_id;
    return true;
}

'''
path.write_text(text[:start] + new + text[end:])

fixture = root / "tests/compiler/c0/static_local_record_initializer.c"
text = fixture.read_text()
append = r'''

typedef struct MiniAtomic {
    int counter;
} MiniAtomic;

typedef struct MiniStaticKey {
    MiniAtomic enabled;
    union {
        unsigned long type;
        void *entries;
    };
} MiniStaticKey;

typedef struct MiniStaticKeyTrue {
    MiniStaticKey key;
} MiniStaticKeyTrue;

int read_static_compound_record(void) {
    static MiniStaticKeyTrue once_key = (MiniStaticKeyTrue) {
        .key = { .enabled = { 1 }, { .type = 1UL } },
    };
    return once_key.key.enabled.counter + (int)once_key.key.type;
}
'''
if "read_static_compound_record" in text:
    raise SystemExit("static compound record fixture already present")
fixture.write_text(text + append)

runner = root / "tests/compiler/c0/run-static-local-record-initializers.sh"
text = runner.read_text()
old_msg = "printf '%s\\n' 'PASS compiler/c0/static_local_record_initializer enum=7 nested-zero=12 signed=-1,-2 target-layout=rv64'\n"
new_msg = "grep -F '  .dword 1' \"$work/static_local_record_initializer.s\" >/dev/null\nprintf '%s\\n' 'PASS compiler/c0/static_local_record_initializer enum=7 nested-zero=12 signed=-1,-2 compound-literal=1 designated-nested=1 anonymous-union-first=1 shared-owner=1 target-layout=rv64'\n"
if text.count(old_msg) != 1:
    raise SystemExit("static local record runner message changed")
runner.write_text(text.replace(old_msg, new_msg))
