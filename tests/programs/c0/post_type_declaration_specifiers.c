typedef int Word;

Word static internal_value = 35;
Word extern external_value;
Word external_value = 7;

Word inline helper(void)
{
    return internal_value + external_value;
}

int main(void)
{
    return helper() == 42 ? 0 : 1;
}
