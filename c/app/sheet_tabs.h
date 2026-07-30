#ifndef XLSONE_SHEET_TABS_H
#define XLSONE_SHEET_TABS_H

#include <stddef.h>

typedef float (*xls_sheet_tab_width_callback)(
    size_t index,
    void *user_data
);

size_t xls_sheet_tab_page_end(
    size_t first,
    size_t count,
    float available_width,
    float gap,
    xls_sheet_tab_width_callback width_callback,
    void *user_data
);

size_t xls_sheet_tab_previous_page(
    size_t first,
    float available_width,
    float gap,
    xls_sheet_tab_width_callback width_callback,
    void *user_data
);

#endif
