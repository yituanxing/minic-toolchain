#ifndef MINIC_TOOLS_MINIC_DRIVER_INTERNAL_H
#define MINIC_TOOLS_MINIC_DRIVER_INTERNAL_H

#include "target/driver_stage_recipe.h"

#include <stdbool.h>
#include <stddef.h>

#define MINIC_DRIVER_MAX_INPUTS 128U
#define MINIC_DRIVER_MAX_FORWARD_OPTIONS 128U
#define MINIC_DRIVER_MAX_LINK_ARGUMENTS 512U
#define MINIC_DRIVER_MAX_MACHINE_OPTIONS 16U

typedef enum MinicDriverMode {
    MINIC_DRIVER_LINK,
    MINIC_DRIVER_COMPILE,
    MINIC_DRIVER_ASSEMBLY
} MinicDriverMode;

/* C ABI overrides belong to a single driver invocation, never TargetInfo. */
typedef struct MinicDriverLanguageOptions {
    const char *plain_char;
    const char *wide_char;
    const char *relocation;
} MinicDriverLanguageOptions;

/* Immutable after CLI validation. Paths and options borrow argv/environment;
 * they remain valid until execution finishes. A target does not gain an AS/LD
 * pipeline merely because its backend can produce -S output. */
typedef struct MinicDriverInvocation {
    MinicDriverMode mode;
    /* NULL for every -S invocation; only an explicitly registered product
     * stage provides CPP/AS/LD details to the executor. */
    const MinicTargetDriverStageRecipe *stage_recipe;
    const char *target_name;
    const char *inputs[MINIC_DRIVER_MAX_INPUTS];
    size_t input_count;
    const char *output;
    const char *sysroot;
    bool hosted;
    bool static_link;
    bool no_stdlib;
    bool no_startfiles;
    bool no_defaultlibs;
    bool no_stdinc;
    bool function_sections;
    const char *march;
    const char *mabi;
    const char *runtime_archive;
    MinicDriverLanguageOptions language;
    const char *cpp_forward[MINIC_DRIVER_MAX_FORWARD_OPTIONS];
    size_t cpp_forward_count;
    const char *ld_forward[MINIC_DRIVER_MAX_FORWARD_OPTIONS];
    size_t ld_forward_count;
    const char *machine_codegen_options[MINIC_DRIVER_MAX_MACHINE_OPTIONS];
    size_t machine_codegen_option_count;
} MinicDriverInvocation;

/* 0 = ready, 1 = --help printed, 2 = usage/error printed. */
int minic_driver_cli_parse(int argc, char **argv, MinicDriverInvocation *invocation);
int minic_driver_execute(const char *argv0,
                         const MinicDriverInvocation *invocation);

#endif
