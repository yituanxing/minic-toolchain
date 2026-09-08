static char local_from_double(double value)
{
    char narrowed = value;
    return narrowed;
}

static unsigned return_from_double(double value)
{
    return value;
}

static float return_from_int(int value)
{
    return value;
}

static double local_from_float(float value)
{
    double widened = value;
    return widened;
}

int main(void)
{
    if (local_from_double(65.75) != 65) {
        return 1;
    }
    if (return_from_double(42.9) != 42U) {
        return 2;
    }
    if (return_from_int(7) != 7.0f) {
        return 3;
    }
    if (local_from_float(3.5f) != 3.5) {
        return 4;
    }
    return 0;
}
