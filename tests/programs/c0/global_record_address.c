typedef struct {
    int value;
} Box;

static Box global_box = { 0 };

static int read_box(Box *box) {
    return box->value;
}

int main(void) {
    if (read_box(&global_box) != 0) {
        return 1;
    }
    return 0;
}
