#include "update_checker.h"

#include "cJSON.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static void copy_text(char *destination, size_t capacity, const char *source)
{
    if (capacity == 0u) {
        return;
    }
    (void)snprintf(
        destination,
        capacity,
        "%s",
        source == NULL ? "" : source
    );
}

static const char *skip_version_prefix(const char *version)
{
    const unsigned char *cursor = (const unsigned char *)version;
    while (*cursor != '\0' && isspace(*cursor) != 0) {
        ++cursor;
    }
    if (*cursor == (unsigned char)'v'
        || *cursor == (unsigned char)'V') {
        ++cursor;
    }
    return (const char *)cursor;
}

int xls_update_compare_versions(const char *left, const char *right)
{
    const char *left_cursor;
    const char *right_cursor;
    if (left == NULL || right == NULL) {
        return 0;
    }
    left_cursor = skip_version_prefix(left);
    right_cursor = skip_version_prefix(right);
    for (;;) {
        unsigned long left_part = 0u;
        unsigned long right_part = 0u;
        int left_has_digits = 0;
        int right_has_digits = 0;
        while (isdigit((unsigned char)*left_cursor) != 0) {
            left_has_digits = 1;
            left_part = left_part * 10u
                + (unsigned long)(*left_cursor - '0');
            ++left_cursor;
        }
        while (isdigit((unsigned char)*right_cursor) != 0) {
            right_has_digits = 1;
            right_part = right_part * 10u
                + (unsigned long)(*right_cursor - '0');
            ++right_cursor;
        }
        if (left_part != right_part) {
            return left_part > right_part ? 1 : -1;
        }
        if (!left_has_digits && !right_has_digits) {
            break;
        }
        if (*left_cursor == '.') {
            ++left_cursor;
        }
        if (*right_cursor == '.') {
            ++right_cursor;
        }
        if ((*left_cursor == '\0' || *left_cursor == '-'
                || *left_cursor == '+')
            && (*right_cursor == '\0' || *right_cursor == '-'
                || *right_cursor == '+')) {
            break;
        }
    }
    if (*left_cursor == '-' && *right_cursor != '-') {
        return -1;
    }
    if (*right_cursor == '-' && *left_cursor != '-') {
        return 1;
    }
    return 0;
}

int xls_update_parse_response(
    const char *json,
    const char *platform_key,
    xls_update_info *info
)
{
    cJSON *root;
    cJSON *version;
    cJSON *changelog;
    cJSON *downloads;
    cJSON *download;
    const char *parse_end = NULL;
    if (json == NULL || platform_key == NULL || info == NULL) {
        return 0;
    }
    memset(info, 0, sizeof(*info));
    root = cJSON_ParseWithLengthOpts(
        json, strlen(json) + 1u, &parse_end, 1
    );
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return 0;
    }
    version = cJSON_GetObjectItemCaseSensitive(
        root, "latest_version"
    );
    changelog = cJSON_GetObjectItemCaseSensitive(root, "changelog");
    downloads = cJSON_GetObjectItemCaseSensitive(root, "downloads");
    download = cJSON_IsObject(downloads)
        ? cJSON_GetObjectItemCaseSensitive(downloads, platform_key)
        : NULL;
    if (!cJSON_IsString(version)
        || version->valuestring == NULL
        || version->valuestring[0] == '\0'
        || !cJSON_IsString(download)
        || download->valuestring == NULL
        || download->valuestring[0] == '\0') {
        cJSON_Delete(root);
        return 0;
    }
    copy_text(
        info->latest_version,
        sizeof(info->latest_version),
        version->valuestring
    );
    if (cJSON_IsString(changelog)
        && changelog->valuestring != NULL) {
        copy_text(
            info->changelog,
            sizeof(info->changelog),
            changelog->valuestring
        );
    }
    copy_text(
        info->download_url,
        sizeof(info->download_url),
        download->valuestring
    );
    cJSON_Delete(root);
    return 1;
}

const char *xls_update_platform_key(void)
{
#if defined(_WIN32)
    return "windows_amd64";
#elif defined(__APPLE__)
    return "macos";
#elif defined(__aarch64__) || defined(__arm64__)
    return "linux_arm64";
#else
    return "linux_amd64";
#endif
}
