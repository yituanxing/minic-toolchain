#include "minic/compiler.h"

#include "frontend/parser.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct MinicSourceBuffer {
    char *data;
    size_t size;
} MinicSourceBuffer;

static void minic_set_diagnostic(
    MinicDiagnostic *diagnostic,
    const char *path,
    size_t line,
    size_t column,
    const char *format,
    ...)
{
    va_list arguments;

    if (diagnostic == NULL) {
        return;
    }

    diagnostic->path = path;
    diagnostic->line = line;
    diagnostic->column = column;

    va_start(arguments, format);
    (void)vsnprintf(
        diagnostic->message,
        sizeof(diagnostic->message),
        format,
        arguments);
    va_end(arguments);
}

static bool minic_read_file(
    const char *path,
    MinicSourceBuffer *buffer,
    MinicDiagnostic *diagnostic)
{
    FILE *file;
    long end_position;
    size_t size;
    char *data;

    file = fopen(path, "rb");
    if (file == NULL) {
        minic_set_diagnostic(
            diagnostic,
            path,
            1U,
            1U,
            "cannot open input: %s",
            strerror(errno));
        return false;
    }

    if (fseek(file, 0L, SEEK_END) != 0) {
        minic_set_diagnostic(
            diagnostic,
            path,
            1U,
            1U,
            "cannot seek input: %s",
            strerror(errno));
        (void)fclose(file);
        return false;
    }

    end_position = ftell(file);
    if (end_position < 0L) {
        minic_set_diagnostic(
            diagnostic,
            path,
            1U,
            1U,
            "cannot measure input: %s",
            strerror(errno));
        (void)fclose(file);
        return false;
    }

    if ((unsigned long)end_position > (unsigned long)(SIZE_MAX - 1U)) {
        minic_set_diagnostic(
            diagnostic,
            path,
            1U,
            1U,
            "input is too large");
        (void)fclose(file);
        return false;
    }

    size = (size_t)end_position;
    if (fseek(file, 0L, SEEK_SET) != 0) {
        minic_set_diagnostic(
            diagnostic,
            path,
            1U,
            1U,
            "cannot rewind input: %s",
            strerror(errno));
        (void)fclose(file);
        return false;
    }

    data = (char *)malloc(size + 1U);
    if (data == NULL) {
        minic_set_diagnostic(
            diagnostic,
            path,
            1U,
            1U,
            "out of memory while reading input");
        (void)fclose(file);
        return false;
    }

    if (size != 0U && fread(data, 1U, size, file) != size) {
        minic_set_diagnostic(
            diagnostic,
            path,
            1U,
            1U,
            "cannot read input: %s",
            ferror(file) != 0 ? strerror(errno) : "unexpected end of file");
        free(data);
        (void)fclose(file);
        return false;
    }

    data[size] = '\0';
    if (fclose(file) != 0) {
        minic_set_diagnostic(
            diagnostic,
            path,
            1U,
            1U,
            "cannot close input: %s",
            strerror(errno));
        free(data);
        return false;
    }

    buffer->data = data;
    buffer->size = size;
    return true;
}

static bool minic_write_riscv_assembly(
    const char *path,
    int return_value,
    MinicDiagnostic *diagnostic)
{
    FILE *file;

    file = fopen(path, "wb");
    if (file == NULL) {
        minic_set_diagnostic(
            diagnostic,
            path,
            1U,
            1U,
            "cannot open output: %s",
            strerror(errno));
        return false;
    }

    if (fprintf(
            file,
            ".text\n"
            ".globl main\n"
            ".type main, @function\n"
            "main:\n"
            "  li a0, %d\n"
            "  ret\n"
            ".size main, .-main\n",
            return_value) < 0) {
        minic_set_diagnostic(
            diagnostic,
            path,
            1U,
            1U,
            "cannot write output: %s",
            strerror(errno));
        (void)fclose(file);
        return false;
    }

    if (fclose(file) != 0) {
        minic_set_diagnostic(
            diagnostic,
            path,
            1U,
            1U,
            "cannot close output: %s",
            strerror(errno));
        return false;
    }

    return true;
}

int minic_compile_preprocessed_file(
    const char *input_path,
    const char *output_path,
    MinicDiagnostic *diagnostic)
{
    MinicSourceBuffer buffer;
    int return_value;
    bool success;

    if (input_path == NULL || output_path == NULL) {
        minic_set_diagnostic(
            diagnostic,
            input_path,
            1U,
            1U,
            "input and output paths are required");
        return 1;
    }

    if (diagnostic != NULL) {
        diagnostic->path = input_path;
        diagnostic->line = 1U;
        diagnostic->column = 1U;
        diagnostic->message[0] = '\0';
    }

    buffer.data = NULL;
    buffer.size = 0U;
    if (!minic_read_file(input_path, &buffer, diagnostic)) {
        return 1;
    }

    success = minic_parse_c0_translation_unit(
        input_path,
        buffer.data,
        buffer.size,
        &return_value,
        diagnostic);
    if (success) {
        success = minic_write_riscv_assembly(
            output_path,
            return_value,
            diagnostic);
    }

    free(buffer.data);
    return success ? 0 : 1;
}
