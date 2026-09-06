typedef struct Vfs Vfs;

struct Vfs {
    void (*(*xDlSym)(Vfs *, void *, const char *))(void);
};

static void resolved_symbol(void)
{
}

static void (*resolve_symbol(Vfs *vfs, void *handle, const char *name))(void)
{
    (void)vfs;
    (void)handle;
    (void)name;
    return resolved_symbol;
}

int install_resolver(Vfs *vfs)
{
    vfs->xDlSym = resolve_symbol;
    return sizeof(vfs->xDlSym) == sizeof(void *) ? 0 : 1;
}
