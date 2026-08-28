typedef unsigned char u8;
typedef u8 blk_status_t;

struct pointer_case_item {
    int value;
};

int linux_casted_case(blk_status_t error) {
    switch (error) {
    case ((blk_status_t)1):
        return 1;
    case ((blk_status_t)(1 + 2)):
        return 3;
    case ((blk_status_t)4)...((blk_status_t)(2 + 4)):
        return 4;
    default:
        return 0;
    }
}

int typed_narrowing_case(unsigned int value) {
    switch (value) {
    case ((u8)257):
        return 1;
    case ((u8)-1):
        return 255;
    default:
        return 0;
    }
}

int pointer_integer_roundtrip_case(unsigned long value) {
    switch (value) {
    case (unsigned long)((struct pointer_case_item *)1):
        return 1;
    case (unsigned long)((struct pointer_case_item *)(1 + 2)):
        return 3;
    default:
        return 0;
    }
}

int unsigned_64_case(unsigned long value) {
    switch (value) {
    case (3UL | (1UL << 63)):
        return 1;
    case -4ULL:
        return 2;
    default:
        return 0;
    }
}
