static void (*nested_function_pointer_type_name(void *symbol,
                                                     void *handle,
                                                     const char *name))(void)
{
    void (*(*resolver)(void *, const char *))(void);

    resolver = (void (*(*)(void *, const char *))(void))symbol;
    return (*resolver)(handle, name);
}

/* Keep the nested type-name path reachable under the Common dead-static
 * elimination pass. The helper itself is free to be inlined or specialized. */
void (*resolve_nested_function_pointer(void *symbol,
                                       void *handle,
                                       const char *name))(void)
{
    return nested_function_pointer_type_name(symbol, handle, name);
}
