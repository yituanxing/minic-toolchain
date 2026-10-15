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
old_count = text.count(old)
new_count = text.count(new)
if old_count == 0 and new_count == 1:
    print("MINIC_INLINE_SPECIALIZATION_SYMBOLIC_TRANSITIVE_REWRITE_V0=ALREADY_CANONICAL")
    raise SystemExit(0)
if old_count != 1 or new_count != 0:
    raise SystemExit(f"symbolic transitive rewrite is partial/duplicated old={old_count} new={new_count}")
p.write_text(text.replace(old, new, 1))
print("MINIC_INLINE_SPECIALIZATION_SYMBOLIC_TRANSITIVE_REWRITE_V0=APPLIED")
