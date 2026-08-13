static const char *overlay_action_name(int action)
{
    static const char *const names[] = {
        "init",
        "pre-apply",
        "post-apply",
        "pre-remove",
        "post-remove",
    };

    return names[action];
}

int main(void)
{
    return overlay_action_name(2)[0] == 'p' ? 0 : 1;
}
