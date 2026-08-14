typedef int (*MiniBinary)(int left, int right);

int call_pointer(MiniBinary function, int left, int right) {
    return function(left, right);
}

int call_dereferenced_pointer(MiniBinary function, int left, int right) {
    return (*function)(left, right);
}
