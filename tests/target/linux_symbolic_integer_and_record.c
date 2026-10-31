/* Real Linux patterns: link-time integer addresses and GNU aggregate return
 * from a statement expression. Keep these forms target-neutral. */
extern char kernel_link_base[];
extern int kernel_link_object;

unsigned long kernel_percpu_offset = (unsigned long)kernel_link_base;
unsigned long kernel_percpu_table[64] = {
    [0 ... 63] = (unsigned long)kernel_link_base,
};
unsigned long kernel_percpu_addend[2] = {
    (unsigned long)&kernel_link_object + 8,
    (unsigned long)kernel_link_base + 16,
};

typedef struct kernel_page_pair {
    unsigned long first;
    unsigned long second;
} kernel_page_pair;

kernel_page_pair kernel_make_pair(unsigned long number) {
    kernel_page_pair pair = {number, number + 7};
    return pair;
}

unsigned long kernel_record_statement_value(unsigned long number) {
    kernel_page_pair result;
    result = ({
        unsigned long index = number + 1;
        kernel_make_pair(index);
    });
    return result.first + result.second;
}

int main(void) {
    return kernel_record_statement_value(4) == 17 ? 0 : 1;
}
