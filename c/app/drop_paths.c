#include "drop_paths.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static int ascii_equal_ci(char left, char right)
{
    return tolower((unsigned char)left)
        == tolower((unsigned char)right);
}

static int has_suffix_ci(const char *value, const char *suffix)
{
    size_t value_length;
    size_t suffix_length;
    size_t index;
    if (value == NULL || suffix == NULL) {
        return 0;
    }
    value_length = strlen(value);
    suffix_length = strlen(suffix);
    if (value_length < suffix_length) {
        return 0;
    }
    for (index = 0u; index < suffix_length; ++index) {
        if (!ascii_equal_ci(
            value[value_length - suffix_length + index],
            suffix[index]
        )) {
            return 0;
        }
    }
    return 1;
}

int xls_drop_path_is_workbook(const char *path)
{
    return has_suffix_ci(path, ".xlsx")
        || has_suffix_ci(path, ".xls");
}

static int hexadecimal_value(char character)
{
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

static void decode_uri_path(char *path)
{
    const char *source = path;
    char *target = path;
    while (*source != '\0') {
        if (source[0] == '%' && source[1] != '\0'
            && source[2] != '\0') {
            const int high = hexadecimal_value(source[1]);
            const int low = hexadecimal_value(source[2]);
            if (high >= 0 && low >= 0) {
                *target++ = (char)(high * 16 + low);
                source += 3;
                continue;
            }
        }
        *target++ = *source++;
    }
    *target = '\0';
}

static char *normalize_line(const char *start, size_t length)
{
    char *line;
    char *path;
    int uri = 0;
    while (length > 0u
        && isspace((unsigned char)*start) != 0) {
        ++start;
        --length;
    }
    while (length > 0u
        && isspace((unsigned char)start[length - 1u]) != 0) {
        --length;
    }
    if (length == 0u || *start == '#') {
        return NULL;
    }
    line = (char *)malloc(length + 3u);
    if (line == NULL) {
        return NULL;
    }
    memcpy(line, start, length);
    line[length] = '\0';
    path = line;
    if (length >= 7u
        && strncmp(line, "file://", 7u) == 0) {
        uri = 1;
        path += 7;
        if (strncmp(path, "localhost/", 10u) == 0) {
            path += 9;
        } else if (path[0] != '/') {
            const size_t path_length = strlen(path);
            memmove(line + 2, path, path_length + 1u);
            line[0] = '/';
            line[1] = '/';
            path = line;
        }
    }
    if (path != line) {
        memmove(line, path, strlen(path) + 1u);
    }
    if (uri) {
        decode_uri_path(line);
    }
#if defined(_WIN32)
    if (line[0] == '/' && isalpha((unsigned char)line[1]) != 0
        && line[2] == ':') {
        memmove(line, line + 1, strlen(line));
    }
#endif
    return line;
}

size_t xls_drop_paths_from_text(
    const char *text,
    xls_drop_path_callback callback,
    void *user_data
)
{
    const char *cursor = text;
    size_t count = 0u;
    if (text == NULL || callback == NULL) {
        return 0u;
    }
    while (*cursor != '\0') {
        const char *end = cursor;
        char *path;
        while (*end != '\0' && *end != '\r' && *end != '\n') {
            ++end;
        }
        path = normalize_line(cursor, (size_t)(end - cursor));
        if (path != NULL) {
            if (xls_drop_path_is_workbook(path)) {
                ++count;
                if (!callback(path, user_data)) {
                    free(path);
                    break;
                }
            }
            free(path);
        }
        cursor = end;
        while (*cursor == '\r' || *cursor == '\n') {
            ++cursor;
        }
    }
    return count;
}
