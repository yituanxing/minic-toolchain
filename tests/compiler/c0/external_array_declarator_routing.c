typedef unsigned char u8;

struct cpu_operations {
    int state;
};

const struct cpu_operations *cpu_ops[4] __attribute__((__section__(".data..ro_after_init")));

unsigned long empty_zero_page[8] __attribute__((__section__(".bss..page_aligned")))
__attribute__((__aligned__(64)));

u8 purgatory_sha256_digest[32] __attribute__((__section__(".kexec-purgatory")));
u8 purgatory_sha_regions[2][4] __attribute__((__section__(".kexec-purgatory")));

unsigned long initialized_map[4]
    __attribute__((__section__(".data..ro_after_init"))) = {1, 2, 3, 4};

extern unsigned long completed_tentative[];
unsigned long completed_tentative[4];

extern unsigned long completed_definition[];
unsigned long completed_definition[4] = {11, 12, 13, 14};
