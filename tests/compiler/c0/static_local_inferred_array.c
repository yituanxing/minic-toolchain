typedef unsigned char MiniByte;

int static_table_checksum(void) {
    static const MiniByte nextage[] = {1, 3, 3, 4, 4, 5, 6};
    return (int)sizeof(nextage) + nextage[0] + nextage[6];
}
