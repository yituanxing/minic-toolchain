#include "target/loongarch64/machine_text_opt.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Native post-emission transformations operate on complete, compiler-owned
 * machine text only. Core analysis and ABI decisions are not made here. */

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

typedef bool (*LoongMachineLineWriter)(void *context,
                                      const char *line, size_t length);

static bool loongarch64_write_file_line(void *context,
                                       const char *line, size_t length) {
    FILE *out = (FILE *)context;
    return fwrite(line, 1U, length, out) == length;
}

/* The SSA forwarder feeds complete lines to a target-owned consumer. The
 * public legacy API uses a FILE writer; codegen streams into compaction so
 * there is no second whole-function assembly buffer or reparsing boundary. */
static bool loongarch64_forward_to_writer(
    LoongMachineLineWriter write, void *context,
    const char *buffer, size_t length, const size_t *uses,
    size_t value_count) {
    const char *line = buffer, *end = buffer + length;
    const char *previous = NULL;
    size_t previous_length = 0U;
    bool inline_asm = false;
    if (write == NULL || buffer == NULL) return false;
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
                    if (!write(context, line, n)) return false;
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
                    static const char replacement_prefix[] = "  beqz $t0, ";
                    const size_t target_length = m - sizeof("  b ");
                    const size_t prefix_length = sizeof(replacement_prefix) - 1U;
                    char *replacement;
                    bool written;
                    if (target_length >= SIZE_MAX - prefix_length - 2U)
                        return false;
                    replacement = malloc(prefix_length + target_length + 2U);
                    if (replacement == NULL) return false;
                    memcpy(replacement, replacement_prefix, prefix_length);
                    memcpy(replacement + prefix_length,
                           marker + sizeof("  b ") - 1U, target_length);
                    replacement[prefix_length + target_length] = '\n';
                    replacement[prefix_length + target_length + 1U] = '\0';
                    written = write(context, replacement,
                                    prefix_length + target_length + 1U);
                    free(replacement);
                    if (!written) return false;
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
                    !write(context, "  slli.w $t0, $t0, 0\n",
                           sizeof("  slli.w $t0, $t0, 0\n") - 1U))
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
                    char replacement[48];
                    int written = snprintf(replacement, sizeof(replacement),
                        direct_forward == 1U
                            ? "  move $t0, $a%u\n"
                            : "  slli.w $t0, $a%u, 0\n", parameter);
                    if (written < 0 || (size_t)written >= sizeof(replacement) ||
                        !write(context, replacement, (size_t)written))
                        return false;
                    line = load + loongarch64_machine_line_length(load, end);
                    previous = NULL;
                    continue;
                }
            }
        }
        if (!write(context, line, n)) return false;
        previous = line;
        previous_length = n;
        line += n;
    }
    return true;
}

bool minic_loongarch64_forward_single_use_spills(FILE *out,
    const char *buffer, size_t length, const size_t *uses,
    size_t value_count) {
    return out != NULL && loongarch64_forward_to_writer(
        loongarch64_write_file_line, out, buffer, length,
        uses, value_count);
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

static bool loongarch64_redundant_sign_extend_source(
    const char *line, size_t length) {
    return loongarch64_line_starts(line, length, "  ld.w $t0, ") ||
           loongarch64_line_starts(line, length, "  ldptr.w $t0, ") ||
           loongarch64_line_starts(line, length, "  li.w $t0, ") ||
           loongarch64_line_starts(line, length, "  add.w $t0, ") ||
           loongarch64_line_starts(line, length, "  sub.w $t0, ") ||
           loongarch64_line_starts(line, length, "  mul.w $t0, ");
}

typedef struct LoongMachineTextCompactor {
    FILE *out;
    LoongGPFrameCache cache[9];
    bool inline_asm;
    /* Store semantic machine facts rather than pointers to previous text:
     * the upstream forwarder may supply an ephemeral replacement line. */
    bool previous_frame_store;
    unsigned int previous_reg;
    char previous_base[3];
    size_t previous_offset;
    bool previous_sext32;
} LoongMachineTextCompactor;

static bool loongarch64_compact_line(LoongMachineTextCompactor *compactor,
                                    const char *line, size_t length) {
    LoongGPFrameCache *cache = compactor->cache;
    unsigned int reg = 0U;
    char base[3] = {0};
    size_t offset = 0U;
    bool skip = false;
    static const char zero_shift[] = "  slli.w $t0, $t0, 0\n";
    if (loongarch64_line_starts(line, length,
                               "# MINI_LOONG_SSA_WRITEBACK")) {
        if (compactor->previous_frame_store)
            loongarch64_cache_add(&cache[compactor->previous_reg],
                                  compactor->previous_base,
                                  compactor->previous_offset);
        return true;
    }
    if (loongarch64_line_starts(line, length,
                               "# MINI_LOONG_SSA_PARAMETER"))
        return true;
    if (loongarch64_line_starts(line, length,
                               "# MINI_LOONG_INLINE_ASM_BEGIN")) {
        memset(cache, 0, sizeof(compactor->cache));
        compactor->inline_asm = true;
    } else if (loongarch64_line_starts(line, length,
                                      "# MINI_LOONG_INLINE_ASM_END")) {
        memset(cache, 0, sizeof(compactor->cache));
        compactor->inline_asm = false;
    } else if (compactor->inline_asm) {
        /* GNU/user ASM is opaque to machine optimizers. */
    } else if (loongarch64_parse_frame_gpr(line, "ld.d", &reg,
                                            base, &offset)) {
        skip = loongarch64_cache_has(&cache[reg], base, offset);
        if (!skip) cache[reg].count = 0U;
    } else if (loongarch64_line_starts(line, length, "  st.") ||
               loongarch64_line_starts(line, length, "  stptr.")) {
        if (!loongarch64_invalidate_frame_store(cache, line))
            memset(cache, 0, sizeof(compactor->cache));
    } else if (loongarch64_line_starts(line, length, "  ld.")) {
        char op[24], dest[24];
        if (sscanf(line, "  %23s %23[^, \t\n]", op, dest) == 2 &&
            sscanf(dest, "$t%u", &reg) == 1 && reg <= 8U)
            cache[reg].count = 0U;
        else
            memset(cache, 0, sizeof(compactor->cache));
    } else if (loongarch64_line_starts(line, length, "  ")) {
        char op[24], dest[24];
        if (sscanf(line, "  %23s %23[^, \t\n]", op, dest) != 2 ||
            !loongarch64_pure_scalar_opcode(op))
            memset(cache, 0, sizeof(compactor->cache));
        else if (sscanf(dest, "$t%u", &reg) == 1 && reg <= 8U)
            cache[reg].count = 0U;
        else if (strcmp(dest, "$sp") == 0 || strcmp(dest, "$s0") == 0)
            memset(cache, 0, sizeof(compactor->cache));
    } else {
        memset(cache, 0, sizeof(compactor->cache));
    }
    if (!compactor->inline_asm && compactor->previous_sext32 &&
        length == sizeof(zero_shift) - 1U &&
        memcmp(line, zero_shift, sizeof(zero_shift) - 1U) == 0)
        skip = true;
    if (skip) return true;
    if (fwrite(line, 1U, length, compactor->out) != length) return false;
    compactor->previous_frame_store =
        loongarch64_parse_frame_gpr(line, "st.d", &reg, base, &offset);
    if (compactor->previous_frame_store) {
        compactor->previous_reg = reg;
        memcpy(compactor->previous_base, base, sizeof(base));
        compactor->previous_offset = offset;
    }
    compactor->previous_sext32 =
        loongarch64_redundant_sign_extend_source(line, length);
    return true;
}

static bool loongarch64_compact_writer(void *context,
                                      const char *line, size_t length) {
    return loongarch64_compact_line((LoongMachineTextCompactor *)context,
                                   line, length);
}

bool minic_loongarch64_emit_compact_assembly(FILE *out,
                                              const char *buffer,
                                              size_t buffer_length) {
    LoongMachineTextCompactor compactor = {0};
    const char *line, *end;
    if (out == NULL || buffer == NULL) return false;
    compactor.out = out;
    line = buffer;
    end = buffer + buffer_length;
    while (line < end) {
        size_t length = loongarch64_machine_line_length(line, end);
        if (!loongarch64_compact_line(&compactor, line, length)) return false;
        line += length;
    }
    return true;
}

bool minic_loongarch64_forward_and_compact(FILE *out,
    const char *buffer, size_t length, const size_t *uses,
    size_t value_count) {
    LoongMachineTextCompactor compactor = {0};
    if (out == NULL || buffer == NULL) return false;
    compactor.out = out;
    return loongarch64_forward_to_writer(
        loongarch64_compact_writer, &compactor,
        buffer, length, uses, value_count);
}
