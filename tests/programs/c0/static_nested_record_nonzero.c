typedef struct {
    int counter;
} atomic_like;

struct tracking {
    atomic_like state;
    long nesting;
    long nmi;
};

struct tracking value = {
    .nesting = 1,
    .nmi = 2,
    .state = {3},
};

int main(void)
{
    return value.state.counter != 3 || value.nesting != 1 || value.nmi != 2;
}
