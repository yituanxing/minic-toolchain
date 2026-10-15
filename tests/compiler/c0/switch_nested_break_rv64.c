#define CHECKVAL(res) { if ((res) == -1) break; }

static int calls;

static int guarded_api(void)
{
    calls += 1;
    return -1;
}

static int exercise(int option)
{
    switch (option) {
    case 6: {
        int res = guarded_api();
        CHECKVAL(res);
        return 60;
    }
    case 7:
        return guarded_api();
    case 8:
        return guarded_api();
    default:
        return guarded_api();
    }
    return calls == 1 ? 23 : 99;
}

int main(void)
{
    int result = exercise(6);
    if (result != 23)
        return 1;
    if (calls != 1)
        return 2;
    return 0;
}
