#include "source_list.h"

double xls_source_list_max_offset(
    size_t source_count,
    double row_height,
    double viewport_height
)
{
    const double content_height = (double)source_count * row_height;
    if (row_height <= 0.0 || viewport_height <= 0.0
        || content_height <= viewport_height) {
        return 0.0;
    }
    return content_height - viewport_height;
}

double xls_source_list_clamp_offset(
    double offset,
    size_t source_count,
    double row_height,
    double viewport_height
)
{
    const double maximum = xls_source_list_max_offset(
        source_count, row_height, viewport_height
    );
    if (offset <= 0.0) {
        return 0.0;
    }
    return offset >= maximum ? maximum : offset;
}

double xls_source_list_scroll_wheel(
    double offset,
    double wheel_delta,
    size_t source_count,
    double row_height,
    double viewport_height
)
{
    return xls_source_list_clamp_offset(
        offset - wheel_delta * row_height,
        source_count,
        row_height,
        viewport_height
    );
}

double xls_source_list_reveal_index(
    double offset,
    size_t source_index,
    size_t source_count,
    double row_height,
    double viewport_height
)
{
    double target = offset;
    const double row_top = (double)source_index * row_height;
    const double row_bottom = row_top + row_height;
    if (source_count == 0u || source_index >= source_count
        || row_height <= 0.0 || viewport_height <= 0.0) {
        return xls_source_list_clamp_offset(
            offset, source_count, row_height, viewport_height
        );
    }
    if (row_top < target) {
        target = row_top;
    } else if (row_bottom > target + viewport_height) {
        target = row_bottom - viewport_height;
    }
    return xls_source_list_clamp_offset(
        target, source_count, row_height, viewport_height
    );
}
