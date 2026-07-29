#include "../app/license_manager.h"
#include "../app/platform_dialog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;
static char *saved_license = NULL;

static const char valid_license[] =
    "{\"key_id\":\"LINUX-BUILTIN\",\"plan\":\"personal_lifetime\","
    "\"device_hash\":\"\",\"device_components\":[],"
    "\"issued_at\":1784264344,\"expires_at\":0,"
    "\"signature\":\""
    "I1EQuvcw4hgggw22KGRW2bAOZQVd4wUuX2QJxq-kKSOZK4FzAD2UEJpg_"
    "qRNNK9HIjJCf0GYAMFDeJrI4NmjCw\"}";

static char *copy_text(const char *text)
{
    const size_t length = strlen(text);
    char *copy = (char *)malloc(length + 1u);
    if (copy != NULL) {
        memcpy(copy, text, length + 1u);
    }
    return copy;
}

static void expect_true(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

size_t xls_platform_device_components(
    char components[][256],
    size_t capacity
)
{
    static const char *const values[] = {
        "BOARD-TEST-001",
        "CPU-TEST-002",
        "DISK-TEST-003"
    };
    size_t index;
    const size_t count = capacity < 3u ? capacity : 3u;
    for (index = 0u; index < count; ++index) {
        (void)snprintf(components[index], 256u, "%s", values[index]);
    }
    return count;
}

int xls_platform_host_name(char *buffer, size_t capacity)
{
    return snprintf(buffer, capacity, "license-test-device") > 0;
}

int xls_platform_read_license(char **contents, int64_t *last_seen_utc)
{
    (void)contents;
    (void)last_seen_utc;
    return 0;
}

int xls_platform_write_license(const char *contents, int64_t last_seen_utc)
{
    char *replacement;
    (void)last_seen_utc;
    replacement = copy_text(contents == NULL ? "" : contents);
    if (replacement == NULL) {
        return 0;
    }
    free(saved_license);
    saved_license = replacement;
    return 1;
}

int xls_platform_read_text_file(const char *path, char **contents)
{
    char tampered[sizeof(valid_license)];
    if (strcmp(path, "valid") == 0) {
        *contents = copy_text(valid_license);
    } else if (strcmp(path, "trailing") == 0) {
        const size_t length = strlen(valid_license);
        *contents = (char *)malloc(length + 2u);
        if (*contents != NULL) {
            memcpy(*contents, valid_license, length);
            (*contents)[length] = 'x';
            (*contents)[length + 1u] = '\0';
        }
    } else {
        (void)snprintf(tampered, sizeof(tampered), "%s", valid_license);
        {
            char *key_id = strstr(tampered, "LINUX-BUILTIN");
            if (key_id != NULL) {
                key_id[0] = 'M';
            }
        }
        *contents = copy_text(tampered);
    }
    return *contents != NULL;
}

int xls_platform_http_request(
    const char *method,
    const char *url,
    const char *json_body,
    char **response_body,
    long *status_code
)
{
    (void)method;
    (void)url;
    (void)json_body;
    *response_body = NULL;
    *status_code = 0;
    return 0;
}

static void test_key_normalization(void)
{
    char normalized[20];
    expect_true(
        xls_license_normalize_key(
            "abcd efgh-IJKL_mnop", normalized, sizeof(normalized)
        ) == 0,
        "激活码应拒绝下划线"
    );
    expect_true(
        xls_license_normalize_key(
            "abcd efgh-IJKL mnop", normalized, sizeof(normalized)
        ) == 1,
        "激活码应接受空格和连字符"
    );
    expect_true(
        strcmp(normalized, "ABCD-EFGH-IJKL-MNOP") == 0,
        "激活码应规范化为四组大写字符"
    );
    expect_true(
        xls_license_normalize_key(
            "ABCD-EFGH-IJKL", normalized, sizeof(normalized)
        ) == 0,
        "激活码应要求完整的 16 个字符"
    );
}

static void test_signed_license(void)
{
    xls_license_manager manager;
    char message[160];
    xls_license_manager_init(&manager);
    expect_true(
        strlen(manager.device_fingerprint) == 64u,
        "设备指纹应为 64 位 SHA-256"
    );
    expect_true(
        manager.component_count == 3u,
        "设备指纹应保留三个硬件分量"
    );
    expect_true(
        xls_license_import_file(
            &manager, "valid", message, sizeof(message)
        ) == 1,
        "有效 Ed25519 授权应通过验证"
    );
    expect_true(
        manager.state == XLS_LICENSE_ACTIVATED
            && xls_license_is_full(&manager),
        "有效终身授权应解锁完整功能"
    );
    expect_true(
        saved_license != NULL && strstr(saved_license, "LINUX-BUILTIN") != NULL,
        "验证后的授权应持久化"
    );
}

static void test_tampering_and_trailing_data(void)
{
    xls_license_manager manager;
    char message[160];
    xls_license_manager_init(&manager);
    expect_true(
        xls_license_import_file(
            &manager, "tampered", message, sizeof(message)
        ) == 0,
        "被篡改的授权必须被拒绝"
    );
    expect_true(
        strstr(message, "签名") != NULL,
        "篡改错误应明确指向签名验证"
    );
    expect_true(
        xls_license_import_file(
            &manager, "trailing", message, sizeof(message)
        ) == 0,
        "尾随非 JSON 数据必须被拒绝"
    );
}

int main(void)
{
    test_key_normalization();
    test_signed_license();
    test_tampering_and_trailing_data();
    free(saved_license);
    if (failures != 0) {
        fprintf(stderr, "%d license test(s) failed\n", failures);
        return 1;
    }
    puts("license tests passed");
    return 0;
}
