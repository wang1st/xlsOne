#include "internal.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *xls_strndup(const char *text, size_t length)
{
    char *copy;
    if (text == NULL) {
        text = "";
        length = 0;
    }
    copy = (char *)malloc(length + 1);
    if (copy == NULL) {
        return NULL;
    }
    if (length > 0) {
        memcpy(copy, text, length);
    }
    copy[length] = '\0';
    return copy;
}

char *xls_strdup(const char *text)
{
    return xls_strndup(text, text == NULL ? 0 : strlen(text));
}

char *xls_trimdup(const char *text)
{
    const char *begin;
    const char *end;
    if (text == NULL) {
        return xls_strdup("");
    }
    begin = text;
    while (*begin != '\0' && isspace((unsigned char)*begin)) {
        ++begin;
    }
    end = text + strlen(text);
    while (end > begin && isspace((unsigned char)end[-1])) {
        --end;
    }
    return xls_strndup(begin, (size_t)(end - begin));
}

void xls_error_clear(xls_error *error)
{
    if (error != NULL) {
        error->code = XLS_OK;
        error->message[0] = '\0';
    }
}

void xls_set_error(xls_error *error, xls_error_code code, const char *format, ...)
{
    va_list arguments;
    if (error == NULL) {
        return;
    }
    error->code = code;
    va_start(arguments, format);
    (void)vsnprintf(error->message, sizeof(error->message), format, arguments);
    va_end(arguments);
}

const char *xls_error_code_name(xls_error_code code)
{
    switch (code) {
    case XLS_OK:
        return "ok";
    case XLS_ERROR_ARGUMENT:
        return "argument";
    case XLS_ERROR_MEMORY:
        return "memory";
    case XLS_ERROR_IO:
        return "io";
    case XLS_ERROR_FORMAT:
        return "format";
    case XLS_ERROR_UNSUPPORTED:
        return "unsupported";
    case XLS_ERROR_INTERNAL:
        return "internal";
    }
    return "unknown";
}

int xls_multiply_size(size_t left, size_t right, size_t *result)
{
    if (result == NULL) {
        return 0;
    }
    if (left != 0 && right > SIZE_MAX / left) {
        return 0;
    }
    *result = left * right;
    return 1;
}

int xls_string_empty(const char *text)
{
    return text == NULL || text[0] == '\0';
}

int xls_string_contains_ascii_ci(const char *text, const char *needle)
{
    size_t needle_length;
    const char *cursor;
    if (text == NULL || needle == NULL || needle[0] == '\0') {
        return 0;
    }
    needle_length = strlen(needle);
    for (cursor = text; *cursor != '\0'; ++cursor) {
        size_t index;
        for (index = 0; index < needle_length; ++index) {
            unsigned char left = (unsigned char)cursor[index];
            unsigned char right = (unsigned char)needle[index];
            if (left == '\0' || tolower(left) != tolower(right)) {
                break;
            }
        }
        if (index == needle_length) {
            return 1;
        }
    }
    return 0;
}

int xls_string_has_non_ascii(const char *text)
{
    const unsigned char *cursor = (const unsigned char *)text;
    if (cursor == NULL) {
        return 0;
    }
    while (*cursor != '\0') {
        if (*cursor >= 0x80u) {
            return 1;
        }
        ++cursor;
    }
    return 0;
}

int xls_string_all_ascii_digits(const char *text)
{
    const unsigned char *cursor = (const unsigned char *)text;
    if (cursor == NULL || *cursor == '\0') {
        return 0;
    }
    while (*cursor != '\0') {
        if (!isdigit(*cursor)) {
            return 0;
        }
        ++cursor;
    }
    return 1;
}

size_t xls_utf8_length(const char *text)
{
    size_t length = 0;
    const unsigned char *cursor = (const unsigned char *)text;
    if (cursor == NULL) {
        return 0;
    }
    while (*cursor != '\0') {
        if ((*cursor & 0xc0u) != 0x80u) {
            ++length;
        }
        ++cursor;
    }
    return length;
}

size_t xls_utf8_prefix_bytes(const char *text, size_t character_count)
{
    size_t characters = 0;
    size_t bytes = 0;
    const unsigned char *cursor = (const unsigned char *)text;
    if (cursor == NULL) {
        return 0;
    }
    while (cursor[bytes] != '\0') {
        if ((cursor[bytes] & 0xc0u) != 0x80u) {
            if (characters == character_count) {
                break;
            }
            ++characters;
        }
        ++bytes;
    }
    return bytes;
}
