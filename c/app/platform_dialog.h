#ifndef XLSONE_PLATFORM_DIALOG_H
#define XLSONE_PLATFORM_DIALOG_H

#include <stddef.h>

int xls_platform_open_files(char ***paths, size_t *path_count);
int xls_platform_save_file(char **path);
void xls_platform_free_paths(char **paths, size_t path_count);

#endif
