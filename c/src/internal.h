#ifndef XLSONE_INTERNAL_H
#define XLSONE_INTERNAL_H

#include "xlsone/xlsone.h"

#include <stddef.h>
#include <stdio.h>

char *xls_strdup(const char *text);
char *xls_strndup(const char *text, size_t length);
char *xls_trimdup(const char *text);
FILE *xls_fopen_utf8(const char *path, const char *mode);
void xls_set_error(xls_error *error, xls_error_code code, const char *format, ...);
int xls_multiply_size(size_t left, size_t right, size_t *result);
int xls_string_empty(const char *text);
int xls_string_contains_ascii_ci(const char *text, const char *needle);
int xls_string_has_non_ascii(const char *text);
int xls_string_all_ascii_digits(const char *text);
size_t xls_utf8_length(const char *text);
size_t xls_utf8_prefix_bytes(const char *text, size_t character_count);

#endif
