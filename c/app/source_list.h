#ifndef XLSONE_SOURCE_LIST_H
#define XLSONE_SOURCE_LIST_H

#include <stddef.h>

double xls_source_list_max_offset(
    size_t source_count,
    double row_height,
    double viewport_height
);

double xls_source_list_clamp_offset(
    double offset,
    size_t source_count,
    double row_height,
    double viewport_height
);

double xls_source_list_scroll_wheel(
    double offset,
    double wheel_delta,
    size_t source_count,
    double row_height,
    double viewport_height
);

double xls_source_list_reveal_index(
    double offset,
    size_t source_index,
    size_t source_count,
    double row_height,
    double viewport_height
);

#endif
