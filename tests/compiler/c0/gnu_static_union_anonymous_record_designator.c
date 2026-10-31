/* arch/arm64/kvm/emulate-nested.c uses promoted anonymous bitfields in
 * designated static union initializers nested inside arrays of records. */
union trap_config {
    unsigned long long val;
    struct {
        unsigned long long cgt:10;
        unsigned long long fgt:4;
        unsigned long long bit:6;
        unsigned long long pol:1;
        unsigned long long fgf:5;
        unsigned long long unused:37;
        unsigned long long mbz:1;
    };
};

struct encoding_to_trap_config {
    const unsigned int encoding;
    const unsigned int end;
    const union trap_config tc;
    const unsigned int line;
};

static const struct encoding_to_trap_config entries[] = {
    {.encoding = 11, .end = 11, .tc = {.cgt = 3}, .line = 21},
    {.encoding = 12, .end = 12,
     .tc = {.fgt = 2, .bit = 4, .pol = 1, .fgf = 3}, .line = 22},
    {.encoding = 13, .end = 13, .tc = {.val = 41}, .line = 23}
};

int main(void) {
    return entries[0].tc.cgt != 3 || entries[0].tc.val != 3 ||
           entries[1].tc.fgt != 2 || entries[1].tc.bit != 4 ||
           entries[1].tc.pol != 1 || entries[1].tc.fgf != 3 ||
           entries[2].tc.val != 41 || entries[2].line != 23;
}
