struct Box {
    int value;
};

int write_box(const struct Box *box)
{
    box->value = 3;
    return 0;
}

int main(void)
{
    struct Box box;

    return write_box(&box);
}
