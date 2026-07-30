#include "update_checker.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "%s:%d: check failed: %s\n", \
                __FILE__, __LINE__, #condition); \
            ++failures; \
        } \
    } while (0)

static void test_version_comparison(void)
{
    CHECK(xls_update_compare_versions("1.1.4", "1.1.3") > 0);
    CHECK(xls_update_compare_versions("1.2", "1.1.99") > 0);
    CHECK(xls_update_compare_versions("v1.1.4", "1.1.4") == 0);
    CHECK(xls_update_compare_versions("1.1.4", "1.1.4-beta") > 0);
    CHECK(xls_update_compare_versions("1.1.3", "1.1.4") < 0);
}

static void test_response_parsing(void)
{
    static const char json[] =
        "{"
        "\"latest_version\":\"1.1.4\","
        "\"changelog\":\"更新内容\","
        "\"downloads\":{"
            "\"windows_amd64\":\"https://example.com/windows.msi\","
            "\"macos\":\"https://example.com/macos.dmg\","
            "\"linux_amd64\":\"https://example.com/linux.deb\""
        "}"
        "}";
    xls_update_info info;
    CHECK(xls_update_parse_response(
        json, "windows_amd64", &info
    ));
    CHECK(strcmp(info.latest_version, "1.1.4") == 0);
    CHECK(strcmp(info.changelog, "更新内容") == 0);
    CHECK(strcmp(
        info.download_url, "https://example.com/windows.msi"
    ) == 0);
    CHECK(!xls_update_parse_response(json, "linux_arm64", &info));
    CHECK(!xls_update_parse_response("{}", "macos", &info));
}

int main(void)
{
    test_version_comparison();
    test_response_parsing();
    if (failures != 0) {
        fprintf(stderr, "%d update assertion(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    puts("all pure C update tests passed");
    return EXIT_SUCCESS;
}
