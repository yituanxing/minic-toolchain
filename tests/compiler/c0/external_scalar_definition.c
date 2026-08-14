extern int external_count;
int external_count = 7;

long long external_wide = 11LL;
unsigned long loops_per_jiffy = (1 << 12);
static int internal_folded = (3 + 5) * 2;

int main(void) {
    return external_count == 7 && external_wide == 11LL && loops_per_jiffy == 4096UL &&
                   internal_folded == 16
               ? 0
               : 1;
}
