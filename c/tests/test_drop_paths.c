#include "drop_paths.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct captured_paths {
    char values[4][256];
    size_t count;
} captured_paths;

static int failures = 0;

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "%s:%d: check failed: %s\n", \
                __FILE__, __LINE__, #condition); \
            ++failures; \
        } \
    } while (0)

static int capture_path(const char *path, void *user_data)
{
    captured_paths *captured = (captured_paths *)user_data;
    if (captured->count >= 4u) {
        return 0;
    }
    (void)snprintf(
        captured->values[captured->count],
        sizeof(captured->values[captured->count]),
        "%s",
        path
    );
    ++captured->count;
    return 1;
}

static void test_extensions(void)
{
    CHECK(xls_drop_path_is_workbook("/tmp/report.xlsx"));
    CHECK(xls_drop_path_is_workbook("/tmp/report.XLS"));
    CHECK(!xls_drop_path_is_workbook("/tmp/report.csv"));
    CHECK(!xls_drop_path_is_workbook(NULL));
}

static void test_uri_list(void)
{
    captured_paths captured;
    memset(&captured, 0, sizeof(captured));
    const size_t count = xls_drop_paths_from_text(
        "# text/uri-list\r\n"
        "file:///tmp/monthly%20report.xlsx\r\n"
        "file://localhost/tmp/legacy.xls\n"
        "file:///tmp/ignored.csv\n",
        capture_path,
        &captured
    );
    CHECK(count == 2u);
    CHECK(captured.count == 2u);
    CHECK(strcmp(
        captured.values[0], "/tmp/monthly report.xlsx"
    ) == 0);
    CHECK(strcmp(captured.values[1], "/tmp/legacy.xls") == 0);

    memset(&captured, 0, sizeof(captured));
    CHECK(xls_drop_paths_from_text(
        "file://fileserver/shared/report.xlsx",
        capture_path,
        &captured
    ) == 1u);
    CHECK(strcmp(
        captured.values[0], "//fileserver/shared/report.xlsx"
    ) == 0);
}

static void test_plain_path(void)
{
    captured_paths captured;
    memset(&captured, 0, sizeof(captured));
    CHECK(xls_drop_paths_from_text(
        "  /tmp/local report.xlsx  ",
        capture_path,
        &captured
    ) == 1u);
    CHECK(strcmp(
        captured.values[0], "/tmp/local report.xlsx"
    ) == 0);
}

int main(void)
{
    test_extensions();
    test_uri_list();
    test_plain_path();
    if (failures != 0) {
        fprintf(stderr, "%d drop-path assertion(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    puts("drop-path tests passed");
    return EXIT_SUCCESS;
}
