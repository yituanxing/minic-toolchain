#ifndef MINIC_COMPILER_H
#define MINIC_COMPILER_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MinicDiagnostic {
    const char *path;
    size_t line;
    size_t column;
    char message[256];
} MinicDiagnostic;

/*
 * Compile one preprocessed C translation unit to RISC-V64 assembly using
 * the default target. This compatibility entry point retains the original
 * source-level ABI; it does not restrict the input to the original C0 subset.
 * For an explicit target architecture, use the target-aware entry point below.
 */
int minic_compile_preprocessed_file(const char *input_path,
                                    const char *output_path,
                                    MinicDiagnostic *diagnostic);

/* GNU-compatible layout selection: false (default) shares .text;
 * true (-ffunction-sections) gives each function an input section. */
struct MinicTargetInfo;

/* Target-aware C-to-assembly entry point. RV64 and x86-64 have independent
 * code generators; target parsing, DataLayout and Core lowering share the
 * selected MinicTargetInfo. Unsupported combinations reject explicitly,
 * never silently emitting assembly for a different target.
 * GNU assembly, linking and runtime are separate validation stages. */
int minic_compile_preprocessed_file_for_target(const char *input_path,
                                               const char *output_path,
                                               const struct MinicTargetInfo *target_info,
                                               bool function_sections,
                                               MinicDiagnostic *diagnostic);

int minic_compile_preprocessed_file_with_options(const char *input_path,
                                                 const char *output_path,
                                                 bool function_sections,
                                                 MinicDiagnostic *diagnostic);

#ifdef __cplusplus
}
#endif

#endif
