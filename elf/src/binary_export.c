#include "minielf.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

static void set_error(MiniElfBinaryError *error_out,
                      MiniElfBinaryError error) {
    if (error_out != NULL) {
        *error_out = error;
    }
}

const char *minielf_binary_error_string(MiniElfBinaryError error) {
    switch (error) {
    case MINIELF_BINARY_OK:
        return "ok";
    case MINIELF_BINARY_INVALID_ARGUMENT:
        return "invalid-argument";
    case MINIELF_BINARY_INVALID_SECTION:
        return "invalid-section";
    case MINIELF_BINARY_NO_LOADABLE_SECTIONS:
        return "no-loadable-sections";
    case MINIELF_BINARY_LIMIT:
        return "format-limit";
    case MINIELF_BINARY_OUT_OF_MEMORY:
        return "out-of-memory";
    }
    return "unknown";
}

bool minielf_build_binary(const MiniElfView *view,
                          const bool *include_sections,
                          unsigned char **image_out,
                          size_t *size_out,
                          uint64_t *base_address_out,
                          MiniElfBinaryError *error_out) {
    uint64_t minimum = UINT64_MAX;
    uint64_t maximum = 0U;
    unsigned char *image = NULL;
    size_t i;
    bool have_section = false;

    if (error_out != NULL) {
        *error_out = MINIELF_BINARY_OK;
    }
    if (view == NULL || include_sections == NULL ||
        image_out == NULL || size_out == NULL) {
        set_error(error_out, MINIELF_BINARY_INVALID_ARGUMENT);
        return false;
    }
    *image_out = NULL;
    *size_out = 0U;
    if (base_address_out != NULL) {
        *base_address_out = 0U;
    }

    for (i = 1U; i < view->section_count; ++i) {
        MiniElfSection section;
        uint64_t address;
        uint64_t end;

        if (!include_sections[i]) {
            continue;
        }
        if (!minielf_section(view, i, &section) ||
            !minielf_section_load_address(view, &section, &address)) {
            set_error(error_out, MINIELF_BINARY_INVALID_SECTION);
            return false;
        }
        if (section.size == 0U) {
            continue;
        }
        if (section.offset > SIZE_MAX || section.size > SIZE_MAX ||
            section.type == 8U) {
            set_error(error_out, MINIELF_BINARY_INVALID_SECTION);
            return false;
        }
        {
            const unsigned char *data;
            size_t size;

            if (!minielf_section_data(view, i, &data, &size) ||
                size != (size_t)section.size) {
                set_error(error_out, MINIELF_BINARY_INVALID_SECTION);
                return false;
            }
            (void)data;
        }
        if (section.size > UINT64_MAX - address) {
            set_error(error_out, MINIELF_BINARY_LIMIT);
            return false;
        }
        end = address + section.size;
        if (address < minimum) {
            minimum = address;
        }
        if (end > maximum) {
            maximum = end;
        }
        have_section = true;
    }

    if (!have_section) {
        set_error(error_out, MINIELF_BINARY_NO_LOADABLE_SECTIONS);
        return false;
    }
    if (maximum < minimum || maximum - minimum > SIZE_MAX) {
        set_error(error_out, MINIELF_BINARY_LIMIT);
        return false;
    }

    image = calloc((size_t)(maximum - minimum), 1U);
    if (image == NULL) {
        set_error(error_out, MINIELF_BINARY_OUT_OF_MEMORY);
        return false;
    }

    for (i = 1U; i < view->section_count; ++i) {
        MiniElfSection section;
        const unsigned char *data;
        size_t data_size;
        uint64_t address;
        uint64_t relative;

        if (!include_sections[i]) {
            continue;
        }
        if (!minielf_section(view, i, &section) || section.size == 0U) {
            continue;
        }
        if (!minielf_section_load_address(view, &section, &address) ||
            address < minimum ||
            !minielf_section_data(view, i, &data, &data_size)) {
            free(image);
            set_error(error_out, MINIELF_BINARY_INVALID_SECTION);
            return false;
        }
        relative = address - minimum;
        if (relative > SIZE_MAX ||
            data_size > (size_t)(maximum - minimum) - (size_t)relative) {
            free(image);
            set_error(error_out, MINIELF_BINARY_LIMIT);
            return false;
        }
        memcpy(image + (size_t)relative, data, data_size);
    }

    *image_out = image;
    *size_out = (size_t)(maximum - minimum);
    if (base_address_out != NULL) {
        *base_address_out = minimum;
    }
    return true;
}
