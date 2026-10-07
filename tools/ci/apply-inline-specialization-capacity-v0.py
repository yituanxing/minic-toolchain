#!/usr/bin/env python3
from pathlib import Path

p = Path("src/compiler/compiler.c")
text = p.read_text()

old = "#define MINIC_INLINE_INTEGER_SPECIALIZATION_LIMIT 256U\n"
new = "#define MINIC_INLINE_INTEGER_SPECIALIZATION_LIMIT 2048U\n"
count = text.count(old)
if count != 1:
    raise SystemExit(f"expected one specialization limit, found {count}")
text = text.replace(old, new, 1)

old = '''        if (specialized_id == MINIC_FUNCTION_INVALID) {
            if (specialization_count >= MINIC_INLINE_INTEGER_SPECIALIZATION_LIMIT ||
                !minic_add_inline_integer_specialization(program, source_id, known, bits,
                                                         specialization_count,
                                                         &specialized_id)) {
                return false;
            }
            specialization_count += 1U;
        }
'''
new = '''        if (specialized_id == MINIC_FUNCTION_INVALID) {
            if (specialization_count >= MINIC_INLINE_INTEGER_SPECIALIZATION_LIMIT) {
                (void)fprintf(stderr,
                              "MINIC_INLINE_INTEGER_SPECIALIZATION_LIMIT_HIT count=%zu limit=%u functions=%zu source=%zu\\n",
                              specialization_count,
                              (unsigned int)MINIC_INLINE_INTEGER_SPECIALIZATION_LIMIT,
                              program->function_count,
                              (size_t)source_id);
                return false;
            }
            if (!minic_add_inline_integer_specialization(program, source_id, known, bits,
                                                         specialization_count,
                                                         &specialized_id)) {
                (void)fprintf(stderr,
                              "MINIC_INLINE_INTEGER_SPECIALIZATION_ADD_FAILED count=%zu functions=%zu source=%zu\\n",
                              specialization_count,
                              program->function_count,
                              (size_t)source_id);
                return false;
            }
            specialization_count += 1U;
        }
'''
count = text.count(old)
if count != 1:
    raise SystemExit(f"expected one initial specialization capacity guard, found {count}")
text = text.replace(old, new, 1)

old = "specialization_count >= 1024U ||\n"
new = "specialization_count >= MINIC_INLINE_INTEGER_SPECIALIZATION_LIMIT ||\n"
count = text.count(old)
if count == 1:
    text = text.replace(old, new, 1)
elif (
    "refined_id == MINIC_FUNCTION_INVALID" in text
    and "specialization_count >= MINIC_INLINE_INTEGER_SPECIALIZATION_LIMIT ||" in text
):
    # The indexed transitive pass already uses the shared capacity macro.
    # Keep that structurally stable form instead of requiring an exact legacy block.
    pass
else:
    raise SystemExit(f"expected one transitive specialization capacity guard, found {count}")

p.write_text(text)
print("MINIC_INLINE_SPECIALIZATION_CAPACITY_V0=APPLIED limit=2048")
