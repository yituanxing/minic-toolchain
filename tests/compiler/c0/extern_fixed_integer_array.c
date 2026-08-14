typedef unsigned char MiniByte;

extern const MiniByte table[2 + 3];

const MiniByte table[2 + 3] = {1, 2, 3};

int main(void) {
    return sizeof(table) == 5 && table[0] == 1 && table[2] == 3 && table[3] == 0 &&
                   table[4] == 0
               ? 0
               : 1;
}
