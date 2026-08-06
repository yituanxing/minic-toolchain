typedef int Row[3];
typedef Row Matrix[2];

static int exercise(Matrix *matrix)
{
    (*matrix)[0][0] = 2;
    (*matrix)[0][1] = 4;
    (*matrix)[1][2] = 9;
    return (*matrix)[0][0] + (*matrix)[0][1] + (*matrix)[1][2];
}

int main(void)
{
    Matrix matrix;

    return exercise(&matrix);
}
