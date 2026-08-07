int main(void)
{
    int int_value;
    char char_value;
    int *int_pointer;
    char *char_pointer;

    int_pointer = &int_value;
    char_pointer = &char_value;
    return int_pointer == char_pointer;
}
