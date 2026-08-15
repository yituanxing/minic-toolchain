typedef struct {
    unsigned long bits[4];
} range_mask_t;

/* Linux nodemask-style nested positional array initializer with a GNU range designator. */
int runtime_array_range_initializer(void)
{
    range_mask_t mask = (range_mask_t) { { [1 ... 2] = 7UL, [3] = 9UL } };
    return mask.bits[0] == 0UL && mask.bits[1] == 7UL &&
           mask.bits[2] == 7UL && mask.bits[3] == 9UL;
}

int direct_local_array_range_initializer(void)
{
    unsigned long values[4] = { [1 ... 2] = 5UL, [3] = 8UL };
    return values[0] == 0UL && values[1] == 5UL &&
           values[2] == 5UL && values[3] == 8UL;
}

int direct_index_nonconstant(int value)
{
    int values[2] = { [1] = value };
    return values[0] + values[1];
}


static int range_effect(int value)
{
    return value + 1;
}

int single_element_range_effect(int value)
{
    int values[3] = { [1 ... 1] = range_effect(value) };
    return values[0] + values[1] + values[2];
}
