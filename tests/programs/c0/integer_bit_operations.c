int main(void)
{
    int signed_value;
    int signed_shift;
    unsigned unsigned_value;
    unsigned unsigned_shift;
    int result;

    signed_value = -16;
    signed_shift = signed_value >> 2;
    unsigned_value = 1;
    unsigned_value = unsigned_value << 31;
    unsigned_shift = unsigned_value >> 31;
    result = (3 << 4) + (29 & 15);
    result = result + signed_shift;
    result = result + unsigned_shift;
    return result;
}
