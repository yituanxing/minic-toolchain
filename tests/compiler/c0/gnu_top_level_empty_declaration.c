;
static int first(void)
{
    return 11;
}
;;
typedef int probe_int_t;
;
static probe_int_t second(void)
{
    return first() + 31;
}
;
int main(void)
{
    return second() == 42 ? 0 : 1;
}
;
