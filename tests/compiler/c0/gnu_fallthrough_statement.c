int fallthrough_shape(int value)
{
    switch (value) {
    case 0:
        value = 3;
        __attribute__((__fallthrough__));
    case 1:
        return value + 4;
    case 2:
        value = 7;
        __attribute__((fallthrough));
    default:
        return value;
    }
}
