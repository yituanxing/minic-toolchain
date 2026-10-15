enum unsigned_byte_enum {
    UBE_ZERO = 0,
    UBE_HIGH = 200,
} __attribute__((__mode__(byte)));

enum signed_byte_enum {
    SBE_LOW = -128,
    SBE_HIGH = 100,
} __attribute__((mode(__byte__)));

static enum unsigned_byte_enum unsigned_value = UBE_HIGH;
static enum signed_byte_enum signed_value = SBE_LOW;

int main(void) {
    return sizeof(unsigned_value) != 1 || sizeof(signed_value) != 1;
}
