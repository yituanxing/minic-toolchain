int main(void)
{
    int value = 4;
    int values[2];
    unsigned char byte = 255;

    values[0] = 7;
    values[1] = 11;
    value++;
    value--;
    values[0]--;
    values[1]++;
    byte++;

    if (value != 4) {
        return 1;
    }
    if (values[0] != 6 || values[1] != 12) {
        return 2;
    }
    if (byte != 0) {
        return 3;
    }
    return 0;
}
