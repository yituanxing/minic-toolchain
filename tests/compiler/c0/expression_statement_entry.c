struct counters {
    int contexts;
};

int drive_expression_entries(struct counters *ctx, int *value) {
    ++ctx->contexts;
    --*value;
    ~ctx->contexts;
    'x';
    1.5;
    "entry";
    sizeof(ctx->contexts);
    _Alignof(int);
    return ctx->contexts + *value;
}
