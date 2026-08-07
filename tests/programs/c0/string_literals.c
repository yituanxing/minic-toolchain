int main(void)
{
    const char *text;
    const char *empty;

    text = "A\n\"\\";
    empty = "";
    return (text[0] - 65) + (text[1] - 10) + (text[2] - 34) + (text[3] - 92) + text[4] +
           empty[0];
}
