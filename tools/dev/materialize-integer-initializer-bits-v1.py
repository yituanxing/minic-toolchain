from pathlib import Path

root = Path(__file__).resolve().parents[2]

# Retire the legacy typed-bits -> int adapter. Typed static-storage consumers
# now persist the canonical uint64_t payload directly.
core = root / "src/frontend/parser_core.c"
text = core.read_text()
start_marker = '''bool minic_parser_parse_integer_initializer_value(MinicParser *parser,
                                                  MinicType target_type,
                                                  int *value) {
'''
end_marker = '''bool minic_parser_parse_fixed_array_bound(MinicParser *parser, size_t *element_count) {
'''
start = text.find(start_marker)
end = text.find(end_marker, start)
if start < 0 or end < 0:
    raise SystemExit("legacy integer initializer adapter shape changed")
chunk = text[start:end]
if "integer initializer exceeds current global payload range" not in chunk:
    raise SystemExit("legacy payload range diagnostic moved")
core.write_text(text[:start] + text[end:])

header = root / "src/frontend/parser_internal.h"
text = header.read_text()
decl = '''bool minic_parser_parse_integer_initializer_value(MinicParser *parser,
                                                  MinicType target_type,
                                                  int *value);
'''
if text.count(decl) != 1:
    raise SystemExit("legacy integer initializer declaration shape changed")
header.write_text(text.replace(decl, ""))

global_parser = root / "src/frontend/parser_global.c"
text = global_parser.read_text()

# Static scalar integer values: keep typed bits all the way to AST storage.
old = '''        int value;

        if (parser->current.kind == MINIC_TOKEN_LBRACE) {
            minic_parser_error(parser, "expected integer constant expression");
            return false;
        }
        if (!minic_parser_parse_integer_initializer_value(parser, type, &value) ||
            !minic_c0_global_object_add_initializer(parser->program, object_id, value)) {
'''
new = '''        uint64_t bits;

        if (parser->current.kind == MINIC_TOKEN_LBRACE) {
            minic_parser_error(parser, "expected integer constant expression");
            return false;
        }
        if (!minic_parser_parse_integer_initializer_bits(parser, type, &bits) ||
            !minic_c0_global_object_add_initializer_bits(parser->program, object_id, bits)) {
'''
if text.count(old) != 1:
    raise SystemExit("static scalar integer initializer consumer shape changed")
text = text.replace(old, new)

old = '''static bool ensure_static_record_base_value(MinicParser *parser,
                                            MinicGlobalObjectId object_id,
                                            size_t field_index,
                                            int value) {
'''
new = '''static bool ensure_static_record_base_value(MinicParser *parser,
                                            MinicGlobalObjectId object_id,
                                            size_t field_index,
                                            uint64_t bits) {
'''
if text.count(old) != 1:
    raise SystemExit("static record base-value helper signature changed")
text = text.replace(old, new)
old = '''    return object->initializer_count == field_index &&
           minic_c0_global_object_add_initializer(parser->program, object_id, value);
'''
new = '''    return object->initializer_count == field_index &&
           minic_c0_global_object_add_initializer_bits(parser->program, object_id, bits);
'''
if text.count(old) != 1:
    raise SystemExit("static record base-value payload write changed")
text = text.replace(old, new)

old = '''    if (minic_type_is_integer(field->type)) {
        int value;

        if (!minic_parser_parse_integer_initializer_value(parser, field->type, &value)) {
            return false;
        }
        if (value == 0 && parser->program->global_objects[object_id].initializer_count == 0U) {
            return true;
        }
        return ensure_static_record_base_value(parser, object_id, field_index, value);
    }
'''
new = '''    if (minic_type_is_integer(field->type)) {
        uint64_t bits;

        if (!minic_parser_parse_integer_initializer_bits(parser, field->type, &bits)) {
            return false;
        }
        if (bits == 0U && parser->program->global_objects[object_id].initializer_count == 0U) {
            return true;
        }
        return ensure_static_record_base_value(parser, object_id, field_index, bits);
    }
'''
if text.count(old) != 1:
    raise SystemExit("static record integer-field consumer shape changed")
text = text.replace(old, new)
if "minic_parser_parse_integer_initializer_value" in text:
    raise SystemExit("legacy integer initializer consumer remains in parser_global.c")
global_parser.write_text(text)

function_parser = root / "src/frontend/parser_function.c"
text = function_parser.read_text()
old = '''    if (minic_type_is_integer(object_type)) {
        int value;

        if (!minic_parser_parse_integer_initializer_value(parser, object_type, &value) ||
            !minic_c0_global_object_add_initializer(parser->program, object_id, value)) {
'''
new = '''    if (minic_type_is_integer(object_type)) {
        uint64_t bits;

        if (!minic_parser_parse_integer_initializer_bits(parser, object_type, &bits) ||
            !minic_c0_global_object_add_initializer_bits(parser->program, object_id, bits)) {
'''
if text.count(old) != 1:
    raise SystemExit("external integer definition consumer shape changed")
text = text.replace(old, new)
if "minic_parser_parse_integer_initializer_value" in text:
    raise SystemExit("legacy integer initializer consumer remains in parser_function.c")
function_parser.write_text(text)

fixture = root / "tests/compiler/c0/external_scalar_definition.c"
text = fixture.read_text()
old = '''long long external_wide = 11LL;
unsigned long loops_per_jiffy = (1 << 12);
static int internal_folded = (3 + 5) * 2;
'''
new = '''long long external_wide = 11LL;
unsigned long loops_per_jiffy = (1 << 12);
unsigned long external_payload_wide = (1UL << 40);
static const unsigned long long internal_runtime_limit = ((1ULL << (64 - 20)) - 1) * 1000L;
struct WidePayloadRecord {
    unsigned long payload;
};
static const struct WidePayloadRecord internal_wide_record = {
    .payload = (1UL << 40),
};
static int internal_folded = (3 + 5) * 2;
'''
if text.count(old) != 1:
    raise SystemExit("external scalar fixture shape changed")
text = text.replace(old, new)
old = '''    return external_count == 7 && external_wide == 11LL && loops_per_jiffy == 4096UL &&
                   internal_folded == 16
'''
new = '''    return external_count == 7 && external_wide == 11LL && loops_per_jiffy == 4096UL &&
                   external_payload_wide == (1UL << 40) &&
                   internal_runtime_limit == 17592186044415000ULL &&
                   internal_wide_record.payload == (1UL << 40) && internal_folded == 16
'''
if text.count(old) != 1:
    raise SystemExit("external scalar main shape changed")
fixture.write_text(text.replace(old, new))

runner = root / "tests/compiler/c0/run-external-scalar-definitions.sh"
text = runner.read_text()
anchor = '''grep -F '  .dword 4096' "$work/external_scalar_definition.s" >/dev/null
'''
insert = anchor + '''grep -F '.globl external_payload_wide' "$work/external_scalar_definition.s" >/dev/null
grep -F '  .dword 1099511627776' "$work/external_scalar_definition.s" >/dev/null
grep -F 'internal_runtime_limit:' "$work/external_scalar_definition.s" >/dev/null
grep -F '  .dword 17592186044415000' "$work/external_scalar_definition.s" >/dev/null
grep -F 'internal_wide_record:' "$work/external_scalar_definition.s" >/dev/null
test "$(grep -c '  .dword 1099511627776' "$work/external_scalar_definition.s")" -ge 2
'''
if text.count(anchor) != 1:
    raise SystemExit("external scalar runner payload anchor changed")
text = text.replace(anchor, insert)
old = '''expect_failure invalid_external_integer_payload_range \\
    'integer initializer exceeds current global payload range'

printf '%s\\n' \\
    'PASS compiler/c0/external_scalar_definition extern-merge=1 typed-consteval=1 int=.word long=.dword static=shared payload=int-bounded'
'''
new = '''printf '%s\\n' \\
    'PASS compiler/c0/external_scalar_definition extern-merge=1 typed-consteval=1 int=.word long=.dword static=shared payload=typed-bits wide=external+static+record'
'''
if text.count(old) != 1:
    raise SystemExit("external scalar legacy payload contract changed")
runner.write_text(text.replace(old, new))

legacy_negative = root / "tests/compiler/c0/invalid_external_integer_payload_range.c"
if not legacy_negative.exists():
    raise SystemExit("legacy payload-range negative fixture is missing")
legacy_negative.unlink()
