/* Double to signed/unsigned 8-/16-bit C conversions (all representable). */
#include <stdio.h>
int main(int argc, char **argv)
{
    double a = argc ? -12.75 : -10.0;
    double b = argc ? 240.9 : 200.0;
    double c = argc ? -1234.75 : -1200.0;
    double d = argc ? 40000.6 : 35000.0;
    signed char sc = (signed char)a;
    unsigned char uc = (unsigned char)b;
    signed short ss = (signed short)c;
    unsigned short us = (unsigned short)d;
    (void)argv;
    printf("%d %u %d %u\n", (int)sc, (unsigned int)uc,
                              (int)ss, (unsigned int)us);
    return 0;
}
