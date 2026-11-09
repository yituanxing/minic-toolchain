/* An ACLE declaration is not authority to invent float16 ABI or arithmetic. */
typedef __Float16x4_t f16x4;
f16x4 bad_float16(f16x4 a) { return a; }
