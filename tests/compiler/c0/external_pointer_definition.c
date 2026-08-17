char *message = "hello";
void (*power_off)(void) = ((void *)0);
int object_target = 41;
int *object_pointer = &object_target;
struct PointerPair {
    int left;
    int right;
};
struct PointerPair pair_target = {.left = 7, .right = 23};
int *member_pointer = &pair_target.right;
int callback_target(void) {
    return 17;
}
int (*callback_pointer)(void) = callback_target;
int main(void) {
    if (message[0] != 'h' || message[4] != 'o')
        return 1;
    if (power_off != 0)
        return 2;
    if (*object_pointer != 41)
        return 3;
    if (*member_pointer != 23)
        return 4;
    if (callback_pointer() != 17)
        return 5;
    return 0;
}
