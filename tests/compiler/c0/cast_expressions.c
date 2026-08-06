typedef int row_t[3];

int main(void)
{
    int values[3];
    int *flat;
    row_t *row;
    unsigned unsigned_value;
    int signed_value;
    int result;

    values[0] = 7;
    values[1] = 11;
    values[2] = 13;
    flat = &values[0];
    row = (row_t *)flat;
    unsigned_value = (unsigned)-1;
    signed_value = (int)unsigned_value;
    result = (values[0] + values[2]);
    result = result - 20;
    return (*row)[1] + (signed_value == -1) + result;
}
