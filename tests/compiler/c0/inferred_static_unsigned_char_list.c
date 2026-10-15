static const unsigned char filetype_table[] = {0, 8, 4, 2, 6, 1, 12, 10};

int main(void) {
    return sizeof(filetype_table) == 8 && filetype_table[0] == 0 && filetype_table[1] == 8 &&
                   filetype_table[6] == 12 && filetype_table[7] == 10
               ? 0
               : 1;
}
