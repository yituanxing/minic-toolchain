typedef unsigned char lu_byte;

typedef struct {
    unsigned long pad0;
    unsigned long pad1;
    unsigned long pad2;
    unsigned long pad3;
    unsigned long pad4;
    unsigned long pad5;
    unsigned long pad6;
    unsigned long pad7;
    unsigned long pad8;
    unsigned long pad9;
    unsigned long pad10;
    unsigned long pad11;
    unsigned long pad12;
    lu_byte gcstp;
} State;

static int nested_gc(State *g)
{
    if (g->gcstp & (2 | 4))
        return -1;
    return 7;
}

static int run_finalizer(State *g)
{
    lu_byte oldgcstp = g->gcstp;
    int result;
    g->gcstp |= 2;
    result = nested_gc(g);
    g->gcstp = oldgcstp;
    return result;
}

int main(void)
{
    State g = {0};
    int result = run_finalizer(&g);
    if (result != -1)
        return 1;
    if (g.gcstp != 0)
        return 2;
    g.gcstp = 4;
    if (nested_gc(&g) != -1)
        return 3;
    return 0;
}
