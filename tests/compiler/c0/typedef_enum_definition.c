typedef enum {
    ITEM_ZERO,
    ITEM_THREE = 3,
    ITEM_FOUR,
} ItemKind;

int main(void) {
    ItemKind kind = ITEM_FOUR;
    return kind == 4 && ITEM_ZERO == 0 ? 0 : 1;
}
