extern int external_count;
int external_count = 7;

long long external_wide = 11LL;

int main(void) {
    return external_count == 7 && external_wide == 11LL ? 0 : 1;
}
