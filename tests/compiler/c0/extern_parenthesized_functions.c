extern int (external_add)(int left, int right);
extern void *(external_alloc)(void *pointer, unsigned size);

int main(void) {
    char value = 0;
    return external_add(1, 2) == 3 && external_alloc(&value, 1) != 0 ? 0 : 1;
}
