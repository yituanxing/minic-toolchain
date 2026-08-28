#!/usr/bin/env python3
# Keep the global Core callee-table verifier aligned with M85 call arguments.

from pathlib import Path

MARKER = "M85B_RECORD_CALLEE_VERIFIER"
PATH = Path("src/core/core_ir.c")


def main() -> int:
    text = PATH.read_text()
    if MARKER in text:
        print("M85b record callee verifier already applied")
        return 0

    old = '''        for (parameter_index = 0U; parameter_index < callee->parameter_count; ++parameter_index) {
            if (!core_call_scalar_type(callee->parameter_types[parameter_index])) {
                return false;
            }
        }
    }
    for (index = 0U; index < function->call_signature_count; ++index) {
'''
    new = '''        /* M85B_RECORD_CALLEE_VERIFIER: direct callees share the same
           scalar-or-record parameter contract used by creation and CALL
           instruction verification. Indirect signatures remain scalar-only. */
        for (parameter_index = 0U; parameter_index < callee->parameter_count; ++parameter_index) {
            if (!core_call_parameter_type(callee->parameter_types[parameter_index])) {
                return false;
            }
        }
    }
    for (index = 0U; index < function->call_signature_count; ++index) {
'''
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"M85b callee verifier anchor count={count}")
    PATH.write_text(text.replace(old, new, 1))
    print("M85b record callee verifier applied")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
