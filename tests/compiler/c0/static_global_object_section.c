static int __attribute__((__section__(".data.static.init"))) section_initialized = 7;
static int __attribute__((section(".data.static.zero"))) section_zero;
static void *__attribute__((__used__))
__attribute__((__section__(".discard.addressable"))) addressable_shape = (void *)0;

int read_static_global_sections(void) {
    return section_initialized + section_zero + (addressable_shape == (void *)0);
}
