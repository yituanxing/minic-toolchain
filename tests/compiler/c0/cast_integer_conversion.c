int main(void) {
    unsigned int unsigned_value;
    int signed_value;

    unsigned_value = (unsigned int)-1;
    signed_value = (int)unsigned_value;
    return signed_value == -1 ? 0 : 1;
}
