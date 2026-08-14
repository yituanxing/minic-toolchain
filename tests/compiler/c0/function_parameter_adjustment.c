typedef int callback_t(int value);

typedef int done_t(int value, void *private_data);

int apply_callback(callback_t callback);
int apply_callback(int (*callback)(int value))
{
    _Static_assert(sizeof(callback) == sizeof(void *), "function parameter adjusts to pointer");
    return callback(7);
}

int invoke_done(done_t done, void *private_data)
{
    return done(5, private_data);
}

int plus_one(int value)
{
    return value + 1;
}

int finish(int value, void *private_data)
{
    return value + (private_data != (void *)0);
}

int main(void)
{
    int marker = 1;
    return apply_callback(plus_one) == 8 && invoke_done(finish, &marker) == 6 ? 0 : 1;
}
