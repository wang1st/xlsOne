#ifndef XLSONE_UPDATE_CHECKER_H
#define XLSONE_UPDATE_CHECKER_H

#include <stddef.h>

typedef struct xls_update_info {
    char latest_version[64];
    char changelog[1024];
    char download_url[1024];
} xls_update_info;

int xls_update_compare_versions(const char *left, const char *right);
int xls_update_parse_response(
    const char *json,
    const char *platform_key,
    xls_update_info *info
);
const char *xls_update_platform_key(void);

#endif
