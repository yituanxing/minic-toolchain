#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

struct P46Pair { unsigned long lo, hi; };
extern struct P46Pair x86_p46_head_src;
extern struct P46Pair x86_p46_head_dst;
extern void x86_p46_head_copy_direct(void);
extern void x86_p46_head_copy_cast(void);
extern void x86_p46_head_copy_load(void);

static int check_displaced(void (*fn)(void)) {
    const uintptr_t code = (uintptr_t)(void *)fn;
    const uintptr_t src = (uintptr_t)&x86_p46_head_src;
    const uintptr_t dst = (uintptr_t)&x86_p46_head_dst;
    const uintptr_t begin = code < src ? (code < dst ? code : dst)
                                       : (src < dst ? src : dst);
    const uintptr_t end_code = code + 1024U;
    const uintptr_t end_src = src + sizeof(struct P46Pair);
    const uintptr_t end_dst = dst + sizeof(struct P46Pair);
    const uintptr_t end1 = end_code > end_src ? end_code : end_src;
    const uintptr_t end = end1 > end_dst ? end1 : end_dst;
    const size_t span = (size_t)(end - begin);
    unsigned char *image;
    uintptr_t offset_code;
    void (*displaced)(void);
    struct P46Pair cloned_destination;
    const struct P46Pair known_source = {0x6137acUL, 0xf31b42UL};
    const struct P46Pair original_destination = {0x765432UL, 0xabcdefUL};
    int result;

    if (span > (2U << 20)) return 0;
    image = mmap(NULL, span, PROT_READ | PROT_WRITE | PROT_EXEC,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (image == MAP_FAILED) return 0;
    memcpy(image + (code - begin), (const void *)code, 1024U);
    memcpy(image + (src - begin), &known_source, sizeof(known_source));
    memcpy(image + (dst - begin), &original_destination,
           sizeof(original_destination));

    x86_p46_head_dst = original_destination;
    offset_code = (uintptr_t)(image + (code - begin));
    memcpy(&displaced, &offset_code, sizeof(displaced));
    displaced();
    memcpy(&cloned_destination, image + (dst - begin),
           sizeof(cloned_destination));
    result = cloned_destination.lo == known_source.lo &&
             cloned_destination.hi == known_source.hi &&
             x86_p46_head_dst.lo == original_destination.lo &&
             x86_p46_head_dst.hi == original_destination.hi;
    if (munmap(image, span) != 0) return 0;
    return result;
}

int main(void) {
    int direct = check_displaced(x86_p46_head_copy_direct);
    int cast = check_displaced(x86_p46_head_copy_cast);
    int load = check_displaced(x86_p46_head_copy_load);
    printf("HEAD_AGGREGATE displace_direct=%d displace_cast=%d displace_load=%d\n",
           direct, cast, load);
    return direct && cast && load ? 0 : 42;
}
