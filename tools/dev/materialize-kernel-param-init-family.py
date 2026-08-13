#!/usr/bin/env python3
from pathlib import Path

parser_path = Path("src/frontend/parser_global.c")
text = parser_path.read_text()
marker = "static_object_address_relocation_target(program, expression_id, &target->object_id)"
if marker not in text:
    old = """    if (program == NULL || target == NULL) {
        return false;
    }
    expression = minic_c0_program_expression(program, expression_id);
"""
    new = """    if (program == NULL || target == NULL) {
        return false;
    }
    if (static_object_address_relocation_target(program, expression_id, &target->object_id)) {
        target->member_depth = 0U;
        return true;
    }
    expression = minic_c0_program_expression(program, expression_id);
"""
    if text.count(old) != 1:
        raise SystemExit("unexpected recursive relocation path anchor")
    text = text.replace(old, new, 1)
    parser_path.write_text(text)

program_path = Path("tests/programs/c0/static_record_compound_literal.c")
program = program_path.read_text()
if "typedef struct NameHolder" not in program:
    old = """typedef struct Outer {
    int tag;
    Inner inner;
} Outer;


static const char *const relocation_names[] = { \"backing\", \"literal\" };
"""
    new = """typedef struct Outer {
    int tag;
    Inner inner;
} Outer;

typedef struct NameHolder {
    const char *name;
} NameHolder;

static const char backing_name[] = \"backing\";
static NameHolder name_holder = { backing_name };

static const char *const relocation_names[] = { \"backing\", \"literal\" };
"""
    if program.count(old) != 1:
        raise SystemExit("unexpected static aggregate fixture anchor")
    program = program.replace(old, new, 1)
    old_main = """                   value.inner.link.prev == &value.inner.link &&
                   relocation_names[0][0] == 'b' && relocation_names[1][0] == 'l'
"""
    new_main = """                   value.inner.link.prev == &value.inner.link && name_holder.name[0] == 'b' &&
                   relocation_names[0][0] == 'b' && relocation_names[1][0] == 'l'
"""
    if program.count(old_main) != 1:
        raise SystemExit("unexpected static aggregate main anchor")
    program = program.replace(old_main, new_main, 1)
    program_path.write_text(program)
