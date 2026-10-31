#define _POSIX_C_SOURCE 200809L
#include "target/loongarch64/codegen.h"
#include "target/loongarch64/block_emit.h"
#include "target/loongarch64/frame_emit.h"
#include "target/loongarch64/memory_emit.h"
#include "target/loongarch64/asm_contract.h"
#include "target/common/direct_function_emit.h"
#include "target/common/lp64_aggregate.h"
#include "target/common/direct_program_emit.h"
#include "core/value_uses.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Keep the scope of this native compactor deliberately narrow. A proven
 * compiler-owned SSA writeback marks a stack slot that cannot be a volatile
 * C object. The GPR may then retain that exact value across native scalar
 * operations which do not write it, without a redundant stack reload. Any
 * unknown instruction, branch, call, inline ASM or possible aliasing store
 * drops the cache; function/block boundaries never share it. */
static bool loongarch64_line_starts(const char *line, size_t length,
                                   const char *prefix) {
    const size_t n = strlen(prefix);
    return length >= n && memcmp(line, prefix, n) == 0;
}

/* The shared Core use census proves a marked SSA result has precisely one
 * semantic consumer. If the very next machine instruction merely reloads
 * that result into the unchanged $t0, both the spill and reload disappear.
 * Never apply this to a C object, an ASM memory reference, a cross-block
 * use, or a value with multiple Core uses. */
/* 64-bit reloads preserve the machine register exactly. A 32-bit reload
 * sign-extends, so replace the pair with SLLI.W-by-zero instead of dropping
 * its narrowing semantics. Return 0 (no match), 1 (identity), or 2 (sext). */
static unsigned int loongarch64_exact_spill_reload(const char *store,
                                                  size_t store_length,
                                                  const char *load,
                                                  size_t load_length) {
    static const char *const bases[] = {"$sp", "$s0"};
    size_t i;
    for (i = 0U; i < sizeof(bases)/sizeof(bases[0]); ++i) {
        char st[40], ld[40];
        size_t sw, lw;
        for (sw = 0U; sw < 2U; ++sw)
            for (lw = 0U; lw < 2U; ++lw) {
                int s, l;
                if (sw == 1U && lw == 0U) continue;
                s = snprintf(st, sizeof(st), "  st.%c $t0, %s, ",
                             sw ? 'w' : 'd', bases[i]);
                l = snprintf(ld, sizeof(ld), "  ld.%c $t0, %s, ",
                             lw ? 'w' : 'd', bases[i]);
                if (s > 0 && l > 0 && (size_t)s < sizeof(st) &&
                    (size_t)l < sizeof(ld) &&
                    store_length > (size_t)s && load_length > (size_t)l &&
                    memcmp(store, st, (size_t)s) == 0 &&
                    memcmp(load, ld, (size_t)l) == 0 &&
                    store_length - (size_t)s == load_length - (size_t)l &&
                    memcmp(store + s, load + l,
                           load_length - (size_t)l) == 0)
                    return lw ? 2U : 1U;
            }
    }
    return 0U;
}

static unsigned int loongarch64_exact_parameter_reload(
    const char *store, size_t store_length,
    const char *load, size_t load_length, unsigned int parameter) {
    static const char *const bases[] = {"$sp", "$s0"};
    size_t i, word;
    if (parameter >= 8U) return 0U;
    for (i = 0U; i < sizeof(bases)/sizeof(bases[0]); ++i)
        for (word = 0U; word < 2U; ++word) {
            char st[48], ld[48];
            int s = snprintf(st, sizeof(st), "  st.%c $a%u, %s, ",
                             word ? 'w' : 'd', parameter, bases[i]);
            int l = snprintf(ld, sizeof(ld), "  ld.%c $t0, %s, ",
                             word ? 'w' : 'd', bases[i]);
            if (s > 0 && l > 0 && (size_t)s < sizeof(st) &&
                (size_t)l < sizeof(ld) && store_length > (size_t)s &&
                load_length > (size_t)l &&
                memcmp(store, st, (size_t)s) == 0 &&
                memcmp(load, ld, (size_t)l) == 0 &&
                store_length - (size_t)s == load_length - (size_t)l &&
                memcmp(store + s, load + l,
                       load_length - (size_t)l) == 0)
                return word ? 2U : 1U;
        }
    return 0U;
}

static size_t loongarch64_machine_line_length(const char *line,
                                               const char *end) {
    const char *newline = memchr(line, '\n', (size_t)(end - line));
    return newline != NULL
        ? (size_t)(newline - line) + 1U : (size_t)(end - line);
}

static bool loongarch64_branch_falls_into(
    const char *branch, size_t branch_length, const char *prefix,
    const char *label, size_t label_length) {
    const size_t p = strlen(prefix);
    const size_t n = branch_length > p ? branch_length - p - 1U : 0U;
    return loongarch64_line_starts(branch, branch_length, prefix) &&
           n > strlen(".Lmini_loong_f") &&
           memcmp(branch + p, ".Lmini_loong_f",
                  strlen(".Lmini_loong_f")) == 0 &&
           label_length == n + 2U &&
           memcmp(label, branch + p, n) == 0 &&
           label[n] == ':' && label[n + 1U] == '\n';
}

/* LoongArch .W arithmetic and LD.W/LI.W architecturally sign-extend their
 * result to GRLEN. An adjacent, compiler-generated SSA word writeback can
 * therefore be forwarded even without a replacement sign extension. */
static bool loongarch64_gp_already_sext32(const char *line,
                                         size_t length) {
    static const char *const opcodes[] = {
        "ld.w", "ldptr.w", "li.w", "add.w", "addi.w", "sub.w",
        "mul.w", "div.w", "div.wu", "mod.w", "mod.wu",
        "slli.w", "srai.w", "srli.w", "sll.w", "sra.w", "srl.w",
        "slt", "sltu", "slti", "sltui"
    };
    size_t i;
    char prefix[40];
    for (i = 0U; i < sizeof(opcodes)/sizeof(opcodes[0]); ++i) {
        int n = snprintf(prefix, sizeof(prefix), "  %s $t0, ", opcodes[i]);
        if (n > 0 && (size_t)n < sizeof(prefix) &&
            loongarch64_line_starts(line, length, prefix))
            return true;
    }
    return false;
}

static bool loongarch64_forward_single_use_spills(FILE *out,
    const char *buffer, size_t length, const size_t *uses,
    size_t value_count) {
    const char *line = buffer, *end = buffer + length;
    const char *previous = NULL;
    size_t previous_length = 0U;
    bool inline_asm = false;
    if (out == NULL || buffer == NULL) return false;
    while (line < end) {
        size_t n = loongarch64_machine_line_length(line, end);
        const char *marker = line + n;
        if (loongarch64_line_starts(line, n,
                                   "# MINI_LOONG_INLINE_ASM_BEGIN"))
            inline_asm = true;
        else if (loongarch64_line_starts(line, n,
                                        "# MINI_LOONG_INLINE_ASM_END"))
            inline_asm = false;
        if (!inline_asm && marker < end) {
            size_t m = loongarch64_machine_line_length(marker, end);
            if (loongarch64_branch_falls_into(
                    line, n, "  b ", marker, m)) {
                previous = NULL;
                line = marker;
                continue;
            }
            if (loongarch64_line_starts(line, n,
                                       "  bnez $t0, .Lmini_loong_f") &&
                loongarch64_line_starts(marker, m,
                                       "  b .Lmini_loong_f") &&
                marker + m < end) {
                const char *fallthrough = marker + m;
                size_t f = loongarch64_machine_line_length(fallthrough, end);
                if (loongarch64_branch_falls_into(
                        marker, m, "  b ", fallthrough, f)) {
                    if (fwrite(line, 1U, n, out) != n) return false;
                    previous = NULL;
                    line = fallthrough;
                    continue;
                }
                /* A short conditional may reach the former unconditional
                 * target in functions with <1 MiB total source assembly.
                 * This is much smaller than LoongArch's B[EQ]Z reach; do
                 * not invert very large functions without range proof. */
                if (length < 1000000U &&
                    loongarch64_branch_falls_into(
                        line, n, "  bnez $t0, ", fallthrough, f)) {
                    if (fprintf(out, "  beqz $t0, %.*s\n",
                            (int)(m - strlen("  b ") - 1U),
                            marker + strlen("  b ")) < 0)
                        return false;
                    previous = NULL;
                    line = fallthrough;
                    continue;
                }
            }
        }
        if (!inline_asm && uses != NULL && marker < end &&
            loongarch64_line_starts(marker, (size_t)(end - marker),
                                    "# MINI_LOONG_SSA_WRITEBACK ")) {
            unsigned int value = 0U;
            int consumed = 0;
            size_t m = loongarch64_machine_line_length(marker, end);
            const char *load = marker + m;
            unsigned int direct_forward = load < end
                ? loongarch64_exact_spill_reload(line, n, load,
                    loongarch64_machine_line_length(load, end))
                : 0U;
            if (sscanf(marker, "# MINI_LOONG_SSA_WRITEBACK %u%n",
                       &value, &consumed) == 1 && consumed > 0 &&
                (marker[consumed] == '\n' || marker[consumed] == '\0') &&
                value < value_count && uses[value] == 1U &&
                direct_forward != 0U) {
                if (direct_forward == 2U &&
                    !(previous != NULL &&
                      loongarch64_gp_already_sext32(
                          previous, previous_length)) &&
                    fputs("  slli.w $t0, $t0, 0\n", out) == EOF)
                    return false;
                line = load + loongarch64_machine_line_length(load, end);
                previous = NULL;
                continue;
            }
        }
        if (!inline_asm && uses != NULL && marker < end &&
            loongarch64_line_starts(marker, (size_t)(end - marker),
                                    "# MINI_LOONG_SSA_PARAMETER ")) {
            unsigned int value = 0U, parameter = 0U;
            int consumed = 0;
            size_t m = loongarch64_machine_line_length(marker, end);
            const char *load = marker + m;
            unsigned int direct_forward;
            if (sscanf(marker, "# MINI_LOONG_SSA_PARAMETER %u %u%n",
                       &value, &parameter, &consumed) == 2 &&
                consumed > 0 &&
                (marker[consumed] == '\n' || marker[consumed] == '\0') &&
                value < value_count && uses[value] == 1U && load < end) {
                direct_forward = loongarch64_exact_parameter_reload(
                    line, n, load,
                    loongarch64_machine_line_length(load, end), parameter);
                if (direct_forward != 0U) {
                    if (fprintf(out,
                            direct_forward == 1U
                                ? "  move $t0, $a%u\n"
                                : "  slli.w $t0, $a%u, 0\n",
                            parameter) < 0)
                        return false;
                    line = load + loongarch64_machine_line_length(load, end);
                    previous = NULL;
                    continue;
                }
            }
        }
        if (fwrite(line, 1U, n, out) != n) return false;
        previous = line;
        previous_length = n;
        line += n;
    }
    return true;
}

typedef struct LoongGPFrameCache {
    /* A GPR can equal several SSA slots when its write-through copy is
     * stored more than once without clobbering the GPR. */
    struct { char base[3]; size_t offset; } slots[16];
    size_t count;
} LoongGPFrameCache;

static bool loongarch64_parse_frame_gpr(const char *line,
                                        const char *opcode,
                                        unsigned int *reg,
                                        char base[3], size_t *offset) {
    char pattern[40];
    int count = 0;
    if (snprintf(pattern, sizeof(pattern), "  %s $t%%u, $%%2[a-z0-9], %%zu%%n",
                 opcode) < 0 ||
        sscanf(line, pattern, reg, base, offset, &count) != 3 ||
        count == 0 || (line[count] != '\n' && line[count] != '\0') ||
        *reg > 8U ||
        (strcmp(base, "sp") != 0 && strcmp(base, "s0") != 0))
        return false;
    return true;
}

static bool loongarch64_cache_has(const LoongGPFrameCache *cache,
                                  const char *base, size_t offset) {
    size_t i;
    for (i = 0U; i < cache->count; ++i)
        if (strcmp(cache->slots[i].base, base) == 0 &&
            cache->slots[i].offset == offset)
            return true;
    return false;
}

static void loongarch64_cache_add(LoongGPFrameCache *cache,
                                 const char *base, size_t offset) {
    if (cache->count >= sizeof(cache->slots)/sizeof(cache->slots[0]) ||
        loongarch64_cache_has(cache, base, offset)) return;
    memcpy(cache->slots[cache->count].base, base, 3U);
    cache->slots[cache->count].offset = offset;
    ++cache->count;
}

static bool loongarch64_invalidate_frame_store(
    LoongGPFrameCache cache[9], const char *line) {
    static const struct { const char *op; size_t width; } stores[] = {
        {"st.d", 8U}, {"st.w", 4U}, {"st.h", 2U}, {"st.b", 1U},
        {"stptr.d", 8U}, {"stptr.w", 4U}
    };
    unsigned int source;
    char base[3] = {0};
    size_t offset, i, r;
    for (i = 0U; i < sizeof(stores)/sizeof(stores[0]); ++i) {
        if (!loongarch64_parse_frame_gpr(
                line, stores[i].op, &source, base, &offset))
            continue;
        for (r = 0U; r < 9U; ++r) {
            LoongGPFrameCache *c = &cache[r];
            size_t j = 0U;
            while (j < c->count) {
                const size_t candidate = c->slots[j].offset;
                if (strcmp(base, c->slots[j].base) == 0 &&
                    offset < candidate + 8U &&
                    candidate < offset + stores[i].width) {
                    c->slots[j] = c->slots[--c->count];
                    continue;
                }
                ++j;
            }
        }
        return true;
    }
    return false;
}

static bool loongarch64_pure_scalar_opcode(const char *op) {
    static const char *const prefixes[] = {
        "add", "sub", "mul", "div", "mod", "and", "or", "xor", "nor",
        "sll", "srl", "sra", "rot", "slt", "bstr", "ext", "clz",
        "ctz", "rev", "bytepick", "mask", "li.", "la.", "lu",
        "move", "pcaddi", "pcaddu"
    };
    size_t i;
    for (i = 0U; i < sizeof(prefixes)/sizeof(prefixes[0]); ++i)
        if (strncmp(op, prefixes[i], strlen(prefixes[i])) == 0)
            return true;
    return false;
}

static bool loongarch64_redundant_sign_extend(
    const char *previous, size_t previous_length,
    const char *line, size_t length) {
    static const char zero_shift[] = "  slli.w $t0, $t0, 0\n";
    return length == sizeof(zero_shift) - 1U &&
           memcmp(line, zero_shift, sizeof(zero_shift) - 1U) == 0 &&
           (loongarch64_line_starts(previous, previous_length, "  ld.w $t0, ") ||
            loongarch64_line_starts(previous, previous_length, "  ldptr.w $t0, ") ||
            loongarch64_line_starts(previous, previous_length, "  li.w $t0, ") ||
            loongarch64_line_starts(previous, previous_length, "  add.w $t0, ") ||
            loongarch64_line_starts(previous, previous_length, "  sub.w $t0, ") ||
            loongarch64_line_starts(previous, previous_length, "  mul.w $t0, "));
}

static bool loongarch64_emit_compact_assembly(FILE *out,
                                              const char *buffer,
                                              size_t buffer_length) {
    const char *line = buffer, *previous = NULL;
    size_t previous_length = 0U;
    const char *end = buffer + buffer_length;
    LoongGPFrameCache cache[9];
    bool inline_asm = false;
    if (out == NULL || buffer == NULL) return false;
    memset(cache, 0, sizeof(cache));
    while (line < end) {
        const char *newline = memchr(line, '\n', (size_t)(end - line));
        size_t length = newline != NULL
            ? (size_t)(newline - line) + 1U : (size_t)(end - line);
        unsigned int reg = 0U;
        char base[3] = {0};
        size_t offset = 0U;
        bool skip = false;
        if (loongarch64_line_starts(line, length,
                                   "# MINI_LOONG_SSA_WRITEBACK")) {
            if (previous != NULL &&
                loongarch64_parse_frame_gpr(previous, "st.d", &reg,
                                            base, &offset))
                loongarch64_cache_add(&cache[reg], base, offset);
            line += length;
            continue;
        }
        if (loongarch64_line_starts(line, length,
                                   "# MINI_LOONG_SSA_PARAMETER")) {
            line += length;
            continue;
        }
        if (loongarch64_line_starts(line, length,
                                   "# MINI_LOONG_INLINE_ASM_BEGIN")) {
            memset(cache, 0, sizeof(cache));
            inline_asm = true;
        } else if (loongarch64_line_starts(line, length,
                                          "# MINI_LOONG_INLINE_ASM_END")) {
            memset(cache, 0, sizeof(cache));
            inline_asm = false;
        } else if (inline_asm) {
            /* Do not examine, remove, or rewrite GNU/user instructions. */
        } else if (loongarch64_parse_frame_gpr(line, "ld.d", &reg,
                                                base, &offset)) {
            skip = loongarch64_cache_has(&cache[reg], base, offset);
            if (!skip) cache[reg].count = 0U;
        } else if (loongarch64_line_starts(line, length, "  st.") ||
                   loongarch64_line_starts(line, length, "  stptr.")) {
            /* Exact native frame stores cannot modify disjoint SSA slots.
             * Any indirect store remains a hard alias barrier. */
            if (!loongarch64_invalidate_frame_store(cache, line))
                memset(cache, 0, sizeof(cache));
        } else if (loongarch64_line_starts(line, length, "  ld.")) {
            char op[24], dest[24];
            if (sscanf(line, "  %23s %23[^, \t\n]", op, dest) == 2 &&
                sscanf(dest, "$t%u", &reg) == 1 && reg <= 8U)
                cache[reg].count = 0U;
            else
                memset(cache, 0, sizeof(cache));
        } else if (loongarch64_line_starts(line, length, "  ")) {
            char op[24], dest[24];
            if (sscanf(line, "  %23s %23[^, \t\n]", op, dest) != 2 ||
                !loongarch64_pure_scalar_opcode(op))
                memset(cache, 0, sizeof(cache));
            else if (sscanf(dest, "$t%u", &reg) == 1 && reg <= 8U)
                cache[reg].count = 0U;
            else if (strcmp(dest, "$sp") == 0 ||
                     strcmp(dest, "$s0") == 0)
                memset(cache, 0, sizeof(cache));
        } else {
            /* .L labels and branches join multiple predecessor states. */
            memset(cache, 0, sizeof(cache));
        }
        if (!inline_asm && previous != NULL &&
            loongarch64_redundant_sign_extend(previous, previous_length,
                                              line, length))
            skip = true;
        if (skip) {
            line += length;
            continue;
        }
        if (fwrite(line, 1U, length, out) != length) return false;
        previous = line;
        previous_length = length;
        line += length;
    }
    return true;
}

/* LoongArch LP64D is not AAPCS64 with renamed registers: notably the first
 * GAR holds an indirect result, aggregate pairs may split, and va_list is
 * a contiguous linear cursor. Keep those choices alongside machine emission. */
static const MinicDirectLP64AbiPolicy loongarch64_abi_policy = {
    .name = "loongarch64-lp64d",
    .gp_argument_registers = 8U,
    .fp_argument_registers = 8U,
    .stack_slot_bytes = 8U,
    .stack_alignment = 16U,
    .indirect_result_argument_registers = 1U,
    .record_registers = MINIC_DIRECT_LP64_RECORD_SPLIT_WITH_STACK,
    .wide_registers = MINIC_DIRECT_LP64_WIDE_SPLIT_WITH_STACK,
    .long_double_in_fp_register = false,
    .variadic_floating_in_fp_register = false,
    .variadic_named_double_supported = false,
    .variadic_align_wide_gp_pair = true,
    .variadic_cursor = MINIC_DIRECT_LP64_VARIADIC_LINEAR_GP,
    .variadic_save_bytes = 64U,
    .variadic_save_alignment = 8U,
    .extra_variadic_cursor_bytes = 0U,
    .variadic_requires_public_stack_alignment = true
};

static bool loongarch64_executed_fp_type(MinicType type) {
    return minic_type_is_float(type) || minic_type_is_double(type) ||
           minic_type_is_long_double(type) || minic_type_is_float128(type);
}

/* LP64S passes floating arguments in GPRs and forbids compiler-generated FPU
 * instructions in the kernel. The integer ABI remains compatible with LP64D,
 * but we must not miscompile a future float operation or FP-valued call while
 * soft-float arithmetic/return lowering is still unavailable. Declarations
 * alone (including external FP prototypes and float-pointer types) are fine. */
static bool loongarch64_lp64s_executable_supported(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn) {
    size_t i;
    size_t count, width, j;
    if (fn == NULL || program == NULL || target == NULL ||
        loongarch64_executed_fp_type(fn->return_type)) return false;
    if (minic_type_is_record(fn->return_type) &&
        minic_multi_fp_return_record(program, target, fn->return_type,
                                     &count, &width)) return false;
    for (i = 0U; i < fn->parameter_count; ++i)
        if (loongarch64_executed_fp_type(fn->parameter_types[i])) return false;
    /* Validate actual Core call signatures, not every source declaration.
     * An FP formal type can exist even when argument conversion was folded
     * or no FP-typed SSA operand survives in the call lowering. */
    for (i = 0U; i < fn->callee_count; ++i) {
        const MinicCoreCallee *call = &fn->callees[i];
        if (loongarch64_executed_fp_type(call->return_type)) return false;
        for (j = 0U; j < call->parameter_count; ++j)
            if (loongarch64_executed_fp_type(call->parameter_types[j]))
                return false;
    }
    for (i = 0U; i < fn->call_signature_count; ++i) {
        const MinicCoreCallSignature *call = &fn->call_signatures[i];
        if (loongarch64_executed_fp_type(call->return_type)) return false;
        for (j = 0U; j < call->parameter_count; ++j)
            if (loongarch64_executed_fp_type(call->parameter_types[j]))
                return false;
    }
    for (i = 0U; i < fn->value_count; ++i)
        if (loongarch64_executed_fp_type(fn->values[i].type)) return false;
    for (i = 0U; i < fn->instruction_count; ++i) {
        const MinicCoreInstruction *ins = &fn->instructions[i];
        if (loongarch64_executed_fp_type(ins->type)) return false;
        if ((ins->kind == MINIC_CORE_INSTRUCTION_CALL ||
             ins->kind == MINIC_CORE_INSTRUCTION_INDIRECT_CALL) &&
            minic_type_is_record(ins->type) &&
            minic_multi_fp_return_record(program, target, ins->type,
                                         &count, &width)) return false;
    }
    return true;
}

/* LoongArch64 owns every LP64D frame/ABI register action. Common code owns
 * the function envelope, checked planning, cleanup and ELF serialization. */
static bool loongarch64_emit_prologue(
    FILE *out, const MinicDirectLP64BlockContext *ctx) {
    static const char *const registers[8] = {
        "$a0", "$a1", "$a2", "$a3", "$a4", "$a5", "$a6", "$a7"
    };
    const MinicCoreFunction *fn = ctx->fn;
    const char *name = ctx->name;
    const size_t frame = ctx->frame;
    const size_t save_offset = ctx->save_offset;
    const bool has_calls = ctx->has_direct_calls;
    const bool dynamic_frame = ctx->dynamic_frame;
    size_t i;
    if (ctx->frame_alignment > 16U) {
        /* Entry $sp is only 16-aligned. Reserve worst-case alignment slack,
         * preserve the exact entry $sp, and round a stable callee-saved $s0
         * frame base UP. All object slots are aligned relative to $s0;
         * outgoing call stack space continues to use the 16-aligned $sp. */
        if (!dynamic_frame || ctx->saved_entry_sp_offset == SIZE_MAX ||
            fputs("  move $t0, $sp\n", out) == EOF ||
            !minic_loongarch64_emit_sp_adjust(out, frame, false) ||
            fprintf(out, "  move $t1, $s0\n"
                         "  addi.d $s0, $sp, %zu\n"
                         "  li.d $t3, -%zu\n"
                         "  and $s0, $s0, $t3\n",
                    ctx->frame_alignment - 1U,
                    ctx->frame_alignment) < 0 ||
            !minic_loongarch64_emit_frame_mem(
                out, "st.d", "$t0", ctx->saved_entry_sp_offset, true) ||
            !minic_loongarch64_emit_frame_mem(
                out, "st.d", "$t1",
                save_offset + (has_calls ? 8U : 0U), true))
            goto fail;
    } else if (!minic_loongarch64_emit_sp_adjust(out, frame, false) ||
               (dynamic_frame &&
                (!minic_loongarch64_emit_frame_mem(
                     out, "st.d", "$s0",
                     save_offset + (has_calls ? 8U : 0U), false) ||
                 fputs("  move $s0, $sp\n", out) == EOF)))
        goto fail;
    if (has_calls &&
        !minic_loongarch64_emit_frame_mem(
            out, "st.d", "$ra", save_offset, dynamic_frame))
        goto fail;
    if (ctx->has_indirect_return) {
        /* LP64D uses $a0 as a hidden first GAR, unlike AAPCS64 x8.
         * A nested BL may destroy $a0, so pin the caller-owned buffer
         * address in our stable local frame before any normal instruction. */
        if (!minic_loongarch64_emit_frame_mem(
                out, "st.d", "$a0", ctx->indirect_return_offset,
                dynamic_frame))
            goto fail;
    }
    if (ctx->va_count != 0U) {
        for (i = 0U; i < 8U; ++i)
            if (!minic_loongarch64_emit_frame_mem(
                    out, "st.d", registers[i],
                    ctx->va_base + i * 8U, dynamic_frame))
                return minic_multi_direct_machine_reject(
                    fn, "loongarch64", name, "variadic_save",
                    SIZE_MAX, SIZE_MAX, __LINE__);
    }
    return true;
fail:
    return minic_multi_direct_machine_reject(
        fn, "loongarch64", name, "frame_finalization",
        SIZE_MAX, SIZE_MAX, __LINE__);
}

static bool loongarch64_emit_function(
    FILE *out, const MinicC0Program *program,
    const MinicTargetInfo *target, const MinicFunction *decl,
    const MinicCoreFunction *fn, const MinicCoreFunctionFacts *facts,
    const MinicCoreObjectStoragePlan *storage, size_t id,
    bool function_sections) {
    MinicDirectLP64AbiPolicy abi = loongarch64_abi_policy;
    const MinicDirectLP64FunctionEmitter emitter = {
        "loongarch64", &abi,
        minic_multi_indirect_record, minic_multi_indirect_record,
        "@progbits", "@function", "@init_array", "quad",
        loongarch64_emit_prologue, minic_loongarch64_emit_core_blocks,
        minic_loongarch64_qualified_opaque_asm
    };
    char *generated = NULL;
    size_t generated_length = 0U;
    char *single_use_generated = NULL;
    size_t single_use_generated_length = 0U;
    size_t *uses = NULL;
    FILE *temporary;
    FILE *single_use_output;
    bool success;
    if (target == NULL || fn == NULL) return false;
    if (target->loongarch64_soft_float_abi) {
        if (!loongarch64_lp64s_executable_supported(program, target, fn))
            return minic_multi_direct_machine_reject(
                fn, "loongarch64", fn->name,
                "lp64s-float-codegen-not-supported", SIZE_MAX, SIZE_MAX,
                __LINE__);
        abi.name = "loongarch64-lp64s";
        abi.fp_argument_registers = 0U;
    }
    minic_loongarch64_memory_helpers_begin(id);
    temporary = open_memstream(&generated, &generated_length);
    if (temporary == NULL) return false;
    success = minic_multi_direct_lp64_emit_function(
        temporary, program, target, decl, fn, facts, storage,
        id, function_sections, &emitter);
    if (fclose(temporary) != 0) success = false;
    if (success && fn->value_count != 0U &&
        fn->value_count <= SIZE_MAX / sizeof(*uses)) {
        uses = malloc(fn->value_count * sizeof(*uses));
        if (uses != NULL && !minic_core_function_value_use_counts(
                fn, uses, fn->value_count)) {
            free(uses);
            uses = NULL;
        }
    }
    single_use_output = success
        ? open_memstream(&single_use_generated, &single_use_generated_length)
        : NULL;
    if (success && single_use_output == NULL) success = false;
    if (success)
        success = loongarch64_forward_single_use_spills(
            single_use_output, generated, generated_length,
            uses, fn->value_count);
    if (single_use_output != NULL && fclose(single_use_output) != 0)
        success = false;
    if (success)
        success = loongarch64_emit_compact_assembly(
                      out, single_use_generated,
                      single_use_generated_length) &&
                  minic_loongarch64_memory_helpers_end(out);
    free(generated);
    free(single_use_generated);
    free(uses);
    return success;
}

bool minic_loongarch64_write_c0_program_with_core_functions(
    const char *path, const MinicC0Program *program,
    const MinicTargetInfo *target, const MinicCoreFunction *core_functions,
    size_t core_function_count, bool function_sections,
    MinicDiagnostic *diagnostic) {
    static const MinicMultiDirectProgramConfig config = {
        MINIC_TARGET_ARCH_LOONGARCH64, "loongarch64", "LoongArch64",
        ".section .note.GNU-stack,\"\",@progbits\n",
        "@function", "@object",
        loongarch64_emit_function
    };
    return minic_multi_direct_write_program(
        path, program, target, core_functions, core_function_count,
        function_sections, &config, diagnostic);
}
