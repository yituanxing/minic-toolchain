extern char __attribute__((__section__(".probe.data"))) placed_data[];
char placed_data[] = "x";

extern unsigned long suffix_first __attribute__((__section__(".probe.suffix.first"))),
    suffix_second __attribute__((__section__(".probe.suffix.second")));
unsigned long suffix_first = 7;
unsigned long suffix_second = 9;

extern char suffix_array[] __attribute__((__section__(".probe.suffix.array")));
char suffix_array[] = "z";

struct prefix_section_shape {
    unsigned long value;
};

extern __attribute__((section(".probe.prefix.record" "")))
__typeof__(struct prefix_section_shape) prefix_section_record;

extern __attribute__((section(".probe.prefix.concat" "")))
__typeof__(unsigned long) prefix_section_scalar;
unsigned long prefix_section_scalar = 11;

void __attribute__((__section__(".probe.text"))) placed_function(void);

void placed_function(void) {
}

/* Linux init/main.i shape: symbol section before the return type, optimization
 * metadata beside it, and function metadata after the declarator. */
__attribute__((__section__(".probe.prefix.text"))) __attribute__((__cold__))
void *prefix_placed_function(int nid, unsigned long size, unsigned long mask)
    __attribute__((__alloc_size__(2))) __attribute__((__malloc__));

void *prefix_placed_function(int nid, unsigned long size, unsigned long mask) {
    if (nid || size || mask) {
        return (void *)0;
    }
    return (void *)0;
}

int main(void) {
    placed_function();
    return placed_data[0] == 'x' && suffix_first == 7 && suffix_second == 9 &&
                   suffix_array[0] == 'z' && prefix_section_scalar == 11
               ? 0
               : 1;
}
