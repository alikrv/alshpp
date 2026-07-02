#include "../include/alshpp.h"
#include "common_utils/file_utils.h"
#include "common_utils/path_utils.h"
#include "common_utils/simple_strings.h"

#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STD_LIST_PATH "alsh-std/raw_list.txt"
static str *STD_FUNCTION_NAMES = NULL;
static size_t STD_FUNCTION_COUNT = 0;

static void load_std_function_list(void) {
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

typedef struct {
    char *name;
    char *value;
} DefineEntry;

typedef struct {
    DefineEntry *items;
    size_t count;
    size_t cap;
} DefineList;

typedef struct {
    char **paths;
    size_t count;
    size_t cap;
} PathSet;

typedef struct {
    DefineList defines;
    PathSet imported;
    bool noffi;
    bool stdlib;
} Preprocessor;

static char *xstrndup(const char *s, size_t n) {
    char *out = (char *)malloc(n + 1);
    if (!out) {
        return NULL;
    }
    memcpy(out, s, n);
    out[n] = '\0';
    return out;
}

static bool is_identifier_char(char c) {
    return c == '_' || isalnum((unsigned char)c);
}

static bool token_boundary(const char *start, const char *match, size_t match_len) {
    if (match != start && is_identifier_char(*(match - 1))) {
        return false;
    }
    const char *after = match + match_len;
    if (*after && is_identifier_char(*after)) {
        return false;
    }
    return true;
}

static void free_define_list(DefineList *list) {
    if (!list) {
        return;
    }
    for (size_t i = 0; i < list->count; i++) {
        free(list->items[i].name);
        free(list->items[i].value);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->cap = 0;
}

static void free_path_set(PathSet *set) {
    if (!set) {
        return;
    }
    for (size_t i = 0; i < set->count; i++) {
        free(set->paths[i]);
    }
    free(set->paths);
    set->paths = NULL;
    set->count = 0;
    set->cap = 0;
}

static void add_define(Preprocessor *pp, const char *name, const char *value) {
    if (!pp || !name) {
        return;
    }
    for (size_t i = 0; i < pp->defines.count; i++) {
        if (strcmp(pp->defines.items[i].name, name) == 0) {
            free(pp->defines.items[i].value);
            pp->defines.items[i].value = strdup(value ? value : "");
            return;
        }
    }
    if (pp->defines.count + 1 > pp->defines.cap) {
        size_t next = pp->defines.cap ? pp->defines.cap * 2 : 8;
        DefineEntry *next_items = (DefineEntry *)realloc(pp->defines.items, next * sizeof(DefineEntry));
        if (!next_items) {
            return;
        }
        pp->defines.items = next_items;
        pp->defines.cap = next;
    }
    pp->defines.items[pp->defines.count].name = strdup(name);
    pp->defines.items[pp->defines.count].value = strdup(value ? value : "");
    pp->defines.count += 1;
}

static void remove_define(Preprocessor *pp, const char *name) {
    if (!pp || !name) {
        return;
    }
    for (size_t i = 0; i < pp->defines.count; i++) {
        if (strcmp(pp->defines.items[i].name, name) == 0) {
            free(pp->defines.items[i].name);
            free(pp->defines.items[i].value);
            memmove(&pp->defines.items[i], &pp->defines.items[i + 1],
                    (pp->defines.count - i - 1) * sizeof(DefineEntry));
            pp->defines.count -= 1;
            return;
        }
    }
}

static bool import_path_exists(const Preprocessor *pp, const char *path) {
    if (!pp || !path) {
        return false;
    }
    for (size_t i = 0; i < pp->imported.count; i++) {
        if (strcmp(pp->imported.paths[i], path) == 0) {
            return true;
        }
    }
    return false;
}

static void add_import_path(Preprocessor *pp, const char *path) {
    if (!pp || !path || import_path_exists(pp, path)) {
        return;
    }
    if (pp->imported.count + 1 > pp->imported.cap) {
        size_t next = pp->imported.cap ? pp->imported.cap * 2 : 8;
        char **next_paths = (char **)realloc(pp->imported.paths, next * sizeof(char *));
        if (!next_paths) {
            return;
        }
        pp->imported.paths = next_paths;
        pp->imported.cap = next;
    }
    pp->imported.paths[pp->imported.count++] = strdup(path);
}

static str normalize_path_for_include(const char *base_dir, const char *path) {
    if (!path) {
        return NULL_STRING;
    }
    if (path[0] == '/') {
        str real = canonical_path(path);
        return real.len ? real : str_create(path);
    }
    str joined = path_join(base_dir, path);
    str real = canonical_path(cstr(&joined));
    str_destroy(&joined);
    if (real.len == 0) {
        return str_create(path);
    }
    return real;
}

static str get_directory(const char *path) {
    if (!path) {
        return str_create(".");
    }
    const char *last_slash = strrchr(path, '/');
    if (!last_slash || last_slash == path) {
        return str_create(".");
    }
    size_t len = (size_t)(last_slash - path);
    char *buffer = xstrndup(path, len);
    if (!buffer) {
        return str_create(".");
    }
    str result = str_create(buffer);
    free(buffer);
    return result;
}

static bool starts_with(const char *line, const char *prefix) {
    while (*prefix) {
        if (*line++ != *prefix++) {
            return false;
        }
    }
    return true;
}

static char *trim_inplace(char *text) {
    if (!text) {
        return NULL;
    }
    char *start = text;
    while (*start && isspace((unsigned char)*start)) {
        start++;
    }
    char *end = start + strlen(start);
    while (end > start && isspace((unsigned char)*(end - 1))) {
        end--;
    }
    *end = '\0';
    if (start != text) {
        memmove(text, start, end - start + 1);
    }
    return text;
}

static bool parse_directive_path(const char *line, const char *keyword, char **out_path) {
    if (!line || !keyword || !out_path) {
        return false;
    }
    const char *cursor = line + strlen(keyword);
    while (*cursor && isspace((unsigned char)*cursor)) {
        cursor++;
    }
    if (*cursor == '"' || *cursor == '<') {
        char open = *cursor++;
        char close = (open == '"') ? '"' : '>';
        const char *start = cursor;
        while (*cursor && *cursor != close) {
            cursor++;
        }
        if (*cursor != close) {
            return false;
        }
        size_t len = (size_t)(cursor - start);
        *out_path = xstrndup(start, len);
        return *out_path != NULL;
    }
    if (*cursor) {
        const char *start = cursor;
        while (*cursor && !isspace((unsigned char)*cursor)) {
            cursor++;
        }
        size_t len = (size_t)(cursor - start);
        *out_path = xstrndup(start, len);
        return *out_path != NULL;
    }
    return false;
}

static str replace_text_token(const char *line, const char *token, const char *replacement) {
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

static str apply_defines(const Preprocessor *pp, const char *line) {
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

static bool is_preceded_by_std(const char *match, const char *line) {
    size_t prefix_len = 5;
    if (match - line < (ptrdiff_t)prefix_len) {
        return false;
    }
    return memcmp(match - prefix_len, "std::", prefix_len) == 0;
}

static str apply_stdlib(const char *line) {
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

static bool contains_noffi(const char *line) {
    return (strstr(line, "c::") != NULL) || (strstr(line, "ffi::") != NULL);
}

static str apply_noffi(const char *line) {
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

static str preprocess_file_internal(const char *file_path, const char *base_dir, Preprocessor *pp, bool import_only);

static str extract_exported_declarations(const str *content) {
    str output = str_create("");
    if (!content) {
        return output;
    }
    const char *cursor = cstr(content);
    bool inside_function = false;
    int brace_depth = 0;
    bool keep_next_line = false;

    while (*cursor) {
        const char *line_start = cursor;
        const char *newline = strchr(cursor, '\n');
        size_t line_len = newline ? (size_t)(newline - cursor) : strlen(cursor);
        char *line_buf = xstrndup(line_start, line_len);
        if (!line_buf) {
            break;
        }
        cursor = newline ? newline + 1 : cursor + line_len;
        char *trimmed = trim_inplace(line_buf);
        bool should_keep = false;

        if (keep_next_line) {
            should_keep = true;
            keep_next_line = false;
        }

        if (!inside_function) {
            if (trimmed[0] == '!') {
                if (starts_with(trimmed, "!global")) {
                    should_keep = true;
                    if (strcmp(trimmed, "!global") == 0) {
                        keep_next_line = true;
                    }
                }
            } else if (starts_with(trimmed, "function ") || starts_with(trimmed, "fn ")) {
                should_keep = true;
                inside_function = true;
                brace_depth = 0;
                for (size_t i = 0; i < line_len; i++) {
                    if (line_buf[i] == '{') {
                        brace_depth += 1;
                    } else if (line_buf[i] == '}') {
                        brace_depth -= 1;
                    }
                }
                if (brace_depth <= 0) {
                    inside_function = false;
                }
            }
        } else {
            should_keep = true;
            for (size_t i = 0; i < line_len; i++) {
                if (line_buf[i] == '{') {
                    brace_depth += 1;
                } else if (line_buf[i] == '}') {
                    brace_depth -= 1;
                }
            }
            if (brace_depth <= 0) {
                inside_function = false;
            }
        }

        if (should_keep) {
            str_append(&output, line_buf);
            if (newline) {
                str_append(&output, "\n");
            }
        }
        free(line_buf);
    }
    return output;
}

static str preprocess_file_internal(const char *file_path, const char *base_dir, Preprocessor *pp, bool import_only) {
    if (!file_path || !pp) {
        return str_create("");
    }
    str resolved = normalize_path_for_include(base_dir, file_path);
    str input_dir = get_directory(cstr(&resolved));
    str output = str_create("");

    if (!file_exists(cstr(&resolved))) {
        fprintf(stderr, "alshpp: file not found: %s\n", cstr(&resolved));
        str_destroy(&resolved);
        str_destroy(&input_dir);
        return output;
    }

    str raw = read_entire_file(cstr(&resolved));
    if (raw.len == 0 && raw.data && raw.data[0] == '\0' && !file_exists(cstr(&resolved))) {
        fprintf(stderr, "alshpp: failed to read file: %s\n", cstr(&resolved));
        str_destroy(&resolved);
        str_destroy(&input_dir);
        str_destroy(&raw);
        return output;
    }

    const char *cursor = cstr(&raw);
    while (*cursor) {
        const char *line_start = cursor;
        const char *newline = strchr(cursor, '\n');
        size_t line_len = newline ? (size_t)(newline - cursor) : strlen(cursor);
        char *line_buf = xstrndup(line_start, line_len);
        if (!line_buf) {
            break;
        }
        cursor = newline ? newline + 1 : cursor + line_len;
        char *trimmed = trim_inplace(line_buf);

        if (starts_with(trimmed, "@define")) {
            const char *arg = trimmed + strlen("@define");
            while (*arg && isspace((unsigned char)*arg)) {
                arg++;
            }
            const char *name_start = arg;
            while (*arg && !isspace((unsigned char)*arg)) {
                arg++;
            }
            char *name = xstrndup(name_start, (size_t)(arg - name_start));
            while (*arg && isspace((unsigned char)*arg)) {
                arg++;
            }
            char *value = strdup(arg ? arg : "");
            if (name) {
                add_define(pp, name, value);
                free(name);
            }
            free(value);
            free(line_buf);
            continue;
        }

        if (starts_with(trimmed, "@undefine")) {
            const char *arg = trimmed + strlen("@undefine");
            while (*arg && isspace((unsigned char)*arg)) {
                arg++;
            }
            char *name = strdup(arg);
            if (name) {
                trim_inplace(name);
                remove_define(pp, name);
                free(name);
            }
            free(line_buf);
            continue;
        }

        if (starts_with(trimmed, "@include")) {
            char *include_path = NULL;
            if (parse_directive_path(trimmed, "@include", &include_path)) {
                str included = preprocess_file_internal(include_path, cstr(&input_dir), pp, false);
                str_append(&output, cstr(&included));
                str_destroy(&included);
                free(include_path);
            }
            free(line_buf);
            continue;
        }

        if (starts_with(trimmed, "@import")) {
            char *import_path = NULL;
            if (parse_directive_path(trimmed, "@import", &import_path)) {
                str resolved_import = normalize_path_for_include(cstr(&input_dir), import_path);
                if (!import_path_exists(pp, cstr(&resolved_import))) {
                    add_import_path(pp, cstr(&resolved_import));
                    Preprocessor local = *pp;
                    local.defines.items = NULL;
                    local.defines.count = 0;
                    local.defines.cap = 0;
                    local.noffi = false;
                    local.stdlib = false;
                    str import_dir = get_directory(cstr(&resolved_import));
                    str imported_full = preprocess_file_internal(cstr(&resolved_import), cstr(&import_dir), &local, false);
                    str_destroy(&import_dir);
                    str exported = extract_exported_declarations(&imported_full);
                    str_append(&output, cstr(&exported));
                    str_destroy(&imported_full);
                    str_destroy(&exported);
                }
                str_destroy(&resolved_import);
                free(import_path);
            }
            free(line_buf);
            continue;
        }

        if (starts_with(trimmed, "@stdlib")) {
            pp->stdlib = true;
            free(line_buf);
            continue;
        }

        if (starts_with(trimmed, "@noffi")) {
            pp->noffi = true;
            free(line_buf);
            continue;
        }

        if (starts_with(trimmed, "@main") || starts_with(trimmed, "@justrunit") || starts_with(trimmed, "@justcarryon")) {
            free(line_buf);
            continue;
        }

        if (!import_only) {
            str transformed = apply_defines(pp, line_buf);
            str transformed2 = transformed;
            if (pp->stdlib) {
                str std_transformed = apply_stdlib(cstr(&transformed2));
                str_destroy(&transformed2);
                transformed2 = std_transformed;
            }
            if (pp->noffi) {
                str noffi_transformed = apply_noffi(cstr(&transformed2));
                str_destroy(&transformed2);
                transformed2 = noffi_transformed;
            }
            str_append(&output, cstr(&transformed2));
            str_destroy(&transformed2);
        }

        if (!import_only && newline) {
            str_append(&output, "\n");
        }
        free(line_buf);
    }

    str_destroy(&raw);
    str_destroy(&resolved);
    str_destroy(&input_dir);
    return output;
}

char *alshpp_preprocess_file(const char *path) {
    if (!path) {
        return NULL;
    }
    Preprocessor pp = {0};
    str output = preprocess_file_internal(path, ".", &pp, false);
    free_define_list(&pp.defines);
    free_path_set(&pp.imported);
    if (output.len == 0 && output.data && output.data[0] == '\0') {
        return strdup("");
    }
    char *result = strdup(cstr(&output));
    str_destroy(&output);
    return result;
}

bool alshpp_preprocess_to_file(const char *input_path, const char *output_path) {
    if (!input_path || !output_path) {
        return false;
    }
    char *preprocessed = alshpp_preprocess_file(input_path);
    if (!preprocessed) {
        return false;
    }
    write_entire_file(output_path, preprocessed);
    free(preprocessed);
    return true;
}

void alshpp_free_output(char *output) {
    if (output) {
        free(output);
    }
}
