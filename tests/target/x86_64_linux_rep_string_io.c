/* Original Linux lib/iomap.c port-string I/O hardware constraints.  Never
 * execute these functions in user space: IN/OUT is privileged hardware I/O. */
typedef unsigned short u16;
typedef unsigned long usize;

#define DEFINE_OUT(name, suffix)                                           \
__attribute__((noinline)) usize name(u16 port, const void *src, usize n) { \
    const void *cursor = src;                                              \
    asm volatile("rep; outs" suffix : "+S"(cursor), "+c"(n) : "d"(port)   \
        : "memory");                                                      \
    return (usize)cursor + n;                                              \
}

#define DEFINE_IN(name, suffix)                                           \
__attribute__((noinline)) usize name(u16 port, void *dst, usize n) {      \
    void *cursor = dst;                                                     \
    asm volatile("rep; ins" suffix : "+D"(cursor), "+c"(n) : "d"(port)    \
        : "memory");                                                      \
    return (usize)cursor + n;                                              \
}

DEFINE_OUT(x86_rep_outsb,"b")
DEFINE_OUT(x86_rep_outsw,"w")
DEFINE_OUT(x86_rep_outsl,"l")
DEFINE_IN(x86_rep_insb,"b")
DEFINE_IN(x86_rep_insw,"w")
DEFINE_IN(x86_rep_insl,"l")

#ifdef MINIC_NEGATIVE_TEMPLATE
void invalid_template(u16 p, const void *a, usize n) {
    asm volatile("rep; outsb; cli" : "+S"(a), "+c"(n) : "d"(p) : "memory");
}
#endif
#ifdef MINIC_NEGATIVE_CONSTRAINT
void invalid_constraint(u16 p, const void *a, usize n) {
    asm volatile("rep; outsb" : "+D"(a), "+c"(n) : "d"(p) : "memory");
}
#endif
#ifdef MINIC_NEGATIVE_PORT
void invalid_port(u16 p, const void *a, usize n) {
    asm volatile("rep; outsb" : "+S"(a), "+c"(n) : "r"(p) : "memory");
}
#endif
#ifdef MINIC_NEGATIVE_MEMORY
void invalid_memory(u16 p, const void *a, usize n) {
    asm volatile("rep; outsb" : "+S"(a), "+c"(n) : "d"(p));
}
#endif
