typedef long signed int signed_long_a;
typedef signed long signed_long_b;
typedef unsigned long int unsigned_long_a;
typedef long unsigned int size_word;

static signed_long_a identity_signed(signed_long_b value) {
    return value;
}

static unsigned_long_a identity_unsigned(size_word value) {
    return value;
}

int main(void) {
    signed_long_a left = 3;
    size_word right = 4;
    return (int)(identity_signed(left) + identity_unsigned(right));
}
