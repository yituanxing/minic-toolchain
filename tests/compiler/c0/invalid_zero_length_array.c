int main(void)
{
    /* Negative bounds remain invalid.  GNU C accepts local T[0]. */
    int values[-1];

    return 0;
}
