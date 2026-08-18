struct uts_like {
    int value;
};
struct uts_like weak_record __attribute__((__weak__));
const char weak_banner[] __attribute__((__weak__));
int weak_defined __attribute__((weak)) = 7;
int main(void) {
    return weak_record.value + weak_banner[0] + weak_defined - 7;
}
