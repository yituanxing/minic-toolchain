/* Original Linux x86 kernel nofault MOV + GNU asm-goto recovery: the
 * second PREL32 __ex_table field must identify the C fault block, not a
 * synthetic fallthrough. Valid-pointer calls are safe in user mode. */
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
struct __large_struct { unsigned long data[16]; };

#define NOFAULT_LOAD(width) \
    "\n1: mov" width " %[umem],%[output]\n" \
    " .pushsection \"__ex_table\",\"a\"\n" \
    " .balign 4\n" \
    " .long (1b) - .\n" \
    " .long (%l2) - .\n" \
    " .long 3\n" \
    " .popsection\n"

#define NOFAULT_STORE(width) \
    "\n1: mov" width " %0,%1\n" \
    " .pushsection \"__ex_table\",\"a\"\n" \
    " .balign 4\n" \
    " .long (1b) - .\n" \
    " .long (%l2) - .\n" \
    " .long 3\n" \
    " .popsection\n"

#define DEFINE_NOFAULT_GET(suffix, ctype, regconstraint, mnemonic) \
    int linux_nofault_get_##suffix(const void *p, ctype *out) { \
        ctype temp; \
        asm volatile goto(NOFAULT_LOAD(mnemonic) \
            : [output] regconstraint (temp) \
            : [umem] "m" (*(struct __large_struct *)p) : : bad); \
        *out = temp; \
        return 0; \
    bad: return -14; \
    }

#define DEFINE_NOFAULT_PUT(suffix, ctype, regconstraint, mnemonic) \
    int linux_nofault_put_##suffix(void *p, ctype value) { \
        asm goto(NOFAULT_STORE(mnemonic) \
            : : regconstraint (value), \
                "m" (*(struct __large_struct *)p) : : bad); \
        return 0; \
    bad: return -14; \
    }

DEFINE_NOFAULT_GET(b, u8,  "=q", "b")
DEFINE_NOFAULT_GET(w, u16, "=r", "w")
DEFINE_NOFAULT_GET(l, u32, "=r", "l")
DEFINE_NOFAULT_GET(q, u64, "=r", "q")
DEFINE_NOFAULT_PUT(b, u8,  "iq", "b")
DEFINE_NOFAULT_PUT(w, u16, "ir", "w")
DEFINE_NOFAULT_PUT(l, u32, "ir", "l")
DEFINE_NOFAULT_PUT(q, u64, "er", "q")
