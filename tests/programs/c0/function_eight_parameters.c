int one(void) { return 1; }
int two(void) { return 2; }
int three(void) { return 3; }
int four(void) { return 4; }
int five(void) { return 5; }
int six(void) { return 6; }
int seven(void) { return 7; }
int eight(void) { return 8; }

int mix8(
    int a, int b, int c, int d,
    int e, int f, int g, int h)
{
    return a * 8 + b * 7 + c * 6 + d * 5
         + e * 4 + f * 3 + g * 2 + h;
}

int main(void)
{
    int saved = 5;
    return mix8(
        one(), two(), three(), four(),
        five(), six(), seven(), eight()) + saved;
}
