struct match_token {
    int token;
    const char *pattern;
};
typedef struct match_token match_table_t[];

static const match_table_t earlier = {
    { 1, "first" },
    { 2, "second" },
};

static const match_table_t later = {
    { 3, "third" },
    { 4, "fourth" },
    { 5, "fifth" },
    { 6, "sixth" },
    { 7, "seventh" },
};

const match_table_t external_earlier = {
    { 8, "eighth" },
};

const match_table_t external_later = {
    { 9, "ninth" },
    { 10, "tenth" },
    { 11, "eleventh" },
};

extern const match_table_t declared_before_definition;
const match_table_t declared_before_definition = {
    { 12, "twelfth" },
    { 13, "thirteenth" },
    { 14, "fourteenth" },
    { 15, "fifteenth" },
};

int main(void) {
    return !(sizeof(earlier) / sizeof(earlier[0]) == 2 &&
             sizeof(later) / sizeof(later[0]) == 5 &&
             later[4].token == 7 &&
             sizeof(external_earlier) / sizeof(external_earlier[0]) == 1 &&
             sizeof(external_later) / sizeof(external_later[0]) == 3 &&
             external_later[2].token == 11 &&
             sizeof(declared_before_definition) / sizeof(declared_before_definition[0]) == 4 &&
             declared_before_definition[3].token == 15);
}
