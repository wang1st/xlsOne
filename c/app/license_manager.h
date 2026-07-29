#ifndef XLSONE_LICENSE_MANAGER_H
#define XLSONE_LICENSE_MANAGER_H

#include <stddef.h>
#include <stdint.h>

typedef enum xls_license_state {
    XLS_LICENSE_UNACTIVATED,
    XLS_LICENSE_ACTIVATED,
    XLS_LICENSE_EXPIRED,
    XLS_LICENSE_TRIAL
} xls_license_state;

typedef enum xls_license_plan {
    XLS_LICENSE_PLAN_TRIAL,
    XLS_LICENSE_PLAN_PERSONAL_LIFETIME,
    XLS_LICENSE_PLAN_PERSONAL_YEARLY,
    XLS_LICENSE_PLAN_ENTERPRISE
} xls_license_plan;

typedef struct xls_license_info {
    char key_id[80];
    char device_hash[129];
    xls_license_plan plan;
    int64_t issued_at;
    int64_t expires_at;
} xls_license_info;

typedef struct xls_license_manager {
    xls_license_state state;
    xls_license_info info;
    char device_fingerprint[65];
    char component_hashes[4][65];
    size_t component_count;
    int busy;
} xls_license_manager;

void xls_license_manager_init(xls_license_manager *manager);
int xls_license_is_full(const xls_license_manager *manager);
int xls_license_remaining_days(const xls_license_manager *manager);
int xls_license_grace_days(const xls_license_manager *manager);
int xls_license_max_import_files(const xls_license_manager *manager);
const char *xls_license_watermark(const xls_license_manager *manager);
const char *xls_license_state_text(
    const xls_license_manager *manager,
    char *buffer,
    size_t capacity
);
const char *xls_license_plan_text(xls_license_plan plan);

int xls_license_normalize_key(
    const char *input,
    char *output,
    size_t capacity
);
int xls_license_activate(
    xls_license_manager *manager,
    const char *key,
    char *message,
    size_t message_capacity
);
int xls_license_request_trial(
    xls_license_manager *manager,
    char *message,
    size_t message_capacity
);
int xls_license_import_file(
    xls_license_manager *manager,
    const char *path,
    char *message,
    size_t message_capacity
);

#endif
