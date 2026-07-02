#ifndef ALSHPP_H
#define ALSHPP_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

char *alshpp_preprocess_file(const char *path);
bool alshpp_preprocess_to_file(const char *input_path, const char *output_path);
void alshpp_free_output(char *output);

#ifdef __cplusplus
}
#endif

#endif
