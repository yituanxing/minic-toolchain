/* GNU C permits a completed array declaration before an omitted-bound
 * string-literal definition. The existing complete extent must be retained. */
char exact[4];
char exact[] = "abc";

char padded[6];
char padded[] = "abc";

char no_nul[3];
char no_nul[] = "abc";

extern char extern_decl[4];
char extern_decl[] = "xyz";

int main(void) {
    return !(exact[0] == 'a' && exact[3] == 0 &&
             padded[2] == 'c' && padded[3] == 0 && padded[5] == 0 &&
             no_nul[0] == 'a' && no_nul[2] == 'c' &&
             extern_decl[0] == 'x' && extern_decl[3] == 0);
}
