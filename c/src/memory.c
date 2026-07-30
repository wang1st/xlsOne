#include "internal.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#include <wchar.h>

static wchar_t *xls_windows_wide_copy(const wchar_t *text)
{
    const size_t length = wcslen(text);
    wchar_t *copy = (wchar_t *)malloc(
        (length + 1u) * sizeof(*copy)
    );
    if (copy != NULL) {
        memcpy(copy, text, (length + 1u) * sizeof(*copy));
    }
    return copy;
}

static wchar_t *xls_windows_extended_path(const wchar_t *path)
{
    static const wchar_t device_prefix[] = L"\\\\?\\";
    static const wchar_t unc_prefix[] = L"\\\\?\\UNC\\";
    DWORD required;
    DWORD written;
    wchar_t *full;
    wchar_t *extended;
    size_t full_length;
    size_t index;
    if (wcsncmp(path, L"\\\\?\\", 4u) == 0
        || wcsncmp(path, L"\\\\.\\", 4u) == 0) {
        return xls_windows_wide_copy(path);
    }
    required = GetFullPathNameW(path, 0u, NULL, NULL);
    if (required == 0u) {
        return xls_windows_wide_copy(path);
    }
    full = (wchar_t *)malloc((size_t)required * sizeof(*full));
    if (full == NULL) {
        return NULL;
    }
    written = GetFullPathNameW(path, required, full, NULL);
    if (written == 0u || written >= required) {
        free(full);
        return xls_windows_wide_copy(path);
    }
    for (index = 0u; full[index] != L'\0'; ++index) {
        if (full[index] == L'/') {
            full[index] = L'\\';
        }
    }
    full_length = wcslen(full);
    if (full_length >= 2u
        && full[0] == L'\\'
        && full[1] == L'\\') {
        const size_t prefix_length =
            (sizeof(unc_prefix) / sizeof(unc_prefix[0])) - 1u;
        extended = (wchar_t *)malloc(
            (prefix_length + full_length - 2u + 1u)
                * sizeof(*extended)
        );
        if (extended != NULL) {
            memcpy(
                extended,
                unc_prefix,
                prefix_length * sizeof(*extended)
            );
            memcpy(
                extended + prefix_length,
                full + 2u,
                (full_length - 2u + 1u) * sizeof(*extended)
            );
        }
    } else if (full_length >= 3u
        && full[1] == L':'
        && full[2] == L'\\') {
        const size_t prefix_length =
            (sizeof(device_prefix) / sizeof(device_prefix[0])) - 1u;
        extended = (wchar_t *)malloc(
            (prefix_length + full_length + 1u)
                * sizeof(*extended)
        );
        if (extended != NULL) {
            memcpy(
                extended,
                device_prefix,
                prefix_length * sizeof(*extended)
            );
            memcpy(
                extended + prefix_length,
                full,
                (full_length + 1u) * sizeof(*extended)
            );
        }
    } else {
        extended = xls_windows_wide_copy(full);
    }
    free(full);
    return extended;
}
#endif

FILE *xls_fopen_utf8(const char *path, const char *mode)
{
#if defined(_WIN32)
    int path_length;
    int mode_length;
    wchar_t *wide_path;
    wchar_t *wide_mode;
    wchar_t *extended_path;
    FILE *file;
    if (path == NULL || mode == NULL) {
        return NULL;
    }
    path_length = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0
    );
    mode_length = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, mode, -1, NULL, 0
    );
    if (path_length <= 0 || mode_length <= 0) {
        return NULL;
    }
    wide_path = (wchar_t *)malloc(
        (size_t)path_length * sizeof(*wide_path)
    );
    wide_mode = (wchar_t *)malloc(
        (size_t)mode_length * sizeof(*wide_mode)
    );
    if (wide_path == NULL || wide_mode == NULL) {
        free(wide_path);
        free(wide_mode);
        return NULL;
    }
    if (MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS,
            path, -1, wide_path, path_length
        ) <= 0
        || MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS,
            mode, -1, wide_mode, mode_length
        ) <= 0) {
        free(wide_path);
        free(wide_mode);
        return NULL;
    }
    extended_path = xls_windows_extended_path(wide_path);
    file = _wfopen(
        extended_path == NULL ? wide_path : extended_path,
        wide_mode
    );
    free(extended_path);
    free(wide_path);
    free(wide_mode);
    return file;
#else
    return fopen(path, mode);
#endif
}

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
