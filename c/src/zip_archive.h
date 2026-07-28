#ifndef XLSONE_ZIP_ARCHIVE_H
#define XLSONE_ZIP_ARCHIVE_H

#include "xlsone/xlsone.h"

#include <stddef.h>
#include <stdint.h>

typedef struct xls_zip_entry {
    char *name;
    uint16_t flags;
    uint16_t method;
    uint32_t compressed_size;
    uint32_t uncompressed_size;
    uint32_t local_header_offset;
} xls_zip_entry;

typedef struct xls_zip_archive {
    unsigned char *data;
    size_t size;
    xls_zip_entry *entries;
    size_t entry_count;
} xls_zip_archive;

int xls_zip_open(
    const char *path,
    xls_zip_archive *archive,
    xls_error *error
);
int xls_zip_contains(const xls_zip_archive *archive, const char *name);
int xls_zip_read(
    const xls_zip_archive *archive,
    const char *name,
    unsigned char **data,
    size_t *size,
    xls_error *error
);
void xls_zip_close(xls_zip_archive *archive);

#endif
