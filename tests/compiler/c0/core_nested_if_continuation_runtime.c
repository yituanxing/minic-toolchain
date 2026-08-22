#include <stdio.h>

int core_nested_then(int outer, int inner);
int core_nested_both(int outer, int inner);

int main(void) {
    printf("%d %d %d %d %d %d\n",
           core_nested_then(0, 0),
           core_nested_then(1, 0),
           core_nested_then(1, 1),
           core_nested_both(1, 0),
           core_nested_both(0, 0),
           core_nested_both(0, 1));
    return 0;
}
