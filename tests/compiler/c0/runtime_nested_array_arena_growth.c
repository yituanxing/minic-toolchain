/* MC-039: parsing sizeof(type) inside a nested runtime array initializer
 * may grow Program.array_types before the next nested element is parsed. */
int main(void) {
    (void)sizeof(int[3]);  (void)sizeof(int[4]);
    (void)sizeof(int[5]);  (void)sizeof(int[6]);
    (void)sizeof(int[7]);  (void)sizeof(int[8]);
    (void)sizeof(int[9]);  (void)sizeof(int[10]);
    (void)sizeof(int[11]); (void)sizeof(int[12]);
    (void)sizeof(int[13]); (void)sizeof(int[14]);
    (void)sizeof(int[15]);
#ifndef MC039_SKIP_LAST_PREFIX
    (void)sizeof(int[16]);
#endif
#ifdef MC039_EXTRA_PREFIX
    (void)sizeof(int[17]);
#endif
#ifdef MC039_EXTRA_PREFIX_2
    (void)sizeof(int[18]);
#endif
    int values[2][1] = {{sizeof(int[777])}, {5}};
    return values[0][0] + values[1][0] == (int)sizeof(int[777]) + 5 ? 0 : 1;
}
