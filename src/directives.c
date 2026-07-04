#pragma once

#include "../include/alshpp.h"
#include "common_utils/file_utils.h"
#include "common_utils/path_utils.h"
#include "common_utils/simple_strings.h"

#include "utils.c"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


str apply_defines(const Preprocessor *pp, const char *line) {
    if (!pp || !line || pp->defines.count == 0) {
        return str_create(line ? line : "");
    }
    str current = str_create(line);
    for (size_t i = 0; i < pp->defines.count; i++) {
        if (pp->defines.items[i].name[0] == '\0') {
            continue;
        }
        str replaced = replace_text_token(cstr(&current), pp->defines.items[i].name,
                                          pp->defines.items[i].value);
        str_destroy(&current);
        current = replaced;
    }
    return current;
}


str apply_stdlib(const char *line) {
    load_std_function_list();
    str current = str_create(line ? line : "");
    for (size_t i = 0; i < STD_FUNCTION_COUNT; i++) {
        const char *name = cstr(&STD_FUNCTION_NAMES[i]);
        size_t name_len = strlen(name);
        str next = str_create("");
        const char *cursor = cstr(&current);
        while (*cursor) {
            const char *match = strstr(cursor, name);
            if (!match) {
                str_append(&next, cursor);
                break;
            }
            if (!token_boundary(cursor, match, name_len)) {
                size_t prefix_len = (size_t)(match - cursor + name_len);
                char *prefix = xstrndup(cursor, prefix_len);
                if (prefix) {
                    str_append(&next, prefix);
                    free(prefix);
                }
                cursor = match + name_len;
                continue;
            }
            const char *after = match + name_len;
            while (*after == ' ' || *after == '\t') {
                after++;
            }
            if (*after != '(') {
                size_t prefix_len = (size_t)(match - cursor + name_len);
                char *prefix = xstrndup(cursor, prefix_len);
                if (prefix) {
                    str_append(&next, prefix);
                    free(prefix);
                }
                cursor = match + name_len;
                continue;
            }
            if (is_preceded_by_std(match, cstr(&current))) {
                size_t prefix_len = (size_t)(match - cursor + name_len);
                char *prefix = xstrndup(cursor, prefix_len);
                if (prefix) {
                    str_append(&next, prefix);
                    free(prefix);
                }
                cursor = match + name_len;
                continue;
            }
            size_t prefix_len = (size_t)(match - cursor);
            if (prefix_len > 0) {
                char *prefix = xstrndup(cursor, prefix_len);
                if (prefix) {
                    str_append(&next, prefix);
                    free(prefix);
                }
            }
            str_append(&next, "std::");
            str_append(&next, name);
            cursor = match + name_len;
        }
        str_destroy(&current);
        current = next;
    }
    return current;
}

bool contains_noffi(const char *line) {
    return (strstr(line, "c::") != NULL) || (strstr(line, "ffi::") != NULL);
}

str apply_noffi(const char *line) {
    if (!line) {
        return str_create("");
    }
    if (contains_noffi(line)) {
        str out = str_create("// noffi disabled: ");
        str_append(&out, line);
        return out;
    }
    return str_create(line);
}