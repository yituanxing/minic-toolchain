#ifndef MINIC_COMPILER_H
#define MINIC_COMPILER_H

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
 * Compile one normalized, preprocessed C translation unit to RISC-V assembly.
 * 将一个规范化的预处理后 C 翻译单元编译为 RISC-V 汇编。
 *
 * C0 accepts only an int main function with an optional integer return.
 * C0 目前只接受 int main 函数，以及可选的整数 return 语句。
 */
int minic_compile_preprocessed_file(
    const char *input_path,
    const char *output_path,
    MinicDiagnostic *diagnostic);

#ifdef __cplusplus
}
#endif

#endif
