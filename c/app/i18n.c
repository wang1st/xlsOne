#include "i18n.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct xls_i18n_entry {
    const char *source;
    const char *translation;
} xls_i18n_entry;

#include "i18n_catalog.inc"

static xls_ui_language current_language = XLS_UI_LANGUAGE_ZH_HANS;

static int language_code_equals(const char *left, const char *right)
{
    while (*left != '\0' && *right != '\0') {
        unsigned char left_character = (unsigned char)*left++;
        unsigned char right_character = (unsigned char)*right++;
        if (left_character == '_' || left_character == '-') {
            left_character = '-';
        } else {
            left_character = (unsigned char)tolower(left_character);
        }
        if (right_character == '_' || right_character == '-') {
            right_character = '-';
        } else {
            right_character = (unsigned char)tolower(right_character);
        }
        if (left_character != right_character) {
            return 0;
        }
    }
    return *left == '\0' && *right == '\0';
}

static int language_code_starts_with(
    const char *value,
    const char *prefix
)
{
    while (*prefix != '\0') {
        unsigned char value_character;
        unsigned char prefix_character;
        if (*value == '\0') {
            return 0;
        }
        value_character = (unsigned char)*value++;
        prefix_character = (unsigned char)*prefix++;
        if (value_character == '_') {
            value_character = '-';
        } else {
            value_character = (unsigned char)tolower(value_character);
        }
        if (prefix_character == '_') {
            prefix_character = '-';
        } else {
            prefix_character = (unsigned char)tolower(prefix_character);
        }
        if (value_character != prefix_character) {
            return 0;
        }
    }
    return 1;
}

int xls_i18n_parse_language(
    const char *code,
    xls_ui_language *language
)
{
    if (code == NULL || language == NULL) {
        return 0;
    }
    if (language_code_equals(code, "system")) {
        *language = XLS_UI_LANGUAGE_SYSTEM;
    } else if (language_code_equals(code, "en")) {
        *language = XLS_UI_LANGUAGE_ENGLISH;
    } else if (language_code_equals(code, "zh-Hans")
        || language_code_equals(code, "zh-CN")
        || language_code_equals(code, "zh-SG")) {
        *language = XLS_UI_LANGUAGE_ZH_HANS;
    } else if (language_code_equals(code, "zh-Hant")
        || language_code_equals(code, "zh-TW")
        || language_code_equals(code, "zh-HK")
        || language_code_equals(code, "zh-MO")) {
        *language = XLS_UI_LANGUAGE_ZH_HANT;
    } else if (language_code_equals(code, "ja")
        || language_code_starts_with(code, "ja-")) {
        *language = XLS_UI_LANGUAGE_JAPANESE;
    } else {
        return 0;
    }
    return 1;
}

const char *xls_i18n_language_code(xls_ui_language language)
{
    switch (language) {
    case XLS_UI_LANGUAGE_ENGLISH:
        return "en";
    case XLS_UI_LANGUAGE_ZH_HANS:
        return "zh-Hans";
    case XLS_UI_LANGUAGE_ZH_HANT:
        return "zh-Hant";
    case XLS_UI_LANGUAGE_JAPANESE:
        return "ja";
    case XLS_UI_LANGUAGE_SYSTEM:
    default:
        return "system";
    }
}

xls_ui_language xls_i18n_resolve_language(
    xls_ui_language preference,
    const char *system_locale
)
{
    if (preference != XLS_UI_LANGUAGE_SYSTEM) {
        return preference;
    }
    if (system_locale == NULL || system_locale[0] == '\0') {
        return XLS_UI_LANGUAGE_ENGLISH;
    }
    if (language_code_starts_with(system_locale, "ja")) {
        return XLS_UI_LANGUAGE_JAPANESE;
    }
    if (language_code_starts_with(system_locale, "zh-hant")
        || language_code_starts_with(system_locale, "zh-tw")
        || language_code_starts_with(system_locale, "zh-hk")
        || language_code_starts_with(system_locale, "zh-mo")) {
        return XLS_UI_LANGUAGE_ZH_HANT;
    }
    if (language_code_starts_with(system_locale, "zh")) {
        return XLS_UI_LANGUAGE_ZH_HANS;
    }
    return XLS_UI_LANGUAGE_ENGLISH;
}

void xls_i18n_set_language(xls_ui_language language)
{
    current_language = language == XLS_UI_LANGUAGE_SYSTEM
        ? XLS_UI_LANGUAGE_ENGLISH
        : language;
}

xls_ui_language xls_i18n_language(void)
{
    return current_language;
}

static const char *lookup_translation(
    const xls_i18n_entry *entries,
    size_t count,
    const char *source
)
{
    size_t lower = 0u;
    size_t upper = count;
    while (lower < upper) {
        const size_t middle = lower + (upper - lower) / 2u;
        const int comparison = strcmp(source, entries[middle].source);
        if (comparison == 0) {
            return entries[middle].translation;
        }
        if (comparison < 0) {
            upper = middle;
        } else {
            lower = middle + 1u;
        }
    }
    return source;
}

const char *xls_i18n_translate(const char *source)
{
    if (source == NULL || current_language == XLS_UI_LANGUAGE_ZH_HANS) {
        return source == NULL ? "" : source;
    }
    switch (current_language) {
    case XLS_UI_LANGUAGE_ENGLISH:
        return lookup_translation(
            xls_i18n_catalog_en,
            sizeof(xls_i18n_catalog_en) / sizeof(xls_i18n_catalog_en[0]),
            source
        );
    case XLS_UI_LANGUAGE_ZH_HANT:
        return lookup_translation(
            xls_i18n_catalog_zh_hant,
            sizeof(xls_i18n_catalog_zh_hant)
                / sizeof(xls_i18n_catalog_zh_hant[0]),
            source
        );
    case XLS_UI_LANGUAGE_JAPANESE:
        return lookup_translation(
            xls_i18n_catalog_ja,
            sizeof(xls_i18n_catalog_ja) / sizeof(xls_i18n_catalog_ja[0]),
            source
        );
    case XLS_UI_LANGUAGE_SYSTEM:
    case XLS_UI_LANGUAGE_ZH_HANS:
    default:
        return source;
    }
}

int xls_i18n_read_preference(
    const char *path,
    xls_ui_language *language
)
{
    FILE *file;
    char buffer[32];
    size_t length;
    int read_error;
    int close_error;
    if (path == NULL || language == NULL) {
        return 0;
    }
    file = fopen(path, "rb");
    if (file == NULL) {
        return 0;
    }
    length = fread(buffer, 1u, sizeof(buffer) - 1u, file);
    read_error = ferror(file);
    close_error = fclose(file);
    if (read_error != 0 || close_error != 0) {
        return 0;
    }
    buffer[length] = '\0';
    while (length > 0u
        && isspace((unsigned char)buffer[length - 1u]) != 0) {
        buffer[--length] = '\0';
    }
    return xls_i18n_parse_language(buffer, language);
}

int xls_i18n_write_preference(
    const char *path,
    xls_ui_language language
)
{
    FILE *file;
    char temporary[2048];
    const char *code = xls_i18n_language_code(language);
    int path_length;
    int result;
    if (path == NULL) {
        return 0;
    }
    path_length = snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    if (path_length < 0 || (size_t)path_length >= sizeof(temporary)) {
        return 0;
    }
    file = fopen(temporary, "wb");
    if (file == NULL) {
        return 0;
    }
    result = fprintf(file, "%s\n", code) > 0;
    result = fclose(file) == 0 && result;
    if (result) {
#if defined(_WIN32)
        (void)remove(path);
#endif
        result = rename(temporary, path) == 0;
    }
    if (!result) {
        (void)remove(temporary);
    }
    return result;
}
