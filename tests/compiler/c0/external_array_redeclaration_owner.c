extern int tentative_values[];
int tentative_values[4];
extern int defined_values[];
int defined_values[4] = {1, 2, 3, 4};
int main(void) {
    return tentative_values[0] + defined_values[3] - 4;
}
