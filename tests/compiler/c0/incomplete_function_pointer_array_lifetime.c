typedef int (*CallbackArray[])(int);

int increment(int value) { return value + 1; }
int decrement(int value) { return value - 1; }

extern int (*callbacks[])(int);
extern int (*callbacks[])(int);
int (*callbacks[])(int) = {increment, decrement};

CallbackArray first_table = {increment};
CallbackArray second_table = {decrement, increment};

extern int (*later[])(int);
extern int (*later[])(int);

int main(void) {
    return callbacks[0](1) == 2 && callbacks[1](1) == 0 &&
           first_table[0](1) == 2 && second_table[0](1) == 0 &&
           second_table[1](1) == 2 ? 0 : 1;
}
