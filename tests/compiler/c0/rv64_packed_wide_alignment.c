/* Target-independent Core LOAD/STORE may point at misaligned 16-byte
 * int128/long-double members.  RV64 must split into guarded 8-byte words
 * with bytewise fallback, not assume the physical member's type alignment. */
struct __attribute__((packed)) PackedWide {
    char lead;
    __int128 value;
    long double fp;
};

__int128 packed_wide_read(const struct PackedWide *p) { return p->value; }
void packed_wide_write(struct PackedWide *p, __int128 x) { p->value = x; }
long double packed_long_double_read(const struct PackedWide *p) { return p->fp; }
void packed_long_double_write(struct PackedWide *p, long double x) { p->fp = x; }
