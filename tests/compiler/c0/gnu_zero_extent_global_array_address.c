/* sound/soc/soc-core.c: GCC permits a GNU-complete zero-sized array and
 * Linux __ADDRESSABLE emits a global address constant for it. */
struct component { int id; };
struct component null_components[0];
extern typeof(null_components) null_components;
static void *const addressable_null_components = (void *)&null_components;

int main(void) {
    return sizeof(null_components) != 0 ||
           addressable_null_components != (void *)&null_components;
}
