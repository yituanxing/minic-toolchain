struct empty {
};

int main(void)
{
    static struct empty items[2];

    return &items[1] != &items[0];
}
