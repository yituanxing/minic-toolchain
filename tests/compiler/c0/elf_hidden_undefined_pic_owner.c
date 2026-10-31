/* Distinct translation unit: hidden symbols must remain HIDDEN UND in the
 * MiniC object and resolve locally when GNU LD combines the shared module. */
__attribute__((visibility("hidden"))) int hidden_data = 7;
int default_data = 11;

__attribute__((visibility("hidden"))) int hidden_function(int value) {
    return value + 3;
}
int default_function(int value) { return value + 5; }

extern int mini_hidden_pic_runtime(int);
int main(void) { return mini_hidden_pic_runtime(2) == 31 ? 0 : 1; }
