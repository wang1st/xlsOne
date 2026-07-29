#ifndef XLSONE_DROP_PATHS_H
#define XLSONE_DROP_PATHS_H

#include <stddef.h>

typedef int (*xls_drop_path_callback)(
    const char *path,
    void *user_data
);

int xls_drop_path_is_workbook(const char *path);
size_t xls_drop_paths_from_text(
    const char *text,
    xls_drop_path_callback callback,
    void *user_data
);

#endif
