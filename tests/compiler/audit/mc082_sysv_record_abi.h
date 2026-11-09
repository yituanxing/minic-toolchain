#ifndef MC082_SYSV_RECORD_ABI_H
#define MC082_SYSV_RECORD_ABI_H

/* All scalar leaves are byte-aligned INTEGER-class fields. The structs'
 * true sizes span every SysV one-/two-eightbyte partial-tail boundary. */
#define MC082_SIZES(X) \
    X(1) X(2) X(3) X(4) X(5) X(6) X(7) X(8) \
    X(9) X(10) X(11) X(12) X(13) X(14) X(15) X(16)

#define MC082_RECORD(N) \
    struct MC082_R##N { unsigned char bytes[N]; }; \
    struct MC082_R##N mc082_make_##N(unsigned char seed); \
    unsigned int mc082_check_##N(struct MC082_R##N value);
MC082_SIZES(MC082_RECORD)
#undef MC082_RECORD

/* packed with a naturally aligned u32 leaf still has INTEGER class. */
struct __attribute__((packed)) MC082_Packed5 {
    unsigned int word;
    unsigned char tail;
};
struct MC082_Packed5 mc082_make_packed5(unsigned int value);
unsigned int mc082_check_packed5(struct MC082_Packed5 value);

/* Explicit eightbyte alignment can raise sizeof(3-byte data) to eight. */
struct __attribute__((aligned(8))) MC082_Aligned8 {
    unsigned char bytes[3];
};
struct MC082_Aligned8 mc082_make_aligned8(unsigned char seed);
unsigned int mc082_check_aligned8(struct MC082_Aligned8 value);

/* Internal and tail padding are part of sizeof but never authorize an
 * eightbyte read past that size; both remain INTEGER-class aggregates. */
struct MC082_Padded8 { unsigned char tag; unsigned int word; };
struct MC082_Padded16 { unsigned char tag; unsigned long long word; };
struct MC082_Padded8 mc082_make_padded8(unsigned int value);
unsigned int mc082_check_padded8(struct MC082_Padded8 value);
struct MC082_Padded16 mc082_make_padded16(unsigned long long value);
unsigned int mc082_check_padded16(struct MC082_Padded16 value);

/* A one-GP value after six integer arguments and indivisible two-GP values
 * after five arguments must take SysV stack fallback, including odd tails. */
unsigned int mc082_stack3(unsigned long, unsigned long, unsigned long,
                          unsigned long, unsigned long, unsigned long,
                          struct MC082_R3);
unsigned int mc082_stack9(unsigned long, unsigned long, unsigned long,
                          unsigned long, unsigned long, struct MC082_R9);
unsigned int mc082_stack15(unsigned long, unsigned long, unsigned long,
                           unsigned long, unsigned long, struct MC082_R15);

#endif
