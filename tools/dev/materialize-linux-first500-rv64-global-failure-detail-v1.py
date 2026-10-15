#!/usr/bin/env python3
"""Materialize richer RV64 global-emission diagnostics for semantic frontier triage."""
from pathlib import Path

path = Path("src/target/riscv64/codegen_function.c")
text = path.read_text()
old = '''            (void)snprintf(message,
                           sizeof(message),
                           "cannot emit RISC-V global object '%s' (index=%zu)",
                           object->name,
                           global_index);
'''
new = '''            {
                const MinicGlobalRelocation *first_relocation;

                first_relocation = object->relocation_count != 0U ? &object->relocations[0] : NULL;
                (void)snprintf(message,
                               sizeof(message),
                               "cannot emit RISC-V global object '%s' (index=%zu init=%zu reloc=%zu "
                               "union=%zu first-kind=%u first-slot=%zu)",
                               object->name,
                               global_index,
                               object->initializer_count,
                               object->relocation_count,
                               object->union_selection_count,
                               first_relocation != NULL
                                   ? (unsigned int)first_relocation->location_kind
                                   : 999U,
                               first_relocation != NULL ? first_relocation->location_index : 0U);
            }
'''
if new not in text:
    if text.count(old) != 1:
        raise SystemExit("global-emission diagnostic anchor not found uniquely")
    text = text.replace(old, new, 1)
path.write_text(text)
