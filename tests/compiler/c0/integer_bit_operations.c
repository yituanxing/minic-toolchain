int main(void)
{
    int signed_value;
    unsigned unsigned_value;
    int result;

    signed_value = -16;
    unsigned_value = 0x80000000;
    result = (3 << 4) + (signed_value >> 2);
    result = result + (int)(unsigned_value >> 31);
    result = result + (29 & 15);
    return result;
}
