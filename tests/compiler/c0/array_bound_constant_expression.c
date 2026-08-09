static int multiplied[1024 * 1024];
static unsigned char grouped[(2 + 3) * (8 - 1)];
static int divided[(24 / 3) + (7 % 4)];

int main(void) {
    return sizeof(multiplied) == 4194304 && sizeof(grouped) == 35 &&
                   sizeof(divided) == 44
               ? 0
               : 1;
}
