#include "frontend/ast.h"
#include "frontend/parser.h"

#include <stdio.h>
#include <string.h>

static const MinicFunction *find_function(const MinicC0Program *program,
                                          const char *name) {
    size_t i;
    for (i = 0U; i < program->function_count; ++i) {
        const MinicFunction *function = &program->functions[i];
        if (strlen(name) == function->name_length &&
            memcmp(name, function->name, function->name_length) == 0)
            return function;
    }
    return NULL;
}

static int expect_diagnostic_pair(const char *source, const char *name,
                                  const char *error_message,
                                  const char *warning_message) {
    MinicC0Program program;
    MinicDiagnostic diagnostic;
    const MinicFunction *function;
    char mutable_source[2048];
    size_t source_length = strlen(source);
    bool success;
    int result = 0;

    if (source_length >= sizeof(mutable_source)) return 1;
    (void)memcpy(mutable_source, source, source_length + 1U);
    (void)memset(&diagnostic, 0, sizeof(diagnostic));
    minic_c0_program_initialize(&program);
    success = minic_parse_c0_program(
        "function_diagnostic_attribute_test.c", mutable_source,
        source_length, &program, &diagnostic);
    if (!success) {
        (void)fprintf(stderr, "unexpected parse failure: %s\n", diagnostic.message);
        result = 2;
    } else {
        /* Source buffer is not the owner of persistent function metadata. */
        (void)memset(mutable_source, '?', source_length);
        function = find_function(&program, name);
        if (function == NULL ||
            ((error_message == NULL) != (function->error_message == NULL)) ||
            ((warning_message == NULL) != (function->warning_message == NULL)) ||
            (error_message != NULL &&
             (function->error_message_length != strlen(error_message) ||
              memcmp(function->error_message, error_message, strlen(error_message)) != 0)) ||
            (warning_message != NULL &&
             (function->warning_message_length != strlen(warning_message) ||
              memcmp(function->warning_message, warning_message,
                     strlen(warning_message)) != 0))) {
            (void)fprintf(stderr, "diagnostic ownership mismatch: %s\n", name);
            result = 3;
        }
    }
    minic_c0_program_destroy(&program);
    return result;
}

static int expect_parse_failure(const char *source, const char *expected) {
    MinicC0Program program;
    MinicDiagnostic diagnostic;
    bool parsed;
    (void)memset(&diagnostic, 0, sizeof(diagnostic));
    minic_c0_program_initialize(&program);
    parsed = minic_parse_c0_program(
        "function_diagnostic_attribute_test.c", source, strlen(source),
        &program, &diagnostic);
    minic_c0_program_destroy(&program);
    if (parsed || strstr(diagnostic.message, expected) == NULL) {
        (void)fprintf(stderr, "expected parse failure containing '%s', got '%s'\n",
                      expected, diagnostic.message);
        return 1;
    }
    return 0;
}

int main(void) {
    if (expect_diagnostic_pair(
            "extern int guard(int) __attribute__((error(\"compile failed\")));"
            "extern int guard(int);"
            "int consumer(void) { extern int guard(int) "
            "__attribute__((error(\"compile failed\"))); return 0; }",
            "guard", "compile failed", NULL) ||
        expect_diagnostic_pair(
            "__attribute__((warning(\"watch out\"))) extern int warn(int);"
            "int warn(int value) { return value; }",
            "warn", NULL, "watch out") ||
        expect_diagnostic_pair(
            "int consumer(void) { extern int check(int) "
            "__attribute__((error(\"block-only\"))); return 0; }",
            "check", "block-only", NULL) ||
        expect_diagnostic_pair(
            "int consume(void) { __attribute__((error(\"block-prefix\"))) "
            "extern int guard(void); return 0; }",
            "guard", "block-prefix", NULL) ||
        expect_diagnostic_pair(
            "extern int f(int) __attribute__((error(\"same\")));"
            "extern int f(int) __attribute__((error(\"same\")));",
            "f", "same", NULL) ||
        expect_diagnostic_pair(
            "extern int f(void) __attribute__((error(\"one\")));"
            "extern int f(void) __attribute__((error(\"two\")));",
            "f", "two", NULL) ||
        expect_diagnostic_pair(
            "extern int f(void) __attribute__((warning(\"one\")));"
            "extern int f(void) __attribute__((error(\"one\")));",
            "f", "one", "one") ||
        expect_diagnostic_pair(
            "extern int f(void) __attribute__((error(\"one\")));"
            "int caller(void) { extern int f(void) "
            "__attribute__((warning(\"warn\"))); return 0; }"
            "extern int f(void) __attribute__((error(\"last\")));",
            "f", "last", "warn") ||
        expect_diagnostic_pair(
            "extern int f(void) __attribute__((warning(\"first\"),"
            "error(\"stop\"),warning(\"last\")));",
            "f", "stop", "last") ||
        expect_diagnostic_pair(
            "extern int f(void) __attribute__((error(\"max\"\"(\"\"0L\""
            "\", \"\"delta\"\") signedness error\")));",
            "f", "max(0L, delta) signedness error", NULL) ||
        expect_diagnostic_pair(
            "extern int f(void) __attribute__((error(\"one\\n\"\"two\"),"
            "warning(\"a\\t\"\"b\")));",
            "f", "one\ntwo", "a\tb") ||
        expect_diagnostic_pair(
            "extern int f(void) __attribute__((error(\"\")));",
            "f", "", NULL) ||
        expect_parse_failure(
            "int caller(void) { __attribute__((error(\"bad\"))) "
            "extern int object; return 0; }",
            "cannot apply to an object") ||
        expect_parse_failure(
            "extern int f(void) __attribute__((error));",
            "requires one string literal") ||
        expect_parse_failure(
            "extern int f(void) __attribute__((error(12)));",
            "requires one string literal") ||
        expect_parse_failure(
            "extern int f(void) __attribute__((error(\"one\",\"two\")));",
            "requires one string literal") ||
        expect_parse_failure(
            "extern int f(void) __attribute__((error(\"one\" 123 \"two\")));",
            "requires one string literal")) {
        return 1;
    }
    (void)puts("function diagnostic attribute ownership PASS");
    return 0;
}
