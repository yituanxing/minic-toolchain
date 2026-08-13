struct node { int value; };

extern void consume_nodes(struct node *items[], unsigned int count);
extern void consume_matrix(int matrix[][3]);

typedef struct node node_vector[1];
typedef int matrix_alias[2][3];

static void consume_typedef_vector(node_vector items) {
    (void)items;
}

static int consume_typedef_matrix(matrix_alias matrix) {
    return matrix[1][2];
}

int main(void) {
    return 0;
}
