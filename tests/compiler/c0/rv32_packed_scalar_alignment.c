/* GNU packed members are valid C lvalues even when their natural machine
 * width exceeds the enclosing record's alignment.  Scalar loads/stores must
 * never depend on the CPU's optional unaligned access behavior. */
struct __attribute__((packed)) PackedScalars {
    unsigned char prefix;
    short narrow;
    unsigned int word;
    unsigned long long wide;
    double fp;
};

short packed_read_short(const struct PackedScalars *p) { return p->narrow; }
unsigned int packed_read_word(const struct PackedScalars *p) { return p->word; }
unsigned long long packed_read_wide(const struct PackedScalars *p) { return p->wide; }
double packed_read_double(const struct PackedScalars *p) { return p->fp; }
void packed_write_short(struct PackedScalars *p, short v) { p->narrow = v; }
void packed_write_word(struct PackedScalars *p, unsigned int v) { p->word = v; }
void packed_write_wide(struct PackedScalars *p, unsigned long long v) { p->wide = v; }
void packed_write_double(struct PackedScalars *p, double v) { p->fp = v; }

struct NaturalScalars { unsigned int word; unsigned long long wide; };
unsigned int natural_local_fast_path(void) {
    struct NaturalScalars object;
    object.word = 79U;
    return object.word;
}

#ifndef MINIC_PROBE_NO_MAIN
int main(void) {
    struct PackedScalars object;
    object.prefix = 0x5AU;
    packed_write_short(&object, (short)-1234);
    packed_write_word(&object, 0xA98F1234U);
    packed_write_wide(&object, 0x7766554433221100ULL);
    packed_write_double(&object, 3.75);
    if (object.prefix != 0x5AU ||
        packed_read_short(&object) != (short)-1234 ||
        packed_read_word(&object) != 0xA98F1234U ||
        packed_read_wide(&object) != 0x7766554433221100ULL ||
        packed_read_double(&object) != 3.75 ||
        natural_local_fast_path() != 79U)
        return 1;
    return 0;
}
#endif
