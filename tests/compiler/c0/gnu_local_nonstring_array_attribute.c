/* drivers/soc/qcom/cmd-db.c: `u8 query[sizeof(ent->id)] __nonstring`. */
int main(void) {
    unsigned char query[8] __attribute__((nonstring));
    query[0] = 'a';
    query[1] = 'b';
    return query[0] != 'a' || query[1] != 'b';
}
