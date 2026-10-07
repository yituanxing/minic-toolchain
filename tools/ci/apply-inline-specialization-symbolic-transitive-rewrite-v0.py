#!/usr/bin/env python3
from pathlib import Path

p = Path("src/compiler/compiler.c")
text = p.read_text()

old_legacy = '''                    transitive_clone_count += 1U;
                }
            }
        }
    }
'''
new_legacy = '''                    transitive_clone_count += 1U;
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

old_indexed = '''                    transitive_clone_count += 1U;
                }
            }
        }
        free(symbolic_call_next);
        free(symbolic_call_heads);
    }
'''
new_indexed = '''                    transitive_clone_count += 1U;
                }
                /* The transitive symbolic pass used to create the right clone
                   but leave the source call pointing at the base callee.  That
                   made the specialization dead and preserved deferred asm
                   immediates.  Rewrite the actual nested call by stable index. */
                program->expressions[nested_expression_index].value.call.function_id = variant_id;
            }
        }
        free(symbolic_call_next);
        free(symbolic_call_heads);
    }
'''

legacy_count = text.count(old_legacy)
indexed_count = text.count(old_indexed)
if legacy_count == 1 and indexed_count == 0:
    text = text.replace(old_legacy, new_legacy, 1)
elif indexed_count == 1 and legacy_count == 0:
    text = text.replace(old_indexed, new_indexed, 1)
else:
    raise SystemExit(
        f"expected one symbolic transitive tail, legacy={legacy_count} indexed={indexed_count}"
    )

p.write_text(text)
print("MINIC_INLINE_SPECIALIZATION_SYMBOLIC_TRANSITIVE_REWRITE_V0=APPLIED")
