/* File-scope typedef arrays must use the same tentative/definition entity. */
typedef int Three[3];
typedef char Word[4];
typedef int *PointerPair[2];
typedef int Open[];
typedef const int ReadOnlyPair[2];

static Three fixed_decl;
static Three fixed_decl = { 3, 5, 7 };
static Word text_decl;
static Word text_decl = "abc";

static int first = 11;
static int second = 13;
static PointerPair pointer_decl;
static PointerPair pointer_decl = { &first, &second };
static ReadOnlyPair readonly_decl;
static ReadOnlyPair readonly_decl = { 37, 41 };

static Open independently_inferred_a = { 17, 19 };
static Open independently_inferred_b = { 23, 29, 31 };

int main(void) {
    return sizeof(fixed_decl) == 3U * sizeof(int) &&
                   fixed_decl[0] == 3 && fixed_decl[2] == 7 &&
                   sizeof(text_decl) == 4 && text_decl[2] == 'c' &&
                   pointer_decl[0] == &first && pointer_decl[1] == &second &&
                   readonly_decl[0] == 37 && readonly_decl[1] == 41 &&
                   sizeof(independently_inferred_a) == 2U * sizeof(int) &&
                   sizeof(independently_inferred_b) == 3U * sizeof(int) &&
                   independently_inferred_a[1] == 19 &&
                   independently_inferred_b[2] == 31
               ? 0 : 1;
}
