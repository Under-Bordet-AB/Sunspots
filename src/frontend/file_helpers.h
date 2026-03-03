#ifndef FILE_HELPERS_H
#define FILE_HELPERS_H

#include <stdlib.h>

char* load_file(const char* path, size_t* out_size);
int sanitize_path(const char* url_path, char* out_path, size_t out_size);

#endif // FILE_HELPERS_H