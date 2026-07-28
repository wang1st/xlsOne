#include "xlsone/xlsone.h"

#include <math.h>
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

static char *test_duplicate_string(const char *value)
{
    size_t length = strlen(value);
    char *copy = (char *)malloc(length + 1);
    if (copy != NULL) {
        memcpy(copy, value, length + 1);
    }
    return copy;
}

static void check_close(double actual, double expected)
{
    CHECK(fabs(actual - expected) < 0.0000001);
}

static void test_parse_numbers(void)
{
    double value = 0.0;
    char *formatted;
    CHECK(xls_parse_number("1000", &value));
    check_close(value, 1000.0);
    CHECK(xls_parse_number("1,000.50", &value));
    check_close(value, 1000.5);
    CHECK(xls_parse_number("1.234,56", &value));
    check_close(value, 1234.56);
    CHECK(!xls_parse_number("201", &value));
    CHECK(!xls_parse_number("abc", &value));
    CHECK(xls_parse_number("(100.50)", &value));
    check_close(value, -100.5);
    formatted = xls_format_number(9427.0, "#,##0.00");
    CHECK(formatted != NULL && strcmp(formatted, "9,427") == 0);
    free(formatted);
    formatted = xls_format_number(1234.5, "#,##0.00");
    CHECK(formatted != NULL && strcmp(formatted, "1,234.5") == 0);
    free(formatted);
    formatted = xls_format_number(14.0, "#,##0");
    CHECK(formatted != NULL && strcmp(formatted, "14") == 0);
    free(formatted);
}

static int make_workbook(
    xls_workbook *workbook,
    const char *filename,
    const char *sheet_name,
    size_t rows,
    size_t columns
)
{
    xls_error error;
    memset(workbook, 0, sizeof(*workbook));
    workbook->filename = test_duplicate_string(filename);
    workbook->filepath = test_duplicate_string(filename);
    workbook->sheet_count = 1;
    workbook->sheets = (xls_sheet *)calloc(1, sizeof(*workbook->sheets));
    if (workbook->filename == NULL || workbook->filepath == NULL
        || workbook->sheets == NULL) {
        xls_workbook_free(workbook);
        return 0;
    }
    if (!xls_sheet_init(
        &workbook->sheets[0], sheet_name, rows, columns, &error
    )) {
        fprintf(stderr, "sheet init failed: %s\n", error.message);
        xls_workbook_free(workbook);
        return 0;
    }
    return 1;
}

static void set_cell(
    xls_workbook *workbook,
    size_t row,
    size_t column,
    const char *value,
    const double *numeric,
    const char *format
)
{
    xls_error error;
    xls_cell *cell = xls_sheet_cell(&workbook->sheets[0], row, column);
    CHECK(cell != NULL);
    if (cell != NULL) {
        CHECK(xls_cell_set(
            cell, value, NULL, numeric, format, 0, &error
        ));
    }
}

static void test_merge_amounts_and_codes(void)
{
    xls_workbook workbooks[3];
    xls_merged_sheet result;
    xls_error error;
    const double amounts[] = {1000.0, 2000.0, 1500.0};
    const char *codes[] = {"331024001", "331024002", "331024003"};
    size_t index;
    memset(workbooks, 0, sizeof(workbooks));
    memset(&result, 0, sizeof(result));
    for (index = 0; index < 3; ++index) {
        CHECK(make_workbook(
            &workbooks[index],
            index == 0 ? "a.xlsx" : index == 1 ? "b.xlsx" : "c.xlsx",
            "Sheet1", 2, 3
        ));
        set_cell(&workbooks[index], 0, 0, "项目", NULL, NULL);
        set_cell(&workbooks[index], 0, 1, "代码", NULL, NULL);
        set_cell(&workbooks[index], 0, 2, "金额", NULL, NULL);
        set_cell(&workbooks[index], 1, 0, "行政区划代码", NULL, NULL);
        set_cell(&workbooks[index], 1, 1, codes[index], NULL, NULL);
        set_cell(
            &workbooks[index], 1, 2,
            index == 0 ? "1000" : index == 1 ? "2000" : "1500",
            &amounts[index], "#,##0"
        );
    }
    CHECK(xls_merge_sheet(workbooks, 3, "Sheet1", &result, &error));
    if (result.cells != NULL) {
        const xls_merged_cell *header = xls_merged_sheet_cell(&result, 0, 2);
        const xls_merged_cell *code = xls_merged_sheet_cell(&result, 1, 1);
        const xls_merged_cell *amount = xls_merged_sheet_cell(&result, 1, 2);
        CHECK(header != NULL && header->kind == XLS_CELL_LABEL);
        CHECK(code != NULL && code->kind == XLS_CELL_LABEL);
        CHECK(code != NULL && strcmp(code->display_value, "33102400_") == 0);
        CHECK(amount != NULL && amount->kind == XLS_CELL_SUM);
        if (amount != NULL) {
            xls_merged_cell *editable
                = xls_merged_sheet_cell_mutable(&result, 1, 2);
            check_close(amount->sum, 4500.0);
            CHECK(strcmp(amount->display_value, "4,500") == 0);
            CHECK(amount->source_count == 3);
            CHECK(editable != NULL);
            CHECK(xls_merged_cell_set_kind(
                editable, XLS_CELL_MIXED, &error
            ));
            CHECK(editable->kind == XLS_CELL_MIXED);
            CHECK(editable->is_overridden);
            CHECK(editable->mixed_count == 3);
            CHECK(strcmp(editable->display_value, "3条") == 0);
            CHECK(xls_merged_cell_set_kind(
                editable, XLS_CELL_SINGLE, &error
            ));
            CHECK(editable->kind == XLS_CELL_SINGLE);
            CHECK(strcmp(editable->display_value, "1000") == 0);
            CHECK(xls_merged_cell_restore_automatic(editable, &error));
            CHECK(editable->kind == XLS_CELL_SUM);
            CHECK(!editable->is_overridden);
            check_close(editable->sum, 4500.0);
            CHECK(strcmp(editable->display_value, "4,500") == 0);
        }
    }
    xls_merged_sheet_free(&result);
    for (index = 0; index < 3; ++index) {
        xls_workbook_free(&workbooks[index]);
    }
}

static void test_zero_and_blank_sources(void)
{
    xls_workbook workbooks[3];
    xls_merged_sheet result;
    xls_error error;
    const double zero = 0.0;
    const double amount = 123456.0;
    size_t index;
    memset(workbooks, 0, sizeof(workbooks));
    memset(&result, 0, sizeof(result));
    for (index = 0; index < 3; ++index) {
        CHECK(make_workbook(
            &workbooks[index],
            index == 0 ? "a.xlsx" : index == 1 ? "b.xlsx" : "c.xlsx",
            "Sheet1", 3, 2
        ));
        set_cell(&workbooks[index], 0, 0, "项目", NULL, NULL);
        set_cell(&workbooks[index], 0, 1, "金额", NULL, NULL);
        set_cell(&workbooks[index], 1, 0, "本期金额", NULL, NULL);
        set_cell(&workbooks[index], 1, 1, "0.0", &zero, "0.0");
        set_cell(&workbooks[index], 2, 0, "本期金额", NULL, NULL);
        if (index != 1) {
            set_cell(&workbooks[index], 2, 1, "123456", &amount, "#,##0");
        }
    }
    CHECK(xls_merge_sheet(workbooks, 3, "Sheet1", &result, &error));
    if (result.cells != NULL) {
        const xls_merged_cell *zeros = xls_merged_sheet_cell(&result, 1, 1);
        const xls_merged_cell *partial = xls_merged_sheet_cell(&result, 2, 1);
        CHECK(zeros != NULL && zeros->kind == XLS_CELL_SUM);
        CHECK(partial != NULL && partial->kind == XLS_CELL_SUM);
        if (partial != NULL) {
            check_close(partial->sum, 246912.0);
            CHECK(partial->sources[1].state == XLS_SOURCE_EMPTY);
        }
    }
    xls_merged_sheet_free(&result);
    for (index = 0; index < 3; ++index) {
        xls_workbook_free(&workbooks[index]);
    }
}

static void test_source_analysis(void)
{
    xls_source_entry sources[7];
    xls_source_overview overview;
    xls_error error;
    const double values[] = {100.0, 101.0, 102.0, 103.0, 1000.0};
    size_t index;
    memset(sources, 0, sizeof(sources));
    memset(&overview, 0, sizeof(overview));
    for (index = 0; index < 5; ++index) {
        sources[index].state = XLS_SOURCE_VALUE;
        sources[index].numeric_value = values[index];
        sources[index].has_numeric_value = 1;
    }
    sources[5].state = XLS_SOURCE_EMPTY;
    sources[6].state = XLS_SOURCE_MISSING;
    CHECK(xls_analyze_sources(sources, 7, &overview, &error));
    CHECK(overview.value_count == 5);
    CHECK(overview.empty_count == 1);
    CHECK(overview.missing_count == 1);
    CHECK(overview.numeric_count == 5);
    CHECK(overview.has_numeric_median);
    check_close(overview.numeric_median, 102.0);
    CHECK(overview.outlier_count == 1);
    CHECK(overview.outlier_indexes != NULL && overview.outlier_indexes[0] == 4);
    xls_source_overview_free(&overview);
}

static void test_parse_real_xlsx(void)
{
    const char *path = XLSONE_REPOSITORY_ROOT
        "/samples/monthly-report-sample-v1.1/01-operations-team-1-june-2026.xlsx";
    xls_workbook workbook;
    xls_error error;
    memset(&workbook, 0, sizeof(workbook));
    CHECK(xls_parse_file(path, &workbook, &error));
    if (workbook.sheet_count > 0) {
        size_t index;
        size_t populated = 0;
        CHECK(workbook.filename != NULL);
        CHECK(workbook.filepath != NULL);
        for (index = 0; index < workbook.sheet_count; ++index) {
            CHECK(workbook.sheets[index].name != NULL);
            populated += xls_sheet_effective_row_count(&workbook.sheets[index]);
        }
        CHECK(populated > 0);
    } else {
        CHECK(workbook.sheet_count > 0);
    }
    xls_workbook_free(&workbook);
}

static void test_parse_generated_xls_when_present(void)
{
    const char *path = XLSONE_TEST_OUTPUT_DIR
        "/xls-fixture/01-operations-team-1-june-2026.xls";
    FILE *probe = fopen(path, "rb");
    xls_workbook workbook;
    xls_error error;
    if (probe == NULL) {
        return;
    }
    fclose(probe);
    memset(&workbook, 0, sizeof(workbook));
    CHECK(xls_parse_file(path, &workbook, &error));
    if (workbook.sheet_count > 0) {
        const xls_sheet *business
            = xls_workbook_find_sheet(&workbook, "经营指标");
        const xls_sheet *project
            = xls_workbook_find_sheet(&workbook, "项目进度");
        CHECK(business != NULL);
        CHECK(project != NULL);
        if (business != NULL) {
            const xls_cell *amount = xls_sheet_cell_const(business, 5, 2);
            CHECK(amount != NULL);
            if (amount != NULL) {
                CHECK((amount->flags & XLS_CELL_HAS_NUMERIC_VALUE) != 0u);
                check_close(amount->numeric_value, 180000.0);
            }
        }
    } else {
        CHECK(workbook.sheet_count > 0);
    }
    xls_workbook_free(&workbook);
}

static void test_real_sample_merge(void)
{
    static const char *const filenames[] = {
        "01-operations-team-1-june-2026.xlsx",
        "02-operations-team-2-june-2026.xlsx",
        "03-operations-team-3-june-2026.xlsx",
        "04-operations-team-4-june-2026.xlsx"
    };
    static const double business_expected[6][3] = {
        {660000.0, 661000.0, 3020000.0},
        {305000.0, 297200.0, 1363000.0},
        {14.0, 16.0, 66.0},
        {22.0, 22.0, 83.0},
        {139.0, 143.0, 586.0},
        {132.0, 137.0, 558.0}
    };
    static const double project_expected[6][3] = {
        {460.0, 468.0, 1840.0},
        {305.0, 308.0, 1270.0},
        {14.0, 16.0, 66.0},
        {22.0, 22.0, 83.0},
        {347.0, 344.0, 1350.0},
        {150.0, 155.0, 586.0}
    };
    xls_workbook workbooks[4];
    xls_merged_sheet business;
    xls_merged_sheet project;
    xls_error error;
    size_t file_index;
    size_t row;
    size_t column;
    memset(workbooks, 0, sizeof(workbooks));
    memset(&business, 0, sizeof(business));
    memset(&project, 0, sizeof(project));
    for (file_index = 0; file_index < 4; ++file_index) {
        char path[1024];
        (void)snprintf(
            path,
            sizeof(path),
            "%s/samples/monthly-report-sample-v1.1/%s",
            XLSONE_REPOSITORY_ROOT,
            filenames[file_index]
        );
        CHECK(xls_parse_file(path, &workbooks[file_index], &error));
        CHECK(workbooks[file_index].sheet_count == 2);
    }
    {
        xls_validation_report report;
        memset(&report, 0, sizeof(report));
        CHECK(xls_validate_workbooks(workbooks, 4, &report, &error));
        CHECK(report.readiness == XLS_MERGE_READY);
        CHECK(report.common_sheet_count == 2);
        CHECK(report.skipped_sheet_count == 0);
        CHECK(report.mergeable_workbook_count == 4);
        CHECK(report.has_template_workbook);
        xls_validation_report_free(&report);
    }
    CHECK(xls_merge_sheet(workbooks, 4, "经营指标", &business, &error));
    CHECK(xls_merge_sheet(workbooks, 4, "项目进度", &project, &error));
    for (row = 0; row < 6; ++row) {
        for (column = 0; column < 3; ++column) {
            const xls_merged_cell *business_cell
                = xls_merged_sheet_cell(&business, row + 5, column + 2);
            const xls_merged_cell *project_cell
                = xls_merged_sheet_cell(&project, row + 5, column + 2);
            CHECK(business_cell != NULL);
            CHECK(project_cell != NULL);
            if (business_cell != NULL) {
                if (business_cell->kind != XLS_CELL_SUM
                    || fabs(
                        business_cell->sum - business_expected[row][column]
                    ) >= 0.0000001) {
                    fprintf(
                        stderr,
                        "business mismatch at %zu,%zu: kind=%s sum=%.15g display=%s\n",
                        row + 5,
                        column + 2,
                        xls_cell_kind_name(business_cell->kind),
                        business_cell->sum,
                        business_cell->display_value
                    );
                }
                CHECK(business_cell->kind == XLS_CELL_SUM);
                check_close(business_cell->sum, business_expected[row][column]);
            }
            if (project_cell != NULL) {
                CHECK(project_cell->kind == XLS_CELL_SUM);
                check_close(project_cell->sum, project_expected[row][column]);
            }
        }
    }
    {
        char template_path[1024];
        char output_path[1024];
        xls_merged_sheet sheets[2];
        xls_workbook exported;
        (void)snprintf(
            template_path,
            sizeof(template_path),
            "%s/samples/monthly-report-sample-v1.1/%s",
            XLSONE_REPOSITORY_ROOT,
            filenames[0]
        );
        (void)snprintf(
            output_path,
            sizeof(output_path),
            "%s/pure-c-export.xlsx",
            XLSONE_TEST_OUTPUT_DIR
        );
        sheets[0] = business;
        sheets[1] = project;
        CHECK(xls_export_xlsx(
            template_path,
            sheets,
            2,
            output_path,
            "仅供测试",
            &error
        ));
        memset(&exported, 0, sizeof(exported));
        CHECK(xls_parse_file(output_path, &exported, &error));
        if (exported.sheet_count == 2) {
            const xls_sheet *exported_business
                = xls_workbook_find_sheet(&exported, "经营指标");
            const xls_sheet *exported_project
                = xls_workbook_find_sheet(&exported, "项目进度");
            CHECK(exported_business != NULL);
            CHECK(exported_project != NULL);
            if (exported_business != NULL && exported_project != NULL) {
                const xls_cell *watermark
                    = xls_sheet_cell_const(exported_business, 13, 0);
                CHECK(watermark != NULL);
                CHECK(watermark != NULL
                    && strcmp(watermark->value, "仅供测试") == 0);
                for (row = 0; row < 6; ++row) {
                    for (column = 0; column < 3; ++column) {
                        const xls_cell *business_cell = xls_sheet_cell_const(
                            exported_business, row + 5, column + 2
                        );
                        const xls_cell *project_cell = xls_sheet_cell_const(
                            exported_project, row + 5, column + 2
                        );
                        CHECK(business_cell != NULL);
                        CHECK(project_cell != NULL);
                        if (business_cell != NULL) {
                            CHECK(
                                (business_cell->flags
                                    & XLS_CELL_HAS_NUMERIC_VALUE) != 0u
                            );
                            check_close(
                                business_cell->numeric_value,
                                business_expected[row][column]
                            );
                        }
                        if (project_cell != NULL) {
                            CHECK(
                                (project_cell->flags
                                    & XLS_CELL_HAS_NUMERIC_VALUE) != 0u
                            );
                            check_close(
                                project_cell->numeric_value,
                                project_expected[row][column]
                            );
                        }
                    }
                }
            }
        } else {
            CHECK(exported.sheet_count == 2);
        }
        xls_workbook_free(&exported);
    }
    xls_merged_sheet_free(&business);
    xls_merged_sheet_free(&project);
    for (file_index = 0; file_index < 4; ++file_index) {
        xls_workbook_free(&workbooks[file_index]);
    }
}

static void test_validation_rejects_dimension_mismatch(void)
{
    xls_workbook workbooks[3];
    xls_validation_report report;
    xls_error error;
    size_t index;
    memset(workbooks, 0, sizeof(workbooks));
    memset(&report, 0, sizeof(report));
    for (index = 0; index < 3; ++index) {
        CHECK(make_workbook(
            &workbooks[index],
            index == 0 ? "a.xlsx" : index == 1 ? "b.xlsx" : "c.xlsx",
            "Sheet1",
            index == 2 ? 3 : 2,
            2
        ));
        set_cell(&workbooks[index], 0, 0, "标题", NULL, NULL);
        set_cell(&workbooks[index], 1, 0, "值", NULL, NULL);
        if (index == 2) {
            set_cell(&workbooks[index], 2, 0, "额外行", NULL, NULL);
        }
    }
    CHECK(xls_validate_workbooks(workbooks, 3, &report, &error));
    CHECK(report.readiness == XLS_MERGE_BLOCKED);
    CHECK(report.common_sheet_count == 0);
    CHECK(report.skipped_sheet_count == 1);
    CHECK(report.issue_count == 1);
    if (report.issue_count == 1) {
        CHECK(report.issues[0].code == XLS_ISSUE_ROW_COUNT_MISMATCH);
        CHECK(report.issues[0].workbook_index == 2);
    }
    xls_validation_report_free(&report);
    for (index = 0; index < 3; ++index) {
        xls_workbook_free(&workbooks[index]);
    }
}

int main(void)
{
    test_parse_numbers();
    test_merge_amounts_and_codes();
    test_zero_and_blank_sources();
    test_source_analysis();
    test_parse_real_xlsx();
    test_parse_generated_xls_when_present();
    test_real_sample_merge();
    test_validation_rejects_dimension_mismatch();
    if (failures != 0) {
        fprintf(stderr, "%d test assertion(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    printf("all pure C core tests passed\n");
    return EXIT_SUCCESS;
}
