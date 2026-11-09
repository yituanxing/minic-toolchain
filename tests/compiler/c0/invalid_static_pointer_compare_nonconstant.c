extern int dynamic_value;
struct entry { int flag; };
static struct entry entries[] = {
    { .flag = (((int *)(long)dynamic_value) < (int *)1) },
};
int main(void) { return entries[0].flag; }
