#include "sheet_tabs.h"

#include <stdio.h>

static int failures = 0;

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf( \
                stderr, \
                "check failed at %s:%d: %s\n", \
                __FILE__, \
                __LINE__, \
                #condition \
            ); \
            ++failures; \
        } \
    } while (0)

typedef struct tab_widths {
    const float *values;
    size_t count;
} tab_widths;

static float width_at(size_t index, void *user_data)
{
    const tab_widths *widths = (const tab_widths *)user_data;
    return index < widths->count ? widths->values[index] : 0.0f;
}

static void test_all_pages_are_reachable(void)
{
    static const float values[] = {
        104.0f, 150.0f, 138.0f, 116.0f, 190.0f, 132.0f,
        126.0f, 174.0f, 98.0f, 120.0f, 142.0f, 110.0f,
        180.0f, 136.0f, 124.0f, 156.0f, 144.0f, 112.0f,
        168.0f, 130.0f, 118.0f, 152.0f, 140.0f, 106.0f,
        176.0f, 128.0f, 114.0f, 148.0f, 134.0f, 122.0f,
        164.0f, 108.0f, 172.0f, 146.0f, 154.0f, 160.0f
    };
    const tab_widths widths = {
        values, sizeof(values) / sizeof(values[0])
    };
    size_t first = 0u;
    size_t page_count = 0u;
    while (first < widths.count) {
        const size_t end = xls_sheet_tab_page_end(
            first,
            widths.count,
            900.0f,
            8.0f,
            width_at,
            (void *)&widths
        );
        CHECK(end > first);
        first = end;
        ++page_count;
        CHECK(page_count <= widths.count);
    }
    CHECK(first == widths.count);
    CHECK(page_count > 1u);
}

static void test_previous_page_moves_back(void)
{
    static const float values[] = {
        120.0f, 130.0f, 140.0f, 150.0f, 160.0f, 170.0f
    };
    const tab_widths widths = {
        values, sizeof(values) / sizeof(values[0])
    };
    size_t first = 6u;
    size_t steps = 0u;
    while (first > 0u) {
        const size_t previous = xls_sheet_tab_previous_page(
            first,
            300.0f,
            8.0f,
            width_at,
            (void *)&widths
        );
        CHECK(previous < first);
        first = previous;
        ++steps;
        CHECK(steps <= widths.count);
    }
    CHECK(first == 0u);
}

static void test_narrow_view_still_shows_one_tab(void)
{
    static const float values[] = {420.0f, 180.0f};
    const tab_widths widths = {
        values, sizeof(values) / sizeof(values[0])
    };
    CHECK(xls_sheet_tab_page_end(
        0u,
        widths.count,
        60.0f,
        8.0f,
        width_at,
        (void *)&widths
    ) == 1u);
}

int main(void)
{
    test_all_pages_are_reachable();
    test_previous_page_moves_back();
    test_narrow_view_still_shows_one_tab();
    if (failures != 0) {
        fprintf(stderr, "%d sheet-tab assertion(s) failed\n", failures);
        return 1;
    }
    puts("sheet-tab pagination tests passed");
    return 0;
}
