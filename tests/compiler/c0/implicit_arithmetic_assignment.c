static unsigned convert_with_initializer(double value)
{
    unsigned local = value;
    return local;
}

static unsigned convert_with_return(double value)
{
    return value;
}

int main(void)
{
    return convert_with_initializer(7.75) == 7U &&
           convert_with_return(11.5) == 11U
               ? 0
               : 1;
}
