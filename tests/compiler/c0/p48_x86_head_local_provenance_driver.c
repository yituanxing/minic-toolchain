#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

extern volatile unsigned long p48_head_cells[3];
extern volatile unsigned long p48_head_other;

typedef unsigned long (*P48HeadFunction)(unsigned long, unsigned long);
extern unsigned long p48_head_baseline(unsigned long, unsigned long);
extern unsigned long p48_head_plain_local(unsigned long, unsigned long);
extern unsigned long p48_head_local(unsigned long, unsigned long);
extern unsigned long p48_head_roundtrip(unsigned long, unsigned long);
extern unsigned long p48_head_same_origin_branch(unsigned long, unsigned long);
extern unsigned long p48_head_record_field(unsigned long, unsigned long);
extern unsigned long p48_head_overflow_address(unsigned long, unsigned long);
extern unsigned long p48_normal_local(unsigned long, unsigned long);

/* Match the P46 displaced-native oracle: one physical image of code and the
 * entire head-data symbol region, relocated by the SAME delta.  A dereference
 * through an absolute linked VMA writes the original object instead of the
 * cloned physical object, and therefore fails even if ordinary native
 * execution at the linked address would have passed. */
static int p48_check_displaced(const char *name, P48HeadFunction fn,
                              unsigned long value, unsigned long selector,
                              int written_index, int numeric_index) {
    enum { P48_COPIED_FUNCTION_BYTES = 1024 };
    const unsigned long initial_cells[3] = {
        0x128c31UL, 0x637a45UL, 0x9abc51UL
    };
    const unsigned long initial_other = 0xeeca72UL;
    const uintptr_t code = (uintptr_t)(void *)fn;
    const uintptr_t cells = (uintptr_t)(const void *)p48_head_cells;
    const uintptr_t other = (uintptr_t)(const void *)&p48_head_other;
    const uintptr_t begin = code < cells ? (code < other ? code : other)
                                         : (cells < other ? cells : other);
    const uintptr_t code_end = code + P48_COPIED_FUNCTION_BYTES;
    const uintptr_t cells_end = cells + sizeof(initial_cells);
    const uintptr_t other_end = other + sizeof(initial_other);
    const uintptr_t end_a = code_end > cells_end ? code_end : cells_end;
    const uintptr_t end = end_a > other_end ? end_a : other_end;
    const size_t span = (size_t)(end - begin);
    unsigned char *image;
    unsigned long cloned_cells[3] = {0UL, 0UL, 0UL};
    unsigned long cloned_other = 0UL;
    uintptr_t relocated_address;
    P48HeadFunction relocated;
    unsigned long actual;
    uintptr_t expected_numeric;
    int ok = 1;
    size_t i;

    if (span == 0U || span > (4U << 20)) {
        fprintf(stderr, "P48_DISPLACED=FAIL %s unsupported_span=%zu\n",
                name, span);
        return 0;
    }
    image = mmap(NULL, span, PROT_READ | PROT_WRITE | PROT_EXEC,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (image == MAP_FAILED) {
        fprintf(stderr, "P48_DISPLACED=FAIL %s mmap\n", name);
        return 0;
    }
    memcpy(image + (code - begin), (const void *)(uintptr_t)code,
           P48_COPIED_FUNCTION_BYTES);
    memcpy(image + (cells - begin), initial_cells, sizeof(initial_cells));
    memcpy(image + (other - begin), &initial_other, sizeof(initial_other));
    for (i = 0U; i < 3U; ++i) p48_head_cells[i] = initial_cells[i];
    p48_head_other = initial_other;

    relocated_address = (uintptr_t)(image + (code - begin));
    memcpy(&relocated, &relocated_address, sizeof(relocated));
    actual = relocated(value, selector);
    memcpy(cloned_cells, image + (cells - begin), sizeof(cloned_cells));
    memcpy(&cloned_other, image + (other - begin), sizeof(cloned_other));

    expected_numeric = numeric_index == 3
        ? other : cells + (size_t)numeric_index * sizeof(unsigned long);
    if (actual != (unsigned long)expected_numeric) ok = 0;
    for (i = 0U; i < 3U; ++i) {
        if (cloned_cells[i] !=
            (written_index == (int)i ? value : initial_cells[i]) ||
            p48_head_cells[i] != initial_cells[i])
            ok = 0;
    }
    if (cloned_other != (written_index == 3 ? value : initial_other) ||
        p48_head_other != initial_other) ok = 0;
    if (!ok)
        fprintf(stderr,
                "P48_DISPLACED=FAIL %s returned=0x%lx expected=0x%lx "
                "clone0=0x%lx clone1=0x%lx clone2=0x%lx other=0x%lx\n",
                name, actual, (unsigned long)expected_numeric,
                cloned_cells[0], cloned_cells[1], cloned_cells[2],
                cloned_other);
    if (munmap(image, span) != 0) ok = 0;
    return ok;
}

static int p48_check_normal(void) {
    unsigned long expected = (unsigned long)&p48_head_cells[0];
    const unsigned long value = 0x72ca32UL;
    p48_head_cells[0] = 0x5151UL;
    return p48_normal_local(value, 0UL) == expected &&
           p48_head_cells[0] == value;
}

int main(void) {
#ifdef P48_NATIVE_ONLY
    const int normal = p48_check_normal();
    printf("P48_HEAD_LOCAL pic_normal=%d\n", normal);
    return normal ? 0 : 42;
#else
    /* GCC -O2 is a genuine displaced positive reference for the baseline
     * and plain local.  GNU -O0's volatile local pointer store transports
     * an absolute VMA, so it is NOT a displaced golden for harder cases. */
    const int baseline = p48_check_displaced(
        "baseline", p48_head_baseline, 0x17ae31UL, 0UL, 0, 0);
    const int plain = p48_check_displaced(
        "plain_local", p48_head_plain_local, 0x18a25cUL, 0UL, 1, 1);
    const int normal = p48_check_normal();
#ifdef P48_GNU_GOLDEN
    printf("P48_HEAD_LOCAL gnu_baseline=%d gnu_plain_local=%d normal=%d\n",
           baseline, plain, normal);
    return baseline && plain && normal ? 0 : 42;
#else
    const int local = p48_check_displaced(
        "local", p48_head_local, 0x19be43UL, 0UL, 0, 0);
    const int roundtrip_zero = p48_check_displaced(
        "roundtrip_zero", p48_head_roundtrip, 0x6dabc1UL, 0UL, 0, 0);
    const int roundtrip_one = p48_check_displaced(
        "roundtrip_one", p48_head_roundtrip, 0x6dabc2UL, 1UL, 1, 1);
    const int branch_zero = p48_check_displaced(
        "branch_zero", p48_head_same_origin_branch, 0x71be50UL, 0UL, 0, 0);
    const int branch_one = p48_check_displaced(
        "branch_one", p48_head_same_origin_branch, 0x71be51UL, 1UL, 1, 1);
    const int record = p48_check_displaced(
        "record_field", p48_head_record_field, 0xc1a713UL, 0UL, 2, 2);
    const int overflow = p48_check_displaced(
        "overflow_address", p48_head_overflow_address,
        0x19ab34UL, 0UL, 2, 2);
    printf("P48_HEAD_LOCAL baseline=%d plain=%d local=%d roundtrip0=%d "
           "roundtrip1=%d branch0=%d branch1=%d record=%d overflow=%d "
           "normal=%d\n",
           baseline, plain, local, roundtrip_zero, roundtrip_one,
           branch_zero, branch_one, record, overflow, normal);
    return baseline && plain && local && roundtrip_zero && roundtrip_one &&
           branch_zero && branch_one && record && overflow && normal ? 0 : 42;
#endif
#endif
}
