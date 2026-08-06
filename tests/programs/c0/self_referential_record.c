typedef struct Node {
    struct Node *next;
    int value;
} Node;

int main(void)
{
    Node first;
    Node second;
    Node *cursor = &first;

    cursor->next = &second;
    cursor->next->value = 41;
    return cursor->next->value;
}
