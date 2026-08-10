#!/usr/bin/env python3
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, found {count}")
    return text.replace(old, new, 1)


root = Path(__file__).resolve().parents[2]
path = root / "src/frontend/parser_global.c"
text = path.read_text()

text = replace_once(
    text,
    '''        MinicGlobalObjectId object_id;\n        MinicSourceSpan name_span;\n        MinicType object_type;\n        bool is_array;\n\n''',
    '''        MinicGlobalObjectId object_id;\n        MinicSourceSpan name_span;\n        MinicType object_type;\n        char declarator_section_name[256];\n        size_t declarator_section_name_length;\n        bool declarator_has_section;\n        bool is_array;\n\n        declarator_section_name_length = section_name_length;\n        declarator_has_section = has_section;\n        (void)memset(declarator_section_name, 0, sizeof(declarator_section_name));\n        if (has_section) {\n            if (section_name == NULL || section_name_length + 1U > sizeof(declarator_section_name)) {\n                minic_parser_error(parser, "invalid shared GNU section attribute");\n                return false;\n            }\n            (void)memcpy(declarator_section_name, section_name, section_name_length + 1U);\n        }\n\n''',
    "extern-per-declarator-section-state",
)

# GNU permits attributes immediately after the core declarator name.
text = replace_once(
    text,
    '''        if (minic_type_is_void(object_type) || minic_type_is_function(object_type) ||\n            minic_type_is_array(object_type)) {\n''',
    '''        if (!minic_parser_parse_gnu_section_attribute(parser,\n                                                      declarator_section_name,\n                                                      sizeof(declarator_section_name),\n                                                      &declarator_section_name_length,\n                                                      &declarator_has_section)) {\n            return false;\n        }\n        if (minic_type_is_void(object_type) || minic_type_is_function(object_type) ||\n            minic_type_is_array(object_type)) {\n''',
    "extern-section-after-core-declarator",
)

# Also accept attributes following an array suffix; the same conflict checker merges placements.
text = replace_once(
    text,
    '''            object_type = array_type;\n        }\n\n        if (!minic_c0_program_add_global_object(\n''',
    '''            object_type = array_type;\n            if (!minic_parser_parse_gnu_section_attribute(parser,\n                                                          declarator_section_name,\n                                                          sizeof(declarator_section_name),\n                                                          &declarator_section_name_length,\n                                                          &declarator_has_section)) {\n                return false;\n            }\n        }\n\n        if (!minic_c0_program_add_global_object(\n''',
    "extern-section-after-array-suffix",
)

text = replace_once(
    text,
    '''            (has_section && !minic_c0_global_object_set_section(\n                                parser->program, object_id, section_name, section_name_length)) ||\n''',
    '''            (declarator_has_section &&\n             !minic_c0_global_object_set_section(parser->program,\n                                                 object_id,\n                                                 declarator_section_name,\n                                                 declarator_section_name_length)) ||\n''',
    "extern-effective-section-attachment",
)
path.write_text(text)

path = root / "tests/compiler/c0/gnu_section_symbol_attribute.c"
path.write_text(r'''extern char __attribute__((__section__(".probe.data"))) placed_data[];
char placed_data[] = "x";

extern unsigned long suffix_first __attribute__((__section__(".probe.suffix.first"))),
    suffix_second __attribute__((__section__(".probe.suffix.second")));
unsigned long suffix_first = 7;
unsigned long suffix_second = 9;

extern char suffix_array[] __attribute__((__section__(".probe.suffix.array")));
char suffix_array[] = "z";

void __attribute__((__section__(".probe.text"))) placed_function(void);

void placed_function(void) {
}

int main(void) {
    placed_function();
    return placed_data[0] == 'x' && suffix_first == 7 && suffix_second == 9 &&
                   suffix_array[0] == 'z'
               ? 0
               : 1;
}
''')

path = root / "tests/compiler/c0/run-gnu-section-symbol-attribute.sh"
text = path.read_text()
text = replace_once(
    text,
    '''grep -F '.section .probe.data' "$assembly" >/dev/null\ngrep -F 'placed_data:' "$assembly" >/dev/null\n''',
    '''grep -F '.section .probe.data' "$assembly" >/dev/null\ngrep -F 'placed_data:' "$assembly" >/dev/null\ngrep -F '.section .probe.suffix.first' "$assembly" >/dev/null\ngrep -F 'suffix_first:' "$assembly" >/dev/null\ngrep -F '.section .probe.suffix.second' "$assembly" >/dev/null\ngrep -F 'suffix_second:' "$assembly" >/dev/null\ngrep -F '.section .probe.suffix.array' "$assembly" >/dev/null\ngrep -F 'suffix_array:' "$assembly" >/dev/null\n''',
    "extern-suffix-section-assembly-checks",
)
text = replace_once(
    text,
    "extern-object=preserved function-declaration=preserved definition-inherits=1 rv64-section-emission=1",
    "extern-object=prefix+suffix per-declarator=isolated array-suffix=1 function-declaration=preserved definition-inherits=1 rv64-section-emission=1",
    "extern-suffix-section-summary",
)
path.write_text(text)
