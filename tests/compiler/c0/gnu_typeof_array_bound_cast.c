typedef unsigned int
    note_like_t[(((sizeof(unsigned long) + ((__typeof__((sizeof(unsigned long))))((4)) - 1)) &
                  ~((__typeof__((sizeof(unsigned long))))((4)) - 1)) *
                 2 / 4)];

int typeof_array_bound_cast_size(void) {
    return (int)sizeof(note_like_t);
}
