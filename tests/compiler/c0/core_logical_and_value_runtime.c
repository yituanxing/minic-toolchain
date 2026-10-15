#include <stdio.h>

struct core_m19_node {
    struct core_m19_node *next;
    struct core_m19_node *prev;
};

int core_m19_plain(int left, int right);
int core_m19_short_false(void);
int core_m19_short_true(void);
int core_m19_get_rhs_calls(void);
int core_m19_nested(int first, int second, int third);
int core_m19_cfg_statement_rhs(int left, int right);
int core_m19_cfg_initializer(int value);
int core_m19_list_empty_careful_shape(const struct core_m19_node *head);

int main(void) {
    struct core_m19_node empty;
    struct core_m19_node not_empty;
    struct core_m19_node other;
    int false_result;
    int false_calls;
    int true_result;
    int true_calls;

    false_result = core_m19_short_false();
    false_calls = core_m19_get_rhs_calls();
    true_result = core_m19_short_true();
    true_calls = core_m19_get_rhs_calls();

    empty.next = &empty;
    empty.prev = &empty;
    not_empty.next = &other;
    not_empty.prev = &not_empty;
    other.next = &empty;
    other.prev = &empty;

    printf("plain=%d,%d,%d nested=%d,%d\n",
           core_m19_plain(0, 9),
           core_m19_plain(3, 0),
           core_m19_plain(3, 9),
           core_m19_nested(1, 2, 3),
           core_m19_nested(1, 0, 3));
    printf("short=%d/%d,%d/%d\n", false_result, false_calls, true_result, true_calls);
    printf("cfg=%d,%d,%d\n",
           core_m19_cfg_statement_rhs(0, 0),
           core_m19_cfg_statement_rhs(1, 0),
           core_m19_cfg_statement_rhs(1, 7));
    printf("init=%d,%d\n", core_m19_cfg_initializer(0), core_m19_cfg_initializer(7));
    printf("list=%d,%d\n",
           core_m19_list_empty_careful_shape(&empty),
           core_m19_list_empty_careful_shape(&not_empty));
    return 0;
}
