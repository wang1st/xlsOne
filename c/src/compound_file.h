#ifndef XLSONE_COMPOUND_FILE_H
#define XLSONE_COMPOUND_FILE_H

#include "xlsone/xlsone.h"

#include <stddef.h>

int xls_compound_read_workbook_stream(
    const char *path,
    unsigned char **stream,
    size_t *stream_size,
    xls_error *error
);

#endif
