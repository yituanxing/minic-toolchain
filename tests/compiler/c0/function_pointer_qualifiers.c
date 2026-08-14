typedef int callback_t(int value);

struct Ops {
    int (* const filter)(int value);
    int (* volatile hook)(int value);
};

typedef int (* const const_callback_t)(int value);

int call_filter(struct Ops *ops, int value)
{
    return ops->filter(value);
}

int call_hook(struct Ops *ops, int value)
{
    return ops->hook(value);
}

int call_typedef(const_callback_t callback, int value)
{
    return callback(value);
}
