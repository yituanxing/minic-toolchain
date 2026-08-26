#!/usr/bin/env python3
"""Sever the legacy AST -> RV64 function-body route for M175.

This patch deliberately does not touch tests.  It converts production RV64
function-body emission to Core-only, removes the public legacy writer entry,
and removes the legacy function dispatcher.  Qualification must then run on
the unchanged regression contracts so any real semantic/ABI/CFG gap is
exposed instead of normalized away in tests.
"""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
COMPILER = ROOT / "src/compiler/compiler.c"
CODEGEN_H = ROOT / "src/target/riscv64/codegen.h"
CODEGEN_FUNCTION = ROOT / "src/target/riscv64/codegen_function.c"
MARKER = "M175_LEGACY_FUNCTION_ROUTE_REMOVED"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(message)


def remove_span(text: str, start_marker: str, end_marker: str, label: str) -> str:
    start = text.find(start_marker)
    require(start >= 0, f"{label}: start marker not found")
    end = text.find(end_marker, start)
    require(end >= 0, f"{label}: end marker not found")
    return text[:start] + text[end:]


def patch_compiler() -> None:
    text = COMPILER.read_text()
    if MARKER in text:
        return

    text = remove_span(
        text,
        "typedef enum MinicCoreCodegenMode {\n",
        "typedef struct MinicCoreCandidates {\n",
        "compiler Core-codegen enum",
    )
    text = remove_span(
        text,
        "static bool minic_core_codegen_mode(",
        "static bool minic_validate_core_shadow(",
        "compiler Core-codegen selector",
    )

    old_decl = "    MinicCoreCodegenMode core_codegen_mode;\n"
    require(text.count(old_decl) == 1, "compiler Core-codegen local declaration drifted")
    text = text.replace(old_decl, "", 1)

    old_mode = '''    if (!minic_core_codegen_mode(input_path, diagnostic, &core_codegen_mode)) {\n        return 1;\n    }\n    core_validation_mode = core_shadow_mode;\n    if (core_codegen_mode != MINIC_CORE_CODEGEN_DISABLED) {\n        /* Core code generation is fail-closed: no defined function may\n           fall back to the legacy AST body emitter. */\n        core_validation_mode = MINIC_CORE_SHADOW_STRICT;\n    }\n'''
    new_mode = f'''    /* {MARKER}: production RV64 function bodies are Core-only. */\n    core_validation_mode = MINIC_CORE_SHADOW_STRICT;\n'''
    require(text.count(old_mode) == 1, "compiler Core validation selector drifted")
    text = text.replace(old_mode, new_mode, 1)

    old_emit = '''    if (success && core_codegen_mode == MINIC_CORE_CODEGEN_BASIC_V0) {\n        success =\n            minic_riscv64_write_c0_program_with_core_candidates(output_path,\n                                                                &program,\n                                                                core_candidates.functions,\n                                                                core_candidates.core_required,\n                                                                core_candidates.function_count,\n                                                                diagnostic);\n    } else if (success) {\n        success = minic_riscv64_write_c0_program(output_path, &program, diagnostic);\n    }\n'''
    new_emit = '''    if (success) {\n        success =\n            minic_riscv64_write_c0_program_with_core_candidates(output_path,\n                                                                &program,\n                                                                core_candidates.functions,\n                                                                core_candidates.core_required,\n                                                                core_candidates.function_count,\n                                                                diagnostic);\n    }\n'''
    require(text.count(old_emit) == 1, "compiler legacy writer selection drifted")
    text = text.replace(old_emit, new_emit, 1)

    require("MinicCoreCodegenMode" not in text, "compiler still owns a Core-codegen mode")
    require("minic_core_codegen_mode" not in text, "compiler still has a Core-codegen selector")
    require("minic_riscv64_write_c0_program(" not in text, "compiler still calls legacy writer")
    COMPILER.write_text(text)


def patch_codegen_header() -> None:
    text = CODEGEN_H.read_text()
    legacy_decl = '''bool minic_riscv64_write_c0_program(const char *path,\n                                    const MinicC0Program *program,\n                                    MinicDiagnostic *diagnostic);\n\n'''
    if legacy_decl in text:
        require(text.count(legacy_decl) == 1, "legacy writer declaration duplicated")
        text = text.replace(legacy_decl, "", 1)
    require("minic_riscv64_write_c0_program(" not in text, "legacy writer remains public")
    CODEGEN_H.write_text(text)


def patch_codegen_function() -> None:
    text = CODEGEN_FUNCTION.read_text()

    if "static bool minic_riscv64_emit_function(" in text:
        start = text.find("static bool minic_riscv64_emit_function(")
        end = text.find("bool minic_riscv64_write_c0_program_with_core_candidates(", start)
        require(end >= 0, "legacy function emitter end marker not found")
        text = text[:start] + text[end:]

    argument_regs = '''static const char *const minic_riscv64_argument_registers[8] = {\n    "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7"};\n\n'''
    if argument_regs in text:
        require(text.count(argument_regs) == 1, "legacy argument register table duplicated")
        text = text.replace(argument_regs, "", 1)

    fallback_start_marker = '''        } else {\n            const char *failure_stage;\n'''
    fallback_start = text.find(fallback_start_marker)
    if fallback_start >= 0:
        fallback_end_marker = "        if (!success && diagnostic != NULL && diagnostic->message[0] == '\\0') {\n"
        fallback_end = text.find(fallback_end_marker, fallback_start)
        require(fallback_end >= 0, "legacy fallback end marker not found")
        replacement = '''        } else {\n            char message[256];\n            const char *symbol_name;\n\n            symbol_name = minic_c0_function_symbol_name(function);\n            (void)snprintf(message,\n                           sizeof(message),\n                           "defined RISC-V function '%s' is not Core-owned after legacy route removal",\n                           symbol_name != NULL ? symbol_name : "<unnamed>");\n            minic_riscv64_set_diagnostic(diagnostic, path, message);\n            success = false;\n        }\n'''
        text = text[:fallback_start] + replacement + text[fallback_end:]

    legacy_writer = text.find("\nbool minic_riscv64_write_c0_program(")
    if legacy_writer >= 0:
        text = text[:legacy_writer].rstrip() + "\n"

    require("minic_riscv64_emit_function(" not in text, "legacy AST function dispatcher remains")
    require("minic_riscv64_write_c0_program(" not in text, "legacy program writer remains")
    require("const char *failure_stage;" not in text, "legacy function fallback remains")
    CODEGEN_FUNCTION.write_text(text)


def main() -> None:
    patch_compiler()
    patch_codegen_header()
    patch_codegen_function()

    # The migration tool must never rewrite test expectations.
    require(MARKER in COMPILER.read_text(), "Core-only marker missing after patch")
    print("M175_CORE_ONLY_CUT production-selector=removed legacy-writer=removed legacy-function-dispatch=removed tests=untouched")


if __name__ == "__main__":
    main()
