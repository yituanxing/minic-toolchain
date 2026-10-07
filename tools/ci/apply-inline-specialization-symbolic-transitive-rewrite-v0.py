#!/usr/bin/env python3
from pathlib import Path

p = Path("src/compiler/compiler.c")
text = p.read_text()

old = '''                    transitive_clone_count += 1U;
                }
            }
        }
    }
'''
new = '''                    transitive_clone_count += 1U;
                }
                /* The transitive symbolic pass used to create the right clone
                   but leave the source call pointing at the base callee.  That
                   made the specialization dead and preserved deferred asm
                   immediates.  Rewrite the actual nested call by stable index. */
                program->expressions[nested_expression_index].value.call.function_id = variant_id;
            }
        }
    }
'''

indexed_old = '''                    transitive_clone_count += 1U;
                }
                nested_expression_index = next_nested_expression_index;
'''
indexed_new = '''                    transitive_clone_count += 1U;
                }
                /* Keep the indexed walk semantics identical to the legacy
                   transitive pass: the discovered nested call must point at
                   the selected symbolic specialization before advancing. */
                program->expressions[nested_expression_index].value.call.function_id = variant_id;
                nested_expression_index = next_nested_expression_index;
'''

legacy_count = text.count(old)
indexed_count = text.count(indexed_old)
if legacy_count == 1 and indexed_count == 0:
    text = text.replace(old, new, 1)
elif indexed_count == 1 and legacy_count == 0:
    text = text.replace(indexed_old, indexed_new, 1)
else:
    raise SystemExit(
        f"expected one symbolic transitive tail, legacy={legacy_count} indexed={indexed_count}"
    )

p.write_text(text)
print("MINIC_INLINE_SPECIALIZATION_SYMBOLIC_TRANSITIVE_REWRITE_V0=APPLIED")
