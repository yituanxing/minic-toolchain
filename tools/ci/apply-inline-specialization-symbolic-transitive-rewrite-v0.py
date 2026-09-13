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
count = text.count(old)
if count != 1:
    raise SystemExit(f"expected one symbolic transitive tail, found {count}")
p.write_text(text.replace(old, new, 1))
print("MINIC_INLINE_SPECIALIZATION_SYMBOLIC_TRANSITIVE_REWRITE_V0=APPLIED")
