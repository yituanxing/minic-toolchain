struct match_token {
    int token;
};

typedef struct match_token match_table_t[];
typedef match_table_t *match_table_pointer_t;

int main(void) {
    match_table_pointer_t table = 0;
    return table != 0;
}
