/* Composite array declarations: the same object may begin incomplete or with
 * an explicit bound, then become a complete initialized definition. */
static int fixed_to_inferred[2];
static int fixed_to_inferred[] = {11, 22};

static int incomplete_to_inferred[];
static int incomplete_to_inferred[] = {31, 37, 41};

static char fixed_text[5];
static char fixed_text[] = "good";

static char incomplete_text[];
static char incomplete_text[] = "ok";

static int tentative_chain[];
static int tentative_chain[3];
static int tentative_chain[] = {5, 7, 9};

static int *pointer_chain[2];
static int *pointer_chain[] = {&fixed_to_inferred[0], &fixed_to_inferred[1]};

int main(void) {
    return sizeof(fixed_to_inferred) == 2 * sizeof(int) &&
                   fixed_to_inferred[1] == 22 &&
                   sizeof(incomplete_to_inferred) == 3 * sizeof(int) &&
                   incomplete_to_inferred[2] == 41 &&
                   sizeof(fixed_text) == 5 && fixed_text[0] == 'g' &&
                   sizeof(incomplete_text) == 3 && incomplete_text[1] == 'k' &&
                   sizeof(tentative_chain) == 3 * sizeof(int) &&
                   tentative_chain[1] == 7 &&
                   pointer_chain[0] == &fixed_to_inferred[0] &&
                   *pointer_chain[1] == 22
               ? 0
               : 1;
}
