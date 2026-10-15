typedef int LOGFUNC_t;

int block_scope_typedef_probe(void)
{
    {
        typedef void (*LOGFUNC_t)(void *, int, const char *);
        LOGFUNC_t xLog = (LOGFUNC_t)0;
        if (xLog != 0) {
            return 1;
        }
    }

    LOGFUNC_t value = 7;
    return value == 7 ? 0 : 2;
}
