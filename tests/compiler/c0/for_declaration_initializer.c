typedef unsigned long size_t;

enum mod_mem_type { MOD_TEXT = 0, MOD_DATA = 1, MOD_MEM_NUM_TYPES = 2 };

int main(void) {
    char etalon[1024 * 1024];
    int sum = 0;

    for (size_t i = 0; i < sizeof(etalon); i++) {
        if (i == 4) {
            break;
        }
        sum += (int)i;
    }

    for (int i = 0; i < 3; i++) {
        sum += i;
    }

    for (enum mod_mem_type (type) = 0; type < MOD_MEM_NUM_TYPES; type++) {
        sum += type;
    }

    int (parenthesized) = 3;
    sum += parenthesized;

    int i = 7;
    return sum == 14 && i == 7 ? 0 : 1;
}
