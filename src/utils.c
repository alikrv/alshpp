#pragma once

#include "common_utils/file_utils.h"
#include "common_utils/path_utils.h"
#include "common_utils/simple_strings.h"

#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STD_LIST_PATH "alsh-std/raw_list.txt"

str *STD_FUNCTION_NAMES = NULL;
size_t STD_FUNCTION_COUNT = 0;

void load_std_function_list(void) {
    if (STD_FUNCTION_NAMES || STD_FUNCTION_COUNT > 0) {
        return;
    }

    str raw = read_entire_file(STD_LIST_PATH);
    if (raw.len == 0) {
        str_destroy(&raw);
        return;
    }

    size_t count = 0;
    str *items = str_split(&raw, "\n", &count);
    str_destroy(&raw);
    if (!items || count == 0) {
        return;
    }

    while (count > 0 && items[count - 1].len == 0) {
        str_destroy(&items[count - 1]);
        count--;
    }

    STD_FUNCTION_NAMES = items;
    STD_FUNCTION_COUNT = count;
}

char *xstrndup(const char *s, size_t n) {
    char *out = (char *)malloc(n + 1);
    if (!out) {
        return NULL;
    }
    memcpy(out, s, n);
    out[n] = '\0';
    return out;
}

bool is_identifier_char(char c) {
    return c == '_' || isalnum((unsigned char)c);
}

bool token_boundary(const char *start, const char *match, size_t match_len) {
    if (match != start && is_identifier_char(*(match - 1))) {
        return false;
    }
    const char *after = match + match_len;
    if (*after && is_identifier_char(*after)) {
        return false;
    }
    return true;
}


str replace_text_token(const char *line, const char *token, const char *replacement) {
    str result = str_create("");
    if (!line || !token || !replacement) {
        return result;
    }
    size_t token_len = strlen(token);
    const char *cursor = line;
    while (*cursor) {
        const char *match = strstr(cursor, token);
        if (!match) {
            str_append(&result, cursor);
            break;
        }
        size_t prefix_len = (size_t)(match - cursor);
        if (prefix_len > 0) {
            char *prefix = xstrndup(cursor, prefix_len);
            if (prefix) {
                str_append(&result, prefix);
                free(prefix);
            }
        }
        if (token_boundary(line, match, token_len)) {
            str_append(&result, replacement);
        } else {
            str_append(&result, token);
        }
        cursor = match + token_len;
    }
    return result;
}

bool is_preceded_by_std(const char *match, const char *line) {
    size_t prefix_len = 5;
    if (match - line < (ptrdiff_t)prefix_len) {
        return false;
    }
    return memcmp(match - prefix_len, "std::", prefix_len) == 0;
}