#ifndef ALSHPP_H
#define ALSHPP_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

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


char *alshpp_preprocess_file(const char *path);
bool alshpp_preprocess_to_file(const char *input_path, const char *output_path);
void alshpp_free_output(char *output);

#ifdef __cplusplus
}
#endif

#endif
