int main(void)
{
    int value;

    if ('\0' != 0) {
        return 1;
    }
    if ('A' != 65) {
        return 2;
    }
    if ('.' != 46 || '0' != 48 || '9' != 57) {
        return 3;
    }
    if ('\n' != 10 || '\t' != 9) {
        return 4;
    }
    if ('\\' != 92 || '\'' != 39 || '"' != 34 || '\?' != 63) {
        return 5;
    }

    value = '1' + '2' - '0';
    if (value != 51) {
        return 6;
    }
    return 0;
}
