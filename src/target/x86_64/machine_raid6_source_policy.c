#include "target/x86_64/asm_contract.h"
#include <ctype.h>
#include <string.h>

/* Pure finite RAID6 source-ISA grammar, without FILE* text emission. */
typedef enum Raid6OperandKind {
    RAID6_BAD = 0,
    RAID6_MEM,
    RAID6_XMM,
    RAID6_YMM,
    RAID6_ZMM,
    RAID6_K,
    RAID6_IMM4
} Raid6OperandKind;

typedef struct Raid6Operand {
    Raid6OperandKind kind;
    unsigned int number;
} Raid6Operand;

static bool raid6_parse_operand(const char *p, size_t n, size_t count,
                                Raid6Operand *operand, unsigned int *seen) {
    unsigned int reg = 0U;
    size_t i = 0U;
    Raid6OperandKind kind;
    if (!p || !operand || n == 0U) return false;
    if (n == 2U && p[0] == '$' && p[1] == '4') {
        operand->kind = RAID6_IMM4;
        operand->number = 4U;
        return true;
    }
    if (p[i++] != '%') return false;
    if (i < n && p[i] == '%') ++i; /* GNU extended `%%`, basic `%` */
    if (i == 1U && n == 2U && isdigit((unsigned char)p[1])) {
        const unsigned int index = (unsigned int)(p[1] - '0');
        if (index >= count || index >= 8U) return false;
        operand->kind = RAID6_MEM;
        operand->number = index;
        *seen |= 1U << index;
        return true;
    }
    if (i + 3U <= n && memcmp(p + i, "xmm", 3U) == 0) {
        kind = RAID6_XMM;
        i += 3U;
    } else if (i + 3U <= n && memcmp(p + i, "ymm", 3U) == 0) {
        kind = RAID6_YMM;
        i += 3U;
    } else if (i + 3U <= n &&
               (memcmp(p + i, "zmm", 3U) == 0 ||
                memcmp(p + i, "Zmm", 3U) == 0)) {
        kind = RAID6_ZMM;
        i += 3U;
    } else if (i < n && p[i] == 'k') {
        kind = RAID6_K;
        ++i;
    } else return false;
    if (i == n || n - i > 2U) return false;
    for (; i < n; ++i) {
        if (!isdigit((unsigned char)p[i])) return false;
        reg = reg * 10U + (unsigned int)(p[i] - '0');
    }
    if ((kind == RAID6_K && (reg < 1U || reg > 4U)) ||
        (kind != RAID6_K && reg > 15U)) return false;
    operand->kind = kind;
    operand->number = reg;
    return true;
}

static bool raid6_is(const char *name, const char *expected) {
    return strcmp(name, expected) == 0;
}

static bool raid6_opcodes_valid(const char *name, const Raid6Operand *a,
                                size_t n) {
    const Raid6OperandKind x = n ? a[0].kind : RAID6_BAD;
    const Raid6OperandKind y = n > 1U ? a[1].kind : RAID6_BAD;
    const Raid6OperandKind z = n > 2U ? a[2].kind : RAID6_BAD;
    if (raid6_is(name, "prefetchnta")) return n == 1U && x == RAID6_MEM;
    if (raid6_is(name, "movdqa") || raid6_is(name, "movntdq"))
        return n == 2U &&
            ((x == RAID6_XMM && y == RAID6_XMM) ||
             (x == RAID6_MEM && y == RAID6_XMM &&
              !raid6_is(name, "movntdq")) ||
             (x == RAID6_XMM && y == RAID6_MEM));
    if (raid6_is(name, "vmovdqa") || raid6_is(name, "vmovdqa64") ||
        raid6_is(name, "vmovntdq") || raid6_is(name, "vmovapd")) {
        const Raid6OperandKind width = raid6_is(name, "vmovdqa64")
            ? RAID6_ZMM : raid6_is(name, "vmovdqa")
            ? RAID6_YMM : raid6_is(name, "vmovntdq")
            ? (x == RAID6_ZMM ? RAID6_ZMM : RAID6_YMM) : x;
        if (width != RAID6_YMM && width != RAID6_ZMM) return false;
        return n == 2U &&
            ((x == width && y == width) ||
             (x == RAID6_MEM && y == width &&
              !raid6_is(name, "vmovntdq")) ||
             (x == width && y == RAID6_MEM &&
              !raid6_is(name, "vmovapd")));
    }
    if (raid6_is(name, "pxor") || raid6_is(name, "pand") ||
        raid6_is(name, "paddb") || raid6_is(name, "pcmpgtb") ||
        raid6_is(name, "pshufb"))
        return n == 2U && y == RAID6_XMM &&
            (x == RAID6_XMM ||
             (x == RAID6_MEM && raid6_is(name, "pxor")));
    if (raid6_is(name, "psraw"))
        return n == 2U && x == RAID6_IMM4 && y == RAID6_XMM;
    if (raid6_is(name, "vbroadcasti128"))
        return n == 2U && x == RAID6_MEM && y == RAID6_YMM;
    if (raid6_is(name, "vbroadcasti64x2"))
        return n == 2U && x == RAID6_MEM && y == RAID6_ZMM;
    if (raid6_is(name, "vpbroadcastb"))
        return n == 2U && x == RAID6_MEM &&
            (y == RAID6_YMM || y == RAID6_ZMM);
    if (raid6_is(name, "vpmovm2b"))
        return n == 2U && x == RAID6_K && y == RAID6_ZMM;
    if (raid6_is(name, "vpsraw"))
        return n == 3U && x == RAID6_IMM4 &&
            (y == RAID6_YMM || y == RAID6_ZMM) && z == y;
    if (raid6_is(name, "vpcmpgtb"))
        return n == 3U &&
            ((x == RAID6_YMM && y == RAID6_YMM && z == RAID6_YMM) ||
             (x == RAID6_ZMM && y == RAID6_ZMM && z == RAID6_K));
    if (raid6_is(name, "vpxor") || raid6_is(name, "vpxorq") ||
        raid6_is(name, "vpand") || raid6_is(name, "vpandq") ||
        raid6_is(name, "vpaddb") || raid6_is(name, "vpshufb")) {
        const Raid6OperandKind width =
            raid6_is(name, "vpxorq") || raid6_is(name, "vpandq")
                ? RAID6_ZMM : z;
        if (n != 3U || (width != RAID6_YMM && width != RAID6_ZMM) ||
            y != width || z != width) return false;
        return x == width || (x == RAID6_MEM &&
            (raid6_is(name, "vpxor") || raid6_is(name, "vpxorq")));
    }
    return false;
}

bool minic_x86_64_raid6_simd_template(const char *text, size_t length,
                                      size_t memory_count) {
    size_t p = 0U, lines = 0U;
    unsigned int seen = 0U;
    if (!text || length == 0U || length > 4096U || memory_count > 8U)
        return false;
    while (p < length) {
        char opcode[32];
        Raid6Operand operands[3];
        size_t begin, n = 0U, op_len, count = 0U;
        while (p < length && (text[p] == ' ' || text[p] == '\t' ||
                              text[p] == '\r' || text[p] == '\n')) ++p;
        if (p == length) break;
        if (++lines > 64U) return false;
        begin = p;
        while (p < length && ((text[p] >= 'a' && text[p] <= 'z') ||
                              (text[p] >= '0' && text[p] <= '9'))) ++p;
        op_len = p - begin;
        if (!op_len || op_len >= sizeof(opcode)) return false;
        memcpy(opcode, text + begin, op_len);
        opcode[op_len] = '\0';
        if (p == length || (text[p] != ' ' && text[p] != '\t')) return false;
        while (p < length && (text[p] == ' ' || text[p] == '\t')) ++p;
        while (p < length && text[p] != '\r' && text[p] != '\n') {
            while (p < length && (text[p] == ' ' || text[p] == '\t')) ++p;
            begin = p;
            while (p < length && text[p] != ',' && text[p] != '\r' &&
                   text[p] != '\n' && text[p] != ' ' && text[p] != '\t')
                ++p;
            n = p - begin;
            if (count == 3U || !raid6_parse_operand(text + begin, n,
                       memory_count, &operands[count], &seen)) return false;
            ++count;
            while (p < length && (text[p] == ' ' || text[p] == '\t')) ++p;
            if (p == length || text[p] == '\r' || text[p] == '\n') break;
            if (text[p++] != ',') return false;
            if (p == length) return false;
        }
        if (!raid6_opcodes_valid(opcode, operands, count)) return false;
    }
    return lines != 0U && seen == ((1U << memory_count) - 1U);
}
