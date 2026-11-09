#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

extern volatile unsigned long p49_head_global;
extern unsigned long p49_head_entry(unsigned long);
#ifdef P49_POINTER_RETURN
extern volatile unsigned long *p49_head_helper(volatile unsigned long *);
#else
extern unsigned long p49_head_helper(volatile unsigned long *);
#endif

typedef unsigned long (*P49Entry)(unsigned long);

int main(void) {
    enum { copied_code = 256 };
    const uintptr_t entry = (uintptr_t)(void *)p49_head_entry;
    const uintptr_t helper = (uintptr_t)(void *)p49_head_helper;
    const uintptr_t data = (uintptr_t)(const void *)&p49_head_global;
    uintptr_t begin = entry < helper ? entry : helper;
    uintptr_t end = entry > helper ? entry : helper;
    const unsigned long initial = 0x2211UL, value = 0x45fa72UL;
    unsigned char *image;
    unsigned long cloned_value = 0UL, actual;
    uintptr_t destination;
    P49Entry cloned_entry;
    int ok;
    if (data < begin) begin = data;
    end += copied_code;
    if (data + sizeof(initial) > end) end = data + sizeof(initial);
    if (end <= begin || end - begin > (4U << 20)) return 3;
    image = mmap(NULL, end - begin, PROT_READ | PROT_WRITE | PROT_EXEC,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (image == MAP_FAILED) return 4;
    memcpy(image + (entry - begin), (void *)entry, copied_code);
    memcpy(image + (helper - begin), (void *)helper, copied_code);
    memcpy(image + (data - begin), &initial, sizeof(initial));
    p49_head_global = initial;
    destination = (uintptr_t)(image + (entry - begin));
    memcpy(&cloned_entry, &destination, sizeof(cloned_entry));
    actual = cloned_entry(value);
    memcpy(&cloned_value, image + (data - begin), sizeof(cloned_value));
    ok = actual == data && cloned_value == value &&
         p49_head_global == initial;
    printf("P49_HEAD_CROSSCALL=%s returned=0x%lx expected_vma=0x%lx "
           "cloned=0x%lx original=0x%lx\n",
           ok ? "PASS" : "FAIL", actual, (unsigned long)data,
           cloned_value, p49_head_global);
    return munmap(image, end - begin) == 0 && ok ? 0 : 42;
}
