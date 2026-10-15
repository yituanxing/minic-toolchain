int main(void)
{
    double huge = __builtin_huge_val();
    return huge > 1.0e300 ? 0 : 1;
}
