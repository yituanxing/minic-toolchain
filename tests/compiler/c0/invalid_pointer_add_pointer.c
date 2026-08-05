int main(void)
{
    int left_value = 1;
    int right_value = 2;
    int *left = &left_value;
    int *right = &right_value;

    return *(left + right);
}
