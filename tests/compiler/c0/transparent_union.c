struct page { int value; };
struct folio { int value; };
struct encoded_page { int value; };

typedef union {
    struct page **pages;
    struct folio **folios;
    struct encoded_page **encoded_pages;
} release_pages_arg __attribute__((__transparent_union__));

int release_pages(release_pages_arg arg, int nr)
{
    return arg.folios == ((void *)0) ? 0 : nr;
}

int call_page(struct page **pages)
{
    return release_pages(pages, 1);
}

int call_folio(struct folio **folios)
{
    return release_pages(folios, 2);
}

int call_encoded(struct encoded_page **encoded_pages)
{
    return release_pages(encoded_pages, 3);
}

int call_null(void)
{
    return release_pages(0, 4);
}

typedef int (*release_pages_fn)(release_pages_arg, int);

int call_indirect(release_pages_fn fn, struct folio **folios)
{
    return fn(folios, 5);
}
