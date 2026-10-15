static void (*nested_function_pointer_type_name(void *symbol,
                                                     void *handle,
                                                     const char *name))(void)
{
    void (*(*resolver)(void *, const char *))(void);

    resolver = (void (*(*)(void *, const char *))(void))symbol;
    return (*resolver)(handle, name);
}
