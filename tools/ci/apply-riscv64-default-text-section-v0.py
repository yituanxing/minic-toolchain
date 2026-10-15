#!/usr/bin/env python3
from pathlib import Path
import runpy


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    old_count = text.count(old)
    new_count = text.count(new)
    # The verified Runtime section behavior now lives in canonical C source.
    # Keep this entrypoint: it still installs the independent local-fact fix.
    if old_count == 0 and new_count == 1:
        print("MINIC_RISCV64_DEFAULT_TEXT_SECTION_V0=ALREADY_CANONICAL")
        return
    if old_count != 1 or new_count != 0:
        raise SystemExit(f"{path}: partial or duplicate section promotion: old={old_count} new={new_count}")
    p.write_text(text.replace(old, new, 1))


replace_once(
    "src/target/riscv64/function_symbol.h",
    "    } else if (fprintf(file, \".section .text.%s\\n\", symbol->symbol_name) < 0) {\n"
    "        /* Keep every ordinary function in its own input section. This makes\n"
    "           linker --gc-sections effective even when a build system's\n"
    "           -ffunction-sections flag is consumed outside minic-cc, while an\n"
    "           explicit GNU section attribute above still owns the exact section. */\n"
    "        return false;\n"
    "    }\n",
    "    } else if (fprintf(file, \".text\\n\") < 0) {\n"
    "        /* Match the normal GCC/Clang default: ordinary functions share .text.\n"
    "           Per-function .text.<name> sections are an opt-in compiler mode, not\n"
    "           the default ABI surface.  Emitting them unconditionally breaks\n"
    "           linker scripts (including Linux 6.6) that only collect .text.* when\n"
    "           -ffunction-sections/dead-code elimination is explicitly enabled.\n"
    "           Explicit GNU section attributes above still own the exact section. */\n"
    "        return false;\n"
    "    }\n",
)

print("MINIC_RISCV64_DEFAULT_TEXT_SECTION_V0=APPLIED")
runpy.run_path("tools/ci/apply-local-integer-guard-facts-v0.py", run_name="__main__")
