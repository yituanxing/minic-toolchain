int main(void)
{
#if CASE == 1
    return !0;
#elif CASE == 2
    return !5;
#elif CASE == 3
    return !!7;
#elif CASE == 4
    return !(3 < 4);
#elif CASE == 5
    int x = 0;
    return !x;
#else
#error unsupported logical-not test case
#endif
}
