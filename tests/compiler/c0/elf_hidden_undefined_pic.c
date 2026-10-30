/* GNU PIC visibility oracle. GNU AS must receive .hidden for undefined
 * external declarations, while default-visible addresses remain preemptible. */
extern int hidden_data __attribute__((visibility("hidden")));
extern int default_data;
__attribute__((visibility("hidden"))) extern int hidden_function(int);
extern int default_function(int);

int *mini_hidden_data_address(void) { return &hidden_data; }
int *mini_default_data_address(void) { return &default_data; }
int (*mini_hidden_function_address(void))(int) { return &hidden_function; }
int (*mini_default_function_address(void))(int) { return &default_function; }

int mini_hidden_pic_runtime(int value) {
    int *hidden = mini_hidden_data_address();
    int *external = mini_default_data_address();
    int (*hidden_call)(int) = mini_hidden_function_address();
    int (*default_call)(int) = mini_default_function_address();
    return *hidden + *external + hidden_call(value) + default_call(value + 1);
}
