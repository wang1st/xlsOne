#include "sheet_tabs.h"

static float normalized_width(
    size_t index,
    xls_sheet_tab_width_callback width_callback,
    void *user_data
)
{
    const float width = width_callback(index, user_data);
    return width > 1.0f ? width : 1.0f;
}

size_t xls_sheet_tab_page_end(
    size_t first,
    size_t count,
    float available_width,
    float gap,
    xls_sheet_tab_width_callback width_callback,
    void *user_data
)
{
    size_t end = first;
    float used = 0.0f;
    if (first >= count
        || available_width <= 0.0f
        || width_callback == NULL) {
        return first;
    }
    while (end < count) {
        const float width = normalized_width(
            end, width_callback, user_data
        );
        const float required = width
            + (end == first ? 0.0f : gap);
        if (end > first && used + required > available_width) {
            break;
        }
        used += required;
        ++end;
    }
    return end;
}

size_t xls_sheet_tab_previous_page(
    size_t first,
    float available_width,
    float gap,
    xls_sheet_tab_width_callback width_callback,
    void *user_data
)
{
    size_t start = first;
    float used = 0.0f;
    if (first == 0u
        || available_width <= 0.0f
        || width_callback == NULL) {
        return 0u;
    }
    while (start > 0u) {
        const float width = normalized_width(
            start - 1u, width_callback, user_data
        );
        const float required = width
            + (start == first ? 0.0f : gap);
        if (start < first && used + required > available_width) {
            break;
        }
        used += required;
        --start;
    }
    return start;
}
