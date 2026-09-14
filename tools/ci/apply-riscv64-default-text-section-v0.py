#!/usr/bin/env python3
from pathlib import Path
import runpy


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one anchor, found {count}: {old[:160]!r}")
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
# Batch wiring: exercise the validated generic Core CFG re-entry fix in the
# existing 49-object pool without adding another workflow-only patch stage.
runpy.run_path("tools/ci/apply-core-unreachable-reentry-v0.py", run_name="__main__")
