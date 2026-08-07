typedef long unsigned int size_t;

struct Hooks {
    void *(*alloc)(size_t size);
    void (*free_fn)(void *);
};

int main(void)
{
    return 0;
}
