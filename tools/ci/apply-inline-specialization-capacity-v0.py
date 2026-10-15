#!/usr/bin/env python3
from pathlib import Path

p = Path("src/compiler/compiler.c")
text = p.read_text()

old = "#define MINIC_INLINE_INTEGER_SPECIALIZATION_LIMIT 256U\n"
new = "#define MINIC_INLINE_INTEGER_SPECIALIZATION_LIMIT 2048U\n"
old_count = text.count(old)
new_count = text.count(new)
# The tested limit now lives in checked-in compiler.c.  The old
# performance/runtime profile remains callable on both source states.
if old_count == 1 and new_count == 0:
    text = text.replace(old, new, 1)
elif old_count == 0 and new_count == 1:
    print("MINIC_INLINE_SPECIALIZATION_CAPACITY_LIMIT=ALREADY_CANONICAL")
else:
    raise SystemExit(f"partial/duplicate specialization limit old={old_count} new={new_count}")

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
old_count = text.count(old)
new_count = text.count(new)
# Preserve the exact native guard/diagnostics without injecting them twice.
if old_count == 1 and new_count == 0:
    text = text.replace(old, new, 1)
elif old_count == 0 and new_count == 1:
    print("MINIC_INLINE_SPECIALIZATION_CAPACITY_INITIAL=ALREADY_CANONICAL")
else:
    raise SystemExit(f"partial/duplicate initial capacity guard old={old_count} new={new_count}")

old = '''                if (refined_id == MINIC_FUNCTION_INVALID) {
                    if (specialization_count >= 1024U ||
                        !minic_add_inline_integer_specialization(
                            program,
                            nested_source_id,
                            nested_known,
                            nested_bits,
                            specialization_count,
                            &refined_id)) {
                        return false;
                    }
                    specialization_count += 1U;
                    refinement_count += 1U;
                }
'''
new = '''                if (refined_id == MINIC_FUNCTION_INVALID) {
                    if (specialization_count >= MINIC_INLINE_INTEGER_SPECIALIZATION_LIMIT) {
                        (void)fprintf(stderr,
                                      "MINIC_INLINE_INTEGER_SPECIALIZATION_TRANSITIVE_LIMIT_HIT count=%zu limit=%u functions=%zu source=%zu\\n",
                                      specialization_count,
                                      (unsigned int)MINIC_INLINE_INTEGER_SPECIALIZATION_LIMIT,
                                      program->function_count,
                                      (size_t)nested_source_id);
                        return false;
                    }
                    if (!minic_add_inline_integer_specialization(
                            program,
                            nested_source_id,
                            nested_known,
                            nested_bits,
                            specialization_count,
                            &refined_id)) {
                        (void)fprintf(stderr,
                                      "MINIC_INLINE_INTEGER_SPECIALIZATION_TRANSITIVE_ADD_FAILED count=%zu functions=%zu source=%zu\\n",
                                      specialization_count,
                                      program->function_count,
                                      (size_t)nested_source_id);
                        return false;
                    }
                    specialization_count += 1U;
                    refinement_count += 1U;
                }
'''
count = text.count(old)
if count != 1:
    raise SystemExit(f"expected one transitive specialization capacity guard, found {count}")
text = text.replace(old, new, 1)

# The Runtime capacity contract spans source-owned initial specialization and
# dynamically retained transitive specialization. Audit the complete profile,
# not merely each patch anchor: a partial promotion must never be accepted.
required_capacity_markers = (
    "#define MINIC_INLINE_INTEGER_SPECIALIZATION_LIMIT 2048U",
    "MINIC_INLINE_INTEGER_SPECIALIZATION_LIMIT_HIT count=",
    "MINIC_INLINE_INTEGER_SPECIALIZATION_ADD_FAILED count=",
    "MINIC_INLINE_INTEGER_SPECIALIZATION_TRANSITIVE_LIMIT_HIT count=",
    "MINIC_INLINE_INTEGER_SPECIALIZATION_TRANSITIVE_ADD_FAILED count=",
)
for marker in required_capacity_markers:
    if text.count(marker) != 1:
        raise SystemExit("MINIC_INLINE_SPECIALIZATION_CAPACITY_V0=ERROR incomplete native/overlay contract: " + marker)

p.write_text(text)
print("MINIC_INLINE_SPECIALIZATION_CAPACITY_V0=APPLIED limit=2048 native_initial=1 overlay_transitive=1")
