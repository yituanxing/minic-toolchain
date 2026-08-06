int main(void)
{
    int value;
    int result;

    result = 0;
    for (value = 5; ; --value) {
        result = result + value;
        if (value == 1) {
            break;
        }
    }
    result = result + value;
    return result;
}
