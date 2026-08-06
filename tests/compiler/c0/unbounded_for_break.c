int main(void)
{
    int outer;
    int inner;
    int result;

    result = 0;
    for (outer = 0; ; ++outer) {
        for (inner = 0; ; ++inner) {
            result = result + 1;
            if (inner == 2) {
                break;
            }
        }
        result = result + inner;
        result = result + 10;
        if (outer == 1) {
            break;
        }
    }
    result = result + outer;
    return result;
}
