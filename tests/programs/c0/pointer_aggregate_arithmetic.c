struct Triple {
    int first;
    int second;
    int third;
};

typedef int Row[3];

int main(void)
{
    struct Triple records[2];
    struct Triple *record_base = &records[0];
    struct Triple *record_second = record_base + 1;
    struct Triple *record_first = record_second - 1;
    Row rows[2];
    Row *row_base = &rows[0];

    record_first->first = 3;
    record_second->first = 4;
    (1 + record_base)->second = 5;
    (record_base + 1)->third = 6;

    (row_base + 1)[0][0] = 7;
    (1 + row_base)[0][1] = 8;
    (row_base + 1)[0][2] = 9;

    return record_first->first + record_second->first +
           record_second->second + record_second->third +
           (row_base + 1)[0][0] + (1 + row_base)[0][1] +
           (row_base + 1)[0][2];
}
