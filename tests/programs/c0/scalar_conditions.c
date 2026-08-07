static int bump(int *counter, int result)
{
    *counter = *counter + 1;
    return result;
}

int main(void)
{
    int count;
    int value;
    int *pointer;

    count = 0;
    value = 7;
    pointer = &value;

    if (!pointer) {
        return 1;
    }
    if ((int *)0) {
        return 2;
    }

    if (0 && bump(&count, 1)) {
        return 3;
    }
    if (count != 0) {
        return 4;
    }

    if (!(1 || bump(&count, 0))) {
        return 5;
    }
    if (count != 0) {
        return 6;
    }

    if (!(1 && bump(&count, 1))) {
        return 7;
    }
    if (count != 1) {
        return 8;
    }

    if (!(0 || bump(&count, 1))) {
        return 9;
    }
    if (count != 2) {
        return 10;
    }

    if (!(pointer && 1)) {
        return 11;
    }
    if (!((int *)0 || pointer)) {
        return 12;
    }

    if (!(1 || 0 && bump(&count, 1))) {
        return 13;
    }
    if (count != 2) {
        return 14;
    }

    return 0;
}
