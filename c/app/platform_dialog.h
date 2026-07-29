#ifndef XLSONE_PLATFORM_DIALOG_H
#define XLSONE_PLATFORM_DIALOG_H

#include <stddef.h>
#include <stdint.h>

int xls_platform_open_files(char ***paths, size_t *path_count);
int xls_platform_save_file(char **path);
int xls_platform_open_license_file(char **path);
int xls_platform_save_rules_file(char **path);
int xls_platform_open_url(const char *url);
int xls_platform_http_request(
    const char *method,
    const char *url,
    const char *json_body,
    char **response_body,
    long *status_code
);
size_t xls_platform_device_components(
    char components[][256],
    size_t capacity
);
int xls_platform_host_name(char *buffer, size_t capacity);
int xls_platform_read_text_file(const char *path, char **contents);
int xls_platform_write_text_file(const char *path, const char *contents);
int xls_platform_read_license(char **contents, int64_t *last_seen_utc);
int xls_platform_write_license(const char *contents, int64_t last_seen_utc);
int xls_platform_read_rules(char **contents);
int xls_platform_write_rules(const char *contents);
void xls_platform_free_paths(char **paths, size_t path_count);

#endif
