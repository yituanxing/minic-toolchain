/* C array declarators and typedef arrays must agree across local, static,
 * expression, parameter and Core storage paths.  This program is compiled
 * from the same GNU-preprocessed .i by five MiniC targets and GNU itself. */
typedef int Triple[3];
typedef Triple TwoTriples[2];

struct Box {
    unsigned char tag;
    Triple row;
    int tail[2];
};

static const Triple global_triple = {71, 73, 79};
static int bound_evaluations;

static int next_bound(void)
{
    bound_evaluations += 1;
    return 3;
}

static int sum_three(const int values[static 3])
{
    return values[0] + values[1] + values[2];
}

static int check_fixed_locals(void)
{
    int plain[2][3] = {{2, 3, 5}, {7, 11, 13}};
    Triple declarations[2] = {{17, 19, 23}, {29, 31, 37}};
    TwoTriples typedef_outer;
    const int immutable[3] = {67, 71, 73};
    int inferred[] = {79, 83, 89, 97};
    Triple *next = &declarations[1];

    typedef_outer[0][0] = 41;
    typedef_outer[0][1] = 43;
    typedef_outer[0][2] = 47;
    typedef_outer[1][0] = 53;
    typedef_outer[1][1] = 59;
    typedef_outer[1][2] = 61;

    if (sizeof(plain) != 6U * sizeof(int) ||
        sizeof(declarations) != 6U * sizeof(int) ||
        sizeof(typedef_outer) != 6U * sizeof(int) ||
        sizeof(typedef_outer[0]) != 3U * sizeof(int) ||
        sizeof(inferred) != 4U * sizeof(int)) return 1;
    if ((char *)&plain[1] - (char *)&plain[0] != 3 * (int)sizeof(int) ||
        (char *)&declarations[1] - (char *)&declarations[0] !=
            3 * (int)sizeof(int) ||
        (char *)&typedef_outer[1] - (char *)&typedef_outer[0] !=
            3 * (int)sizeof(int)) return 2;
    if (plain[0][2] != 5 || plain[1][1] != 11 ||
        (*next)[0] != 29 || (*next)[2] != 37 ||
        typedef_outer[0][1] != 43 || typedef_outer[1][2] != 61 ||
        immutable[0] != 67 || inferred[3] != 97) return 3;
    if (sum_three(plain[0]) != 10 ||
        sum_three(declarations[1]) != 97 ||
        sum_three(typedef_outer[1]) != 173) return 4;
    return 0;
}

static int check_const_record(void)
{
    const struct Box item = {5, {11, 13, 17}, {19, 23}};
    if (item.tag != 5 || item.row[0] != 11 || item.row[2] != 17 ||
        item.tail[1] != 23) return 1;
    if (sizeof(item.row) != 3U * sizeof(int) ||
        (char *)&item.tail[0] - (char *)&item.row[0] !=
            3 * (int)sizeof(int)) return 2;
    return 0;
}

static int check_static_locals(void)
{
    static const int plain[4] = {31, 37};
    static const int typed_extent[3] = {41, 43, 47};
    static const int inferred[] = {53, 59, 61};
    if (sizeof(plain) != 4U * sizeof(int) ||
        sizeof(typed_extent) != 3U * sizeof(int) ||
        sizeof(inferred) != 3U * sizeof(int)) return 1;
    if (plain[0] != 31 || plain[1] != 37 || plain[2] != 0 ||
        plain[3] != 0 || typed_extent[2] != 47 || inferred[1] != 59 ||
        global_triple[2] != 79) return 2;
    return 0;
}

static int check_vla_and_compound(void)
{
    int values[next_bound()];
    int *literal = (int[]){101, 103, 107};
    values[0] = 109;
    values[1] = 113;
    values[2] = 127;
    if (bound_evaluations != 1 ||
        values[0] != 109 || values[1] != 113 || values[2] != 127)
        return 1;
    if (literal[0] != 101 || literal[1] != 103 || literal[2] != 107)
        return 2;
    return 0;
}

/* GNU C regards a zero-length automatic array as a complete zero-sized
 * object, unlike an incomplete or runtime-sized VLA.  It has an address but
 * must never be indexed or used to overwrite adjacent live locals. */
static int check_gnu_zero_local(void)
{
    int before = 131;
    int empty[0];
    int after = 137;
    void *address = &empty;
    if (sizeof(empty) != 0U || address == 0) return 1;
    if (before != 131 || after != 137) return 2;
    return 0;
}

int main(void)
{
    if (check_fixed_locals()) return 11;
    if (check_const_record()) return 12;
    if (check_static_locals()) return 13;
    if (check_vla_and_compound()) return 14;
    if (check_gnu_zero_local()) return 15;
    return 42;
}
