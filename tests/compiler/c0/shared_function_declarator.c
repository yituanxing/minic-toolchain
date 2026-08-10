typedef int (UnaryFunction)(int);

struct Ops {
    int (*run)(int);
};

extern int (*external_hook)(int);

static int add_one(int value)
{
    return value + 1;
}

static int apply(int (*function)(int), int value)
{
    return function(value);
}

int main(void)
{
    return apply(add_one, 41) - 42;
}
