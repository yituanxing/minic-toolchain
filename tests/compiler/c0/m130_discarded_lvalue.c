typedef struct {
    int value;
} item_t;

static item_t global_item = {7};

static item_t *touch_item(int *count) {
    *count += 1;
    return &global_item;
}

int main(void) {
    int count = 0;
    (void)touch_item(&count)->value;
    return count == 1 ? 0 : 1;
}
