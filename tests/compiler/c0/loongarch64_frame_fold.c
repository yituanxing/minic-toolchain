/* The native path may use a known frame-object offset directly. An escaped
 * pointer and a packed external pointer must retain their distinct paths. */
struct S { long a, b; int flag; };
struct __attribute__((packed)) Packed { char tag; int value; };
volatile long output;
extern void escape(int *);

long work(long a, long b) {
    struct S x;
    x.a = a;
    x.b = b;
    x.flag = 3;
    x.a += x.b;
    output = x.a;
    return x.a * x.flag + x.b;
}

int alias_escape(int x) {
    int local = x;
    escape(&local);
    return local;
}

int packed_read(struct Packed *p) {
    return p->value;
}

/* Keep the frame genuinely large: the volatile byte object is observable,
 * while incoming scalar/SSA transport remains naturally aligned above it. */
unsigned long large_frame_touch(unsigned long x) {
    volatile unsigned char bytes[4096];
    bytes[4032] = (unsigned char)x;
    return (unsigned long)bytes[4032] + x;
}
