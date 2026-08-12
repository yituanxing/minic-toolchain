struct cpumask {
    unsigned long bits[2];
};

typedef struct cpumask cpumask_var_t[1];
extern cpumask_var_t irq_default_affinity;

typedef unsigned long row_t[3];
extern row_t matrix[2];

typedef int triple_t[3];
extern triple_t values;
extern int values[3];

struct cpumask *default_affinity(void) {
    return &irq_default_affinity[0];
}

unsigned long *select_row(unsigned int index) {
    return matrix[index];
}

int *values_ptr(void) {
    return values;
}

unsigned long matrix_size(void) {
    return sizeof(matrix);
}

unsigned long values_size(void) {
    return sizeof(values);
}
