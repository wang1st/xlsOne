#include "source_list.h"

#include <math.h>
#include <stdio.h>

static int failures = 0;

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(                                                          \
                stderr,                                                        \
                "CHECK failed at %s:%d: %s\n",                                 \
                __FILE__,                                                      \
                __LINE__,                                                      \
                #condition                                                     \
            );                                                                 \
            ++failures;                                                        \
        }                                                                      \
    } while (0)

static int nearly_equal(double left, double right)
{
    return fabs(left - right) < 0.0001;
}

static void test_short_list_does_not_scroll(void)
{
    CHECK(nearly_equal(
        xls_source_list_max_offset(3u, 56.0, 224.0), 0.0
    ));
    CHECK(nearly_equal(
        xls_source_list_scroll_wheel(
            0.0, -4.0, 3u, 56.0, 224.0
        ),
        0.0
    ));
}

static void test_wheel_reaches_both_ends(void)
{
    const size_t source_count = 20u;
    const double maximum = 20.0 * 56.0 - 168.0;
    double offset = 0.0;
    CHECK(nearly_equal(
        xls_source_list_max_offset(source_count, 56.0, 168.0),
        maximum
    ));
    offset = xls_source_list_scroll_wheel(
        offset, -1.0, source_count, 56.0, 168.0
    );
    CHECK(nearly_equal(offset, 56.0));
    offset = xls_source_list_scroll_wheel(
        offset, -100.0, source_count, 56.0, 168.0
    );
    CHECK(nearly_equal(offset, maximum));
    offset = xls_source_list_scroll_wheel(
        offset, 100.0, source_count, 56.0, 168.0
    );
    CHECK(nearly_equal(offset, 0.0));
}

static void test_reveal_makes_every_source_reachable(void)
{
    const size_t source_count = 36u;
    const double viewport_height = 190.0;
    double offset = 0.0;
    size_t index;
    for (index = 0u; index < source_count; ++index) {
        const double row_top = (double)index * 56.0;
        const double row_bottom = row_top + 56.0;
        offset = xls_source_list_reveal_index(
            offset,
            index,
            source_count,
            56.0,
            viewport_height
        );
        CHECK(row_top >= offset - 0.0001);
        CHECK(row_bottom <= offset + viewport_height + 0.0001);
    }
    CHECK(nearly_equal(
        offset,
        xls_source_list_max_offset(
            source_count, 56.0, viewport_height
        )
    ));
    offset = xls_source_list_reveal_index(
        offset, 0u, source_count, 56.0, viewport_height
    );
    CHECK(nearly_equal(offset, 0.0));
}

int main(void)
{
    test_short_list_does_not_scroll();
    test_wheel_reaches_both_ends();
    test_reveal_makes_every_source_reachable();
    if (failures != 0) {
        fprintf(stderr, "%d source-list test(s) failed\n", failures);
        return 1;
    }
    printf("source-list tests passed\n");
    return 0;
}
