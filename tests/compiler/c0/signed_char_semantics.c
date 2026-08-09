static signed char signed_value = 254;
static unsigned char unsigned_value = 254;
static char plain_value = (char)255;

int main(void) {
    signed char local = 0;
    signed char *signed_pointer = &signed_value;
    unsigned char *unsigned_pointer = &unsigned_value;
    char *plain_pointer = &plain_value;

    local = *signed_pointer;
    return local == -2 && *unsigned_pointer == 254 && *plain_pointer == 255 ? 0 : 1;
}
