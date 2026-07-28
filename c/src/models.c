#include "internal.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int compare_double(const void *left, const void *right)
{
    const double lhs = *(const double *)left;
    const double rhs = *(const double *)right;
    return lhs < rhs ? -1 : lhs > rhs ? 1 : 0;
}

static double median_sorted(const double *values, size_t count)
{
    const size_t middle = count / 2;
    if ((count % 2u) == 0u) {
        return (values[middle - 1] + values[middle]) / 2.0;
    }
    return values[middle];
}

int xls_parse_number(const char *text, double *value)
{
    char *trimmed;
    char *body;
    char *normalized;
    char *end;
    size_t length;
    size_t index;
    size_t output_index = 0;
    long last_comma = -1;
    long last_period = -1;
    int negative = 0;
    double parsed;

    if (text == NULL || value == NULL) {
        return 0;
    }
    trimmed = xls_trimdup(text);
    if (trimmed == NULL || trimmed[0] == '\0') {
        free(trimmed);
        return 0;
    }
    length = strlen(trimmed);
    if (length == 3 && xls_string_all_ascii_digits(trimmed)) {
        free(trimmed);
        return 0;
    }

    body = trimmed;
    if (length >= 2 && body[0] == '(' && body[length - 1] == ')') {
        body[length - 1] = '\0';
        ++body;
        negative = 1;
    } else if (body[0] == '-') {
        ++body;
        negative = 1;
    }
    while (*body != '\0' && isspace((unsigned char)*body)) {
        ++body;
    }
    length = strlen(body);
    while (length > 0 && isspace((unsigned char)body[length - 1])) {
        body[--length] = '\0';
    }

    for (index = 0; index < length; ++index) {
        if (body[index] == ',') {
            last_comma = (long)index;
        } else if (body[index] == '.') {
            last_period = (long)index;
        }
    }

    normalized = (char *)malloc(length + 1);
    if (normalized == NULL) {
        free(trimmed);
        return 0;
    }
    for (index = 0; index < length; ++index) {
        char character = body[index];
        if (last_comma >= 0 && last_period >= 0) {
            if (length - (size_t)last_comma <= 3u) {
                if (character == '.') {
                    continue;
                }
                if (character == ',') {
                    character = '.';
                }
            } else if (character == ',') {
                continue;
            }
        } else if (last_comma >= 0) {
            if (length - (size_t)last_comma == 3u) {
                if (character == ',') {
                    character = '.';
                }
            } else if (character == ',') {
                continue;
            }
        }
        normalized[output_index++] = character;
    }
    normalized[output_index] = '\0';

    errno = 0;
    end = NULL;
    parsed = strtod(normalized, &end);
    if (errno == ERANGE || end == normalized || end == NULL || *end != '\0'
        || !isfinite(parsed)) {
        free(normalized);
        free(trimmed);
        return 0;
    }
    *value = negative ? -parsed : parsed;
    free(normalized);
    free(trimmed);
    return 1;
}

static char *format_fixed_trimmed(double value, int precision)
{
    char buffer[128];
    char *period;
    char *end;
    (void)snprintf(buffer, sizeof(buffer), "%.*f", precision, value);
    period = strchr(buffer, '.');
    if (period != NULL) {
        end = buffer + strlen(buffer) - 1;
        while (end > period && *end == '0') {
            *end-- = '\0';
        }
        if (end == period) {
            *end = '\0';
        }
    }
    return xls_strdup(buffer);
}

static char *format_grouped(double value)
{
    char ungrouped[128];
    char grouped[160];
    const char *digits;
    const char *period;
    size_t integer_length;
    size_t leading;
    size_t output = 0;
    size_t index;
    int negative = value < 0.0;

    (void)snprintf(ungrouped, sizeof(ungrouped), "%.2f", fabs(value));
    period = strchr(ungrouped, '.');
    if (period != NULL) {
        char *end = ungrouped + strlen(ungrouped) - 1;
        while (end > period && *end == '0') {
            *end-- = '\0';
        }
        if (end == period) {
            *end = '\0';
            period = NULL;
        }
    }
    digits = ungrouped;
    integer_length = period == NULL
        ? strlen(digits)
        : (size_t)(period - digits);
    leading = integer_length % 3u;
    if (negative) {
        grouped[output++] = '-';
    }
    for (index = 0; index < integer_length; ++index) {
        if (index > 0
            && index >= leading
            && (index - leading) % 3u == 0u) {
            grouped[output++] = ',';
        }
        grouped[output++] = digits[index];
    }
    if (period != NULL) {
        const size_t fraction_length = strlen(period);
        memcpy(grouped + output, period, fraction_length);
        output += fraction_length;
    }
    grouped[output] = '\0';
    return xls_strdup(grouped);
}

char *xls_format_number(double value, const char *format_code)
{
    char *number;
    char *result;
    size_t length;
    if (format_code != NULL && format_code[0] != '\0') {
        if (strstr(format_code, "¥") != NULL) {
            char buffer[160];
            (void)snprintf(buffer, sizeof(buffer), "¥%.2f", value);
            return xls_strdup(buffer);
        }
        if (strchr(format_code, '$') != NULL && strstr(format_code, "[$-") == NULL) {
            char buffer[160];
            (void)snprintf(buffer, sizeof(buffer), "$%.2f", value);
            return xls_strdup(buffer);
        }
        if (strstr(format_code, "#,##0") != NULL) {
            return format_grouped(value);
        }
        if (strchr(format_code, '%') != NULL) {
            char buffer[160];
            (void)snprintf(buffer, sizeof(buffer), "%.2f%%", value * 100.0);
            return xls_strdup(buffer);
        }
        if (strstr(format_code, ".00") != NULL) {
            char buffer[160];
            (void)snprintf(buffer, sizeof(buffer), "%.2f", value);
            return xls_strdup(buffer);
        }
        if (strstr(format_code, ".0") != NULL) {
            char buffer[160];
            (void)snprintf(buffer, sizeof(buffer), "%.1f", value);
            return xls_strdup(buffer);
        }
    }
    if (fabs(value - round(value)) < 0.0000001) {
        char buffer[128];
        (void)snprintf(buffer, sizeof(buffer), "%.0f", value);
        return xls_strdup(buffer);
    }
    number = format_fixed_trimmed(value, 2);
    if (number == NULL) {
        return NULL;
    }
    length = strlen(number);
    result = xls_strndup(number, length);
    free(number);
    return result;
}

const char *xls_cell_kind_name(xls_cell_kind kind)
{
    switch (kind) {
    case XLS_CELL_LABEL:
        return "label";
    case XLS_CELL_SUM:
        return "sum";
    case XLS_CELL_MIXED:
        return "mixed";
    case XLS_CELL_SINGLE:
        return "single";
    }
    return "unknown";
}

void xls_cell_free(xls_cell *cell)
{
    if (cell == NULL) {
        return;
    }
    free(cell->value);
    free(cell->raw_value);
    free(cell->format_code);
    memset(cell, 0, sizeof(*cell));
}

int xls_cell_set(
    xls_cell *cell,
    const char *value,
    const char *raw_value,
    const double *numeric_value,
    const char *format_code,
    int is_date,
    xls_error *error
)
{
    xls_cell replacement;
    double parsed = 0.0;
    if (cell == NULL) {
        xls_set_error(error, XLS_ERROR_ARGUMENT, "cell must not be null");
        return 0;
    }
    memset(&replacement, 0, sizeof(replacement));
    replacement.value = xls_trimdup(value);
    if (replacement.value == NULL) {
        xls_set_error(error, XLS_ERROR_MEMORY, "out of memory while copying cell value");
        return 0;
    }
    if (raw_value != NULL) {
        replacement.raw_value = xls_strdup(raw_value);
        if (replacement.raw_value == NULL) {
            xls_cell_free(&replacement);
            xls_set_error(error, XLS_ERROR_MEMORY, "out of memory while copying raw value");
            return 0;
        }
        replacement.flags |= XLS_CELL_HAS_RAW_VALUE;
    }
    if (format_code != NULL) {
        replacement.format_code = xls_strdup(format_code);
        if (replacement.format_code == NULL) {
            xls_cell_free(&replacement);
            xls_set_error(error, XLS_ERROR_MEMORY, "out of memory while copying format code");
            return 0;
        }
        replacement.flags |= XLS_CELL_HAS_FORMAT_CODE;
    }
    if (numeric_value != NULL) {
        replacement.numeric_value = *numeric_value;
        replacement.flags |= XLS_CELL_HAS_NUMERIC_VALUE;
    } else if (xls_parse_number(replacement.value, &parsed)) {
        replacement.numeric_value = parsed;
        replacement.flags |= XLS_CELL_HAS_NUMERIC_VALUE;
    }
    if (is_date) {
        replacement.flags |= XLS_CELL_IS_DATE;
    }
    xls_cell_free(cell);
    *cell = replacement;
    xls_error_clear(error);
    return 1;
}

int xls_sheet_init(
    xls_sheet *sheet,
    const char *name,
    size_t rows,
    size_t columns,
    xls_error *error
)
{
    size_t count;
    if (sheet == NULL || !xls_multiply_size(rows, columns, &count)) {
        xls_set_error(error, XLS_ERROR_ARGUMENT, "invalid sheet dimensions");
        return 0;
    }
    memset(sheet, 0, sizeof(*sheet));
    sheet->name = xls_strdup(name);
    if (sheet->name == NULL) {
        xls_set_error(error, XLS_ERROR_MEMORY, "out of memory while copying sheet name");
        return 0;
    }
    if (count > 0) {
        sheet->cells = (xls_cell *)calloc(count, sizeof(*sheet->cells));
        if (sheet->cells == NULL) {
            xls_sheet_free(sheet);
            xls_set_error(error, XLS_ERROR_MEMORY, "out of memory while allocating sheet");
            return 0;
        }
    }
    sheet->row_count = rows;
    sheet->column_count = columns;
    xls_error_clear(error);
    return 1;
}

int xls_sheet_resize(
    xls_sheet *sheet,
    size_t rows,
    size_t columns,
    xls_error *error
)
{
    xls_cell *replacement = NULL;
    size_t count;
    size_t copy_rows;
    size_t copy_columns;
    size_t row;
    size_t column;
    if (sheet == NULL || !xls_multiply_size(rows, columns, &count)) {
        xls_set_error(error, XLS_ERROR_ARGUMENT, "invalid sheet dimensions");
        return 0;
    }
    if (count > 0) {
        replacement = (xls_cell *)calloc(count, sizeof(*replacement));
        if (replacement == NULL) {
            xls_set_error(error, XLS_ERROR_MEMORY, "out of memory while resizing sheet");
            return 0;
        }
    }
    copy_rows = rows < sheet->row_count ? rows : sheet->row_count;
    copy_columns = columns < sheet->column_count ? columns : sheet->column_count;
    for (row = 0; row < copy_rows; ++row) {
        for (column = 0; column < copy_columns; ++column) {
            replacement[row * columns + column]
                = sheet->cells[row * sheet->column_count + column];
            memset(
                &sheet->cells[row * sheet->column_count + column],
                0,
                sizeof(xls_cell)
            );
        }
    }
    for (row = 0; row < sheet->row_count; ++row) {
        for (column = 0; column < sheet->column_count; ++column) {
            xls_cell_free(&sheet->cells[row * sheet->column_count + column]);
        }
    }
    free(sheet->cells);
    sheet->cells = replacement;
    sheet->row_count = rows;
    sheet->column_count = columns;
    xls_error_clear(error);
    return 1;
}

xls_cell *xls_sheet_cell(xls_sheet *sheet, size_t row, size_t column)
{
    if (sheet == NULL || row >= sheet->row_count || column >= sheet->column_count) {
        return NULL;
    }
    return &sheet->cells[row * sheet->column_count + column];
}

const xls_cell *xls_sheet_cell_const(
    const xls_sheet *sheet,
    size_t row,
    size_t column
)
{
    if (sheet == NULL || row >= sheet->row_count || column >= sheet->column_count) {
        return NULL;
    }
    return &sheet->cells[row * sheet->column_count + column];
}

size_t xls_sheet_effective_row_count(const xls_sheet *sheet)
{
    size_t row;
    size_t column;
    size_t last = 0;
    if (sheet == NULL) {
        return 0;
    }
    for (row = 0; row < sheet->row_count; ++row) {
        for (column = 0; column < sheet->column_count; ++column) {
            const xls_cell *cell = xls_sheet_cell_const(sheet, row, column);
            if (cell != NULL && !xls_string_empty(cell->value)) {
                last = row + 1;
                break;
            }
        }
    }
    return last;
}

size_t xls_sheet_effective_column_count(const xls_sheet *sheet)
{
    size_t row;
    size_t column;
    size_t last = 0;
    if (sheet == NULL) {
        return 0;
    }
    for (row = 0; row < sheet->row_count; ++row) {
        for (column = 0; column < sheet->column_count; ++column) {
            const xls_cell *cell = xls_sheet_cell_const(sheet, row, column);
            if (cell != NULL && !xls_string_empty(cell->value) && column + 1 > last) {
                last = column + 1;
            }
        }
    }
    return last;
}

void xls_sheet_free(xls_sheet *sheet)
{
    size_t count;
    size_t index;
    if (sheet == NULL) {
        return;
    }
    count = sheet->row_count * sheet->column_count;
    for (index = 0; index < count; ++index) {
        xls_cell_free(&sheet->cells[index]);
    }
    free(sheet->cells);
    free(sheet->name);
    memset(sheet, 0, sizeof(*sheet));
}

const xls_sheet *xls_workbook_find_sheet(
    const xls_workbook *workbook,
    const char *sheet_name
)
{
    size_t index;
    if (workbook == NULL || sheet_name == NULL) {
        return NULL;
    }
    for (index = 0; index < workbook->sheet_count; ++index) {
        if (workbook->sheets[index].name != NULL
            && strcmp(workbook->sheets[index].name, sheet_name) == 0) {
            return &workbook->sheets[index];
        }
    }
    return NULL;
}

void xls_workbook_free(xls_workbook *workbook)
{
    size_t index;
    if (workbook == NULL) {
        return;
    }
    for (index = 0; index < workbook->sheet_count; ++index) {
        xls_sheet_free(&workbook->sheets[index]);
    }
    free(workbook->sheets);
    free(workbook->filename);
    free(workbook->filepath);
    memset(workbook, 0, sizeof(*workbook));
}

int xls_analyze_sources(
    const xls_source_entry *sources,
    size_t source_count,
    xls_source_overview *overview,
    xls_error *error
)
{
    double *values;
    double *deviations;
    size_t *source_indexes;
    size_t index;
    size_t numeric_count = 0;
    double median;
    double median_absolute_deviation;
    if (overview == NULL || (sources == NULL && source_count > 0)) {
        xls_set_error(error, XLS_ERROR_ARGUMENT, "invalid source overview arguments");
        return 0;
    }
    memset(overview, 0, sizeof(*overview));
    values = source_count == 0
        ? NULL
        : (double *)malloc(source_count * sizeof(*values));
    deviations = source_count == 0
        ? NULL
        : (double *)malloc(source_count * sizeof(*deviations));
    source_indexes = source_count == 0
        ? NULL
        : (size_t *)malloc(source_count * sizeof(*source_indexes));
    if (source_count > 0
        && (values == NULL || deviations == NULL || source_indexes == NULL)) {
        free(values);
        free(deviations);
        free(source_indexes);
        xls_set_error(error, XLS_ERROR_MEMORY, "out of memory while analyzing sources");
        return 0;
    }
    for (index = 0; index < source_count; ++index) {
        if (sources[index].state == XLS_SOURCE_VALUE) {
            ++overview->value_count;
            if (sources[index].has_numeric_value
                && isfinite(sources[index].numeric_value)) {
                values[numeric_count] = sources[index].numeric_value;
                source_indexes[numeric_count] = index;
                ++numeric_count;
            }
        } else if (sources[index].state == XLS_SOURCE_EMPTY) {
            ++overview->empty_count;
        } else {
            ++overview->missing_count;
        }
    }
    overview->numeric_count = numeric_count;
    if (numeric_count == 0) {
        free(values);
        free(deviations);
        free(source_indexes);
        xls_error_clear(error);
        return 1;
    }
    qsort(values, numeric_count, sizeof(*values), compare_double);
    median = median_sorted(values, numeric_count);
    overview->numeric_median = median;
    overview->has_numeric_median = 1;
    if (numeric_count < 5) {
        free(values);
        free(deviations);
        free(source_indexes);
        xls_error_clear(error);
        return 1;
    }
    for (index = 0; index < numeric_count; ++index) {
        deviations[index] = fabs(values[index] - median);
    }
    qsort(deviations, numeric_count, sizeof(*deviations), compare_double);
    median_absolute_deviation = median_sorted(deviations, numeric_count);
    if (median_absolute_deviation > 2.2204460492503131e-16 * fmax(1.0, fabs(median))) {
        const double minimum_difference = fmax(1.0, fabs(median) * 0.5);
        overview->outlier_indexes
            = (size_t *)malloc(numeric_count * sizeof(*overview->outlier_indexes));
        if (overview->outlier_indexes == NULL) {
            free(values);
            free(deviations);
            free(source_indexes);
            xls_source_overview_free(overview);
            xls_set_error(error, XLS_ERROR_MEMORY, "out of memory while recording outliers");
            return 0;
        }
        for (index = 0; index < source_count; ++index) {
            if (sources[index].state == XLS_SOURCE_VALUE
                && sources[index].has_numeric_value) {
                const double difference = fabs(sources[index].numeric_value - median);
                const double robust_score
                    = 0.6745 * difference / median_absolute_deviation;
                if (robust_score >= 3.5 && difference >= minimum_difference) {
                    overview->outlier_indexes[overview->outlier_count++] = index;
                }
            }
        }
    }
    free(values);
    free(deviations);
    free(source_indexes);
    xls_error_clear(error);
    return 1;
}

void xls_source_overview_free(xls_source_overview *overview)
{
    if (overview == NULL) {
        return;
    }
    free(overview->outlier_indexes);
    memset(overview, 0, sizeof(*overview));
}
