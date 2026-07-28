#ifndef XLSONE_XLSONE_H
#define XLSONE_XLSONE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define XLS_ERROR_MESSAGE_CAPACITY 512

typedef enum xls_error_code {
    XLS_OK = 0,
    XLS_ERROR_ARGUMENT,
    XLS_ERROR_MEMORY,
    XLS_ERROR_IO,
    XLS_ERROR_FORMAT,
    XLS_ERROR_UNSUPPORTED,
    XLS_ERROR_INTERNAL
} xls_error_code;

typedef struct xls_error {
    xls_error_code code;
    char message[XLS_ERROR_MESSAGE_CAPACITY];
} xls_error;

typedef enum xls_cell_flags {
    XLS_CELL_HAS_RAW_VALUE = 1u << 0,
    XLS_CELL_HAS_NUMERIC_VALUE = 1u << 1,
    XLS_CELL_HAS_FORMAT_CODE = 1u << 2,
    XLS_CELL_IS_DATE = 1u << 3
} xls_cell_flags;

typedef struct xls_cell {
    char *value;
    char *raw_value;
    char *format_code;
    double numeric_value;
    unsigned int flags;
} xls_cell;

typedef struct xls_sheet {
    char *name;
    size_t row_count;
    size_t column_count;
    xls_cell *cells;
} xls_sheet;

typedef struct xls_workbook {
    char *filename;
    char *filepath;
    size_t sheet_count;
    xls_sheet *sheets;
} xls_workbook;

typedef enum xls_source_state {
    XLS_SOURCE_VALUE,
    XLS_SOURCE_EMPTY,
    XLS_SOURCE_MISSING
} xls_source_state;

typedef struct xls_source_entry {
    char *filename;
    char *filepath;
    char *value;
    char *raw_value;
    double numeric_value;
    unsigned int has_numeric_value;
    xls_source_state state;
} xls_source_entry;

typedef struct xls_source_overview {
    size_t value_count;
    size_t empty_count;
    size_t missing_count;
    size_t numeric_count;
    double numeric_median;
    unsigned int has_numeric_median;
    size_t *outlier_indexes;
    size_t outlier_count;
} xls_source_overview;

typedef enum xls_cell_kind {
    XLS_CELL_LABEL,
    XLS_CELL_SUM,
    XLS_CELL_MIXED,
    XLS_CELL_SINGLE
} xls_cell_kind;

typedef struct xls_merge_decision {
    xls_cell_kind auto_detected_type;
    double confidence;
    unsigned int is_suspicious;
    char *reason;
} xls_merge_decision;

typedef struct xls_merged_cell {
    xls_cell_kind kind;
    char *display_value;
    double sum;
    size_t mixed_count;
    char *single_value;
    char *format_code;
    xls_source_entry *sources;
    size_t source_count;
    unsigned int is_overridden;
    xls_merge_decision decision;
} xls_merged_cell;

typedef struct xls_merged_sheet {
    char *sheet_name;
    size_t row_count;
    size_t column_count;
    xls_merged_cell *cells;
    char **source_files;
    size_t source_file_count;
} xls_merged_sheet;

typedef enum xls_merge_readiness {
    XLS_MERGE_BLOCKED,
    XLS_MERGE_READY
} xls_merge_readiness;

typedef enum xls_validation_issue_code {
    XLS_ISSUE_EMPTY_WORKBOOK,
    XLS_ISSUE_MISSING_SHEET,
    XLS_ISSUE_ROW_COUNT_MISMATCH,
    XLS_ISSUE_COLUMN_COUNT_MISMATCH
} xls_validation_issue_code;

typedef struct xls_validation_issue {
    xls_validation_issue_code code;
    size_t workbook_index;
    char *filename;
    char *sheet_name;
    char *message;
} xls_validation_issue;

typedef struct xls_validation_report {
    xls_merge_readiness readiness;
    size_t template_workbook_index;
    unsigned int has_template_workbook;
    char **common_sheet_names;
    size_t common_sheet_count;
    char **skipped_sheet_names;
    size_t skipped_sheet_count;
    size_t *mergeable_workbook_indexes;
    size_t mergeable_workbook_count;
    xls_validation_issue *issues;
    size_t issue_count;
} xls_validation_report;

void xls_error_clear(xls_error *error);
const char *xls_error_code_name(xls_error_code code);

int xls_parse_number(const char *text, double *value);
char *xls_format_number(double value, const char *format_code);
const char *xls_cell_kind_name(xls_cell_kind kind);

int xls_cell_set(
    xls_cell *cell,
    const char *value,
    const char *raw_value,
    const double *numeric_value,
    const char *format_code,
    int is_date,
    xls_error *error
);
void xls_cell_free(xls_cell *cell);

int xls_sheet_init(
    xls_sheet *sheet,
    const char *name,
    size_t rows,
    size_t columns,
    xls_error *error
);
int xls_sheet_resize(
    xls_sheet *sheet,
    size_t rows,
    size_t columns,
    xls_error *error
);
xls_cell *xls_sheet_cell(xls_sheet *sheet, size_t row, size_t column);
const xls_cell *xls_sheet_cell_const(
    const xls_sheet *sheet,
    size_t row,
    size_t column
);
size_t xls_sheet_effective_row_count(const xls_sheet *sheet);
size_t xls_sheet_effective_column_count(const xls_sheet *sheet);
void xls_sheet_free(xls_sheet *sheet);

const xls_sheet *xls_workbook_find_sheet(
    const xls_workbook *workbook,
    const char *sheet_name
);
void xls_workbook_free(xls_workbook *workbook);

int xls_parse_file(
    const char *path,
    xls_workbook *workbook,
    xls_error *error
);

int xls_analyze_sources(
    const xls_source_entry *sources,
    size_t source_count,
    xls_source_overview *overview,
    xls_error *error
);
void xls_source_overview_free(xls_source_overview *overview);

int xls_validate_workbooks(
    const xls_workbook *workbooks,
    size_t workbook_count,
    xls_validation_report *report,
    xls_error *error
);
void xls_validation_report_free(xls_validation_report *report);

int xls_export_csv(
    const xls_merged_sheet *sheet,
    const char *output_path,
    const char *watermark,
    xls_error *error
);
int xls_export_xlsx(
    const char *template_path,
    const xls_merged_sheet *sheets,
    size_t sheet_count,
    const char *output_path,
    const char *watermark,
    xls_error *error
);

int xls_merge_sheet(
    const xls_workbook *workbooks,
    size_t workbook_count,
    const char *sheet_name,
    xls_merged_sheet *result,
    xls_error *error
);
int xls_merge_first_sheets(
    const xls_workbook *workbooks,
    size_t workbook_count,
    xls_merged_sheet *result,
    xls_error *error
);
const xls_merged_cell *xls_merged_sheet_cell(
    const xls_merged_sheet *sheet,
    size_t row,
    size_t column
);
xls_merged_cell *xls_merged_sheet_cell_mutable(
    xls_merged_sheet *sheet,
    size_t row,
    size_t column
);
int xls_merged_cell_set_kind(
    xls_merged_cell *cell,
    xls_cell_kind kind,
    xls_error *error
);
int xls_merged_cell_restore_automatic(
    xls_merged_cell *cell,
    xls_error *error
);
void xls_merged_sheet_free(xls_merged_sheet *sheet);

#ifdef __cplusplus
}
#endif

#endif
