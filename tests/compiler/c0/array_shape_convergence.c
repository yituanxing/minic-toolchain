/* One source shape must survive C parsing, Core lowering and all five
 * target emitters.  Keep each observable address/extent check live. */
typedef int Row[3];

struct Fixed {
    int tag;
    Row matrix_row;
    int tail[2];
};

struct Zero {
    int tag;
    int empty[0];
    int tail;
};

struct Flexible {
    unsigned long count;
    unsigned char payload[];
};

static int bound_evaluations;

static int next_bound(void)
{
    ++bound_evaluations;
    return 3;
}

static int check_nested_typedef(void)
{
    Row rows[2] = {{1, 2, 3}, {4, 5, 6}};
    Row *second = &rows[1];

    if (sizeof(rows) != 6U * sizeof(int) ||
        sizeof(rows[0]) != 3U * sizeof(int) ||
        sizeof(*second) != 3U * sizeof(int)) return 1;
    if ((char *)&rows[1] - (char *)&rows[0] != 3 * (int)sizeof(int))
        return 2;
    if ((*second)[0] != 4 || (*second)[2] != 6 || rows[0][1] != 2)
        return 3;
    return 0;
}

static int check_record_fields(void)
{
    struct Fixed fixed = {7, {11, 12, 13}, {17, 19}};
    if (sizeof(fixed.matrix_row) != 3U * sizeof(int) ||
        fixed.matrix_row[2] != 13 || fixed.tail[1] != 19) return 1;
    if ((char *)&fixed.tail[0] - (char *)&fixed.matrix_row[0] !=
        3 * (int)sizeof(int)) return 2;
    return 0;
}

static int check_gnu_zero_and_flexible(void)
{
    struct Zero zero = {3, {}, 5};
    struct Flexible flexible = {9};
    if (sizeof(zero.empty) != 0U || zero.tag != 3 || zero.tail != 5)
        return 1;
    if ((char *)&zero.tail - (char *)&zero.empty != 0)
        return 2;
    if ((char *)flexible.payload - (char *)&flexible !=
        (int)sizeof(unsigned long)) return 3;
    return 0;
}

static int check_vla(void)
{
    int values[next_bound()];
    values[0] = 23;
    values[1] = 29;
    values[2] = 31;
    if (bound_evaluations != 1 || values[0] != 23 ||
        values[1] != 29 || values[2] != 31) return 1;
    return 0;
}

static int check_array_compound_literal(void)
{
    int *values = (int[]){37, 41, 43};
    return values[0] == 37 && values[1] == 41 && values[2] == 43 ? 0 : 1;
}

static int check_inferred_extent(void)
{
    int values[] = {47, 53};
    if (sizeof(values) != 2U * sizeof(int)) return 1;
    if (values[0] != 47 || values[1] != 53) return 2;
    return 0;
}

int main(void)
{
    if (check_nested_typedef()) return 11;
    if (check_record_fields()) return 12;
    if (check_gnu_zero_and_flexible()) return 13;
    if (check_vla()) return 14;
    if (check_array_compound_literal()) return 15;
    if (check_inferred_extent()) return 16;
    return 0;
}
