from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if old not in text:
        raise SystemExit(f"{label} anchor not found")
    return text.replace(old, new, 1)


codegen = Path("src/target/riscv64/codegen_function.c")
text = codegen.read_text()
if '#include "target/riscv64/core_codegen.h"' not in text:
    text = replace_once(
        text,
        '#include "target/riscv64/codegen_internal.h"\n',
        '#include "target/riscv64/codegen_internal.h"\n#include "target/riscv64/core_codegen.h"\n',
        "codegen include",
    )
start = text.index("bool minic_riscv64_write_c0_program(")
tail = r'''bool minic_riscv64_write_c0_program_with_core_candidates(
    const char *path,
    const MinicC0Program *program,
    const MinicCoreFunction *core_functions,
    size_t core_function_count,
    MinicDiagnostic *diagnostic) {
    FILE *file;
    size_t global_index;
    size_t function_index;
    size_t label_counter;
    bool success;

    if (program == NULL) {
        minic_riscv64_set_diagnostic(diagnostic, path, "program is required");
        return false;
    }
    if ((core_functions == NULL && core_function_count != 0U) ||
        (core_functions != NULL && core_function_count != program->function_count)) {
        minic_riscv64_set_diagnostic(
            diagnostic, path, "Core candidate map does not match program functions");
        return false;
    }

    file = fopen(path, "wb");
    if (file == NULL) {
        char message[256];

        (void)snprintf(message, sizeof(message), "cannot open output: %s", strerror(errno));
        minic_riscv64_set_diagnostic(diagnostic, path, message);
        return false;
    }

    success = true;
    for (global_index = 0U; success && global_index < program->global_object_count;
         ++global_index) {
        if (program->global_objects[global_index].is_extern) {
            continue;
        }
        success =
            minic_riscv64_emit_global_object(file, program, &program->global_objects[global_index]);
    }
    if (success && program->file_asm_count != 0U) {
        size_t file_asm_index;

        success = fprintf(file, ".text\n") >= 0;
        for (file_asm_index = 0U; success && file_asm_index < program->file_asm_count;
             ++file_asm_index) {
            success = minic_riscv64_emit_file_asm(file, &program->file_asms[file_asm_index]);
        }
    }
    if (success) {
        success = fprintf(file, ".text\n") >= 0;
    }

    label_counter = 0U;
    for (function_index = 0U; success && function_index < program->function_count;
         ++function_index) {
        const MinicFunction *function;
        const MinicCoreFunction *core_function;

        function = &program->functions[function_index];
        if (!function->is_defined) {
            const char *symbol_name;

            symbol_name = minic_c0_function_symbol_name(function);
            if (function->is_weak && !function->is_internal) {
                success = symbol_name != NULL && symbol_name[0] != '\0' &&
                          fprintf(file, ".weak %s\n", symbol_name) >= 0;
            }
            continue;
        }
        core_function = core_functions != NULL ? &core_functions[function_index] : NULL;
        if (core_function != NULL &&
            minic_riscv64_core_function_can_emit_basic_v0(core_function)) {
            MinicRiscv64FunctionSymbol symbol;

            success = minic_riscv64_function_symbol_from_function(function, &symbol) &&
                      minic_riscv64_emit_core_function_basic_v0_with_symbol(
                          file, core_function, &symbol);
        } else {
            success = minic_riscv64_emit_function(file, program, function, &label_counter);
        }
    }

    if (!success) {
        minic_riscv64_set_diagnostic(diagnostic, path, "cannot write RISC-V assembly");
    }
    if (fclose(file) != 0 && success) {
        minic_riscv64_set_diagnostic(diagnostic, path, "cannot close RISC-V assembly output");
        success = false;
    }
    return success;
}

bool minic_riscv64_write_c0_program(const char *path,
                                    const MinicC0Program *program,
                                    MinicDiagnostic *diagnostic) {
    return minic_riscv64_write_c0_program_with_core_candidates(
        path, program, NULL, 0U, diagnostic);
}
'''
codegen.write_text(text[:start] + tail)

compiler = Path("src/compiler/compiler.c")
text = compiler.read_text()
if "typedef enum MinicCoreCodegenMode" not in text:
    marker = "} MinicCoreShadowMode;\n"
    index = text.index(marker) + len(marker)
    text = text[:index] + r'''

typedef enum MinicCoreCodegenMode {
    MINIC_CORE_CODEGEN_DISABLED = 0,
    MINIC_CORE_CODEGEN_BASIC_V0
} MinicCoreCodegenMode;
''' + text[index:]
if "static bool minic_core_codegen_mode(" not in text:
    index = text.index("static bool minic_validate_core_shadow(")
    helper = r'''static bool minic_core_codegen_mode(const char *input_path,
                                    MinicDiagnostic *diagnostic,
                                    MinicCoreCodegenMode *mode) {
    const char *value;

    if (mode == NULL) {
        return false;
    }
    value = getenv("MINIC_CORE_CODEGEN");
    if (value == NULL || value[0] == '\0') {
        *mode = MINIC_CORE_CODEGEN_DISABLED;
        return true;
    }
    if (strcmp(value, "basic-v0") == 0) {
        *mode = MINIC_CORE_CODEGEN_BASIC_V0;
        return true;
    }
    minic_set_diagnostic(diagnostic,
                         input_path,
                         1U,
                         1U,
                         "MINIC_CORE_CODEGEN must be unset or 'basic-v0'");
    return false;
}

'''
    text = text[:index] + helper + text[index:]
text = replace_once(
    text,
    "    MinicCoreCandidates core_candidates;\n    const MinicTargetInfo *target_info;\n    MinicCoreShadowMode core_shadow_mode;\n    bool success;",
    "    MinicCoreCandidates core_candidates;\n    const MinicTargetInfo *target_info;\n    MinicCoreCodegenMode core_codegen_mode;\n    MinicCoreShadowMode core_shadow_mode;\n    MinicCoreShadowMode core_validation_mode;\n    bool success;",
    "compiler declarations",
)
env_anchor = (
    "    if (!minic_core_shadow_mode(input_path, diagnostic, &core_shadow_mode)) {\n"
    "        return 1;\n"
    "    }\n"
)
env_replacement = env_anchor + (
    "    if (!minic_core_codegen_mode(input_path, diagnostic, &core_codegen_mode)) {\n"
    "        return 1;\n"
    "    }\n"
    "    core_validation_mode = core_shadow_mode;\n"
    "    if (core_codegen_mode != MINIC_CORE_CODEGEN_DISABLED &&\n"
    "        core_validation_mode == MINIC_CORE_SHADOW_DISABLED) {\n"
    "        core_validation_mode = MINIC_CORE_SHADOW_OPTIONAL;\n"
    "    }\n"
)
text = replace_once(text, env_anchor, env_replacement, "Core environment")
text = replace_once(
    text,
    "    if (success && core_shadow_mode != MINIC_CORE_SHADOW_DISABLED &&\n"
    "        !minic_prepare_core_candidates(&program, &core_candidates)) {",
    "    if (success && core_validation_mode != MINIC_CORE_SHADOW_DISABLED &&\n"
    "        !minic_prepare_core_candidates(&program, &core_candidates)) {",
    "candidate preparation",
)
text = replace_once(
    text,
    "            input_path, &program, &core_candidates, core_shadow_mode, diagnostic);",
    "            input_path, &program, &core_candidates, core_validation_mode, diagnostic);",
    "candidate validation",
)
text = replace_once(
    text,
    "    if (success) {\n"
    "        success = minic_riscv64_write_c0_program(output_path, &program, diagnostic);\n"
    "    }\n",
    "    if (success && core_codegen_mode == MINIC_CORE_CODEGEN_BASIC_V0) {\n"
    "        success = minic_riscv64_write_c0_program_with_core_candidates(\n"
    "            output_path,\n"
    "            &program,\n"
    "            core_candidates.functions,\n"
    "            core_candidates.function_count,\n"
    "            diagnostic);\n"
    "    } else if (success) {\n"
    "        success = minic_riscv64_write_c0_program(output_path, &program, diagnostic);\n"
    "    }\n",
    "RV64 writer",
)
compiler.write_text(text)

workflow = Path(".github/workflows/lua-stack-abi-validation.yml")
text = workflow.read_text()
if "Run hybrid Core RV64 production routing differential" not in text:
    anchor = "      - name: Run GCC-MiniC fixed stack-argument differential\n"
    step = r'''      - name: Run hybrid Core RV64 production routing differential
        shell: bash
        run: |
          set -Eeuo pipefail
          work=build/core-hybrid-diff
          compiler=build/rv64-stack-abi-compiler/bin/minic
          source_file=tests/target/riscv64/core_hybrid_differential.i
          runtime=tests/target/riscv64/core_hybrid_differential_runtime.c
          mkdir -p "$work"
          "$compiler" -S "$source_file" -o "$work/legacy.s"
          MINIC_CORE_CODEGEN=basic-v0 \
            "$compiler" -S "$source_file" -o "$work/hybrid.s"
          grep -q '^\.Lcore_hybrid_core_core_bb0:' "$work/hybrid.s"
          grep -q '^\.Lcore_hybrid_fallback_load_return:' "$work/hybrid.s"
          if grep -q '^\.Lcore_hybrid_fallback_load_core_bb' "$work/hybrid.s"; then
            printf '%s\n' 'fallback function unexpectedly used Core emitter' >&2
            exit 1
          fi
          if MINIC_CORE_CODEGEN=invalid \
              "$compiler" -S "$source_file" -o "$work/invalid.s" \
              >"$work/invalid.stdout" 2>"$work/invalid.stderr"; then
            printf '%s\n' 'invalid Core codegen mode unexpectedly succeeded' >&2
            exit 1
          fi
          grep -F "MINIC_CORE_CODEGEN must be unset or 'basic-v0'" "$work/invalid.stderr"
          riscv64-linux-gnu-gcc -static -O2 \
            "$source_file" "$runtime" -o "$work/reference-rv64"
          riscv64-linux-gnu-gcc -static -O2 \
            "$runtime" "$work/legacy.s" -o "$work/legacy-rv64"
          riscv64-linux-gnu-gcc -static -O2 \
            "$runtime" "$work/hybrid.s" -o "$work/hybrid-rv64"
          qemu-riscv64 "$work/reference-rv64" > "$work/reference.out"
          qemu-riscv64 "$work/legacy-rv64" > "$work/legacy.out"
          qemu-riscv64 "$work/hybrid-rv64" > "$work/hybrid.out"
          cmp "$work/reference.out" "$work/legacy.out"
          cmp "$work/reference.out" "$work/hybrid.out"

'''
    text = replace_once(text, anchor, step + anchor, "RV64 workflow")
    workflow.write_text(text)
